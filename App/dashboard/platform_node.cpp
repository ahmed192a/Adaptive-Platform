#include "platform_node.h"

#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "ara/core/initialization.h"
#include "ara/diag/diagnostic_server.h"
#include "ara/log/logging.h"
#include "ara/per/persistency.h"
#include "ara/phm/health_monitor.h"
#include "ara/phm/phm_config.h"
#include "ara/phm/phm_error_domain.h"
#include "ara/phm/supervised_entity.h"
#ifdef AP_NODE_CRYPTO
#include "ara/crypto/crypto.h"
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace apdash {
namespace {

// ------------------------------------------------------------------ helpers
HttpResponse Json(json const &j, int status = 200) { return {status, "application/json", j.dump()}; }
HttpResponse Err(std::string const &msg, int status = 400) { return Json({{"ok", false}, {"error", msg}}, status); }

std::string ToHex(std::vector<std::uint8_t> const &v) {
  static char const *d = "0123456789ABCDEF";
  std::string s;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i != 0) s += ' ';
    s += d[v[i] >> 4];
    s += d[v[i] & 0xF];
  }
  return s;
}
bool FromHex(std::string const &text, std::vector<std::uint8_t> &out) {
  out.clear();
  int hi = -1;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    int v = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
            : (c >= 'a' && c <= 'f') ? c - 'a' + 10
            : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (v < 0) return false;
    if (hi < 0) hi = v;
    else {
      out.push_back(static_cast<std::uint8_t>(hi * 16 + v));
      hi = -1;
    }
  }
  return hi < 0;
}
std::int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// ------------------------------------------------------------ log capture
class CaptureSink final : public ara::log::LogSink {
public:
  void Write(ara::log::LogRecord const &r) override {
    std::lock_guard<std::mutex> lk(m_);
    records_.push_back({++seq_, NowMs(), ara::log::ToString(r.level), r.contextId, r.message});
    if (records_.size() > 500) records_.pop_front();
  }
  json Since(std::uint64_t since) {
    std::lock_guard<std::mutex> lk(m_);
    json a = json::array();
    for (auto const &r : records_)
      if (r.seq > since) a.push_back({{"seq", r.seq}, {"t", r.t}, {"level", r.level}, {"ctx", r.ctx}, {"msg", r.msg}});
    return a;
  }

private:
  struct Rec { std::uint64_t seq; std::int64_t t; std::string level, ctx, msg; };
  std::mutex m_;
  std::deque<Rec> records_;
  std::uint64_t seq_{0};
};

struct Watchdog final : apext::phm::WatchdogInterface {
  std::atomic<bool> fired{false};
  std::atomic<std::uint64_t> alive{0};
  void AliveNotification() override { ++alive; }
  void FireWatchdogReaction() override { fired = true; }
};

struct Worker {
  std::string name;
  std::string fg;
  std::atomic<bool> hung{false};
  std::atomic<int> restarts{0};
  std::atomic<bool> stop{false};
  std::thread thread;
};

} // namespace

// ============================================================================
struct PlatformNode::Impl {
  NodeOptions opt;
  std::shared_ptr<CaptureSink> logs = std::make_shared<CaptureSink>();
  ara::log::Logger *log{nullptr};

  // PHM
  std::shared_ptr<Watchdog> watchdog = std::make_shared<Watchdog>();
  std::vector<std::unique_ptr<Worker>> workers;
  std::atomic<bool> autoRecovery{true};
  std::mutex eventsMutex;
  std::deque<json> events;
  std::mutex restartMutex;
  std::vector<std::thread> restartThreads;

  // persistency
  ara::per::SharedHandle<ara::per::KeyValueStorage> kvs;

  // diagnostics
  std::unique_ptr<ara::diag::DiagnosticServer> diag;

  // EM
  std::mutex emMutex;
  pid_t emPid{-1};

#ifdef AP_NODE_CRYPTO
  std::optional<ara::crypto::SecretKey> aesKey, macKey;
  std::optional<ara::crypto::PrivateKey> edKey, ecKey;
#endif

  // ---------------------------------------------------------------- setup
  std::string Init() {
    std::error_code ec;
    fs::create_directories(opt.dataDir, ec);
    if (ec) return "cannot create data dir: " + ec.message();

    ara::log::apext::ClearSinks();
    ara::log::apext::AddSink(logs);
    ara::log::apext::AddSink(std::make_shared<ara::log::ConsoleSink>());
    log = &ara::log::CreateLogger("NODE", "Adaptive Platform demo node", ara::log::LogLevel::kInfo);
    if (auto r = ara::core::Initialize(); !r.HasValue()) return std::string("ara::core::Initialize: ") + r.Error().Message();

    if (auto e = InitPersistency(); !e.empty()) return e;
    IncrementBootCount();
    InitDiag();
    InitCrypto();
    if (auto e = SetupPhm(); !e.empty()) return e;
    for (auto const *n : {"Brake", "Infotainment", "Telematics"}) {
      auto w = std::make_unique<Worker>();
      w->name = n;
      w->fg = std::string("FG_") + n;
      Worker *raw = w.get();
      w->thread = std::thread([this, raw] { WorkerLoop(*raw); });
      workers.push_back(std::move(w));
    }
    log->LogInfo() << "node ready; data dir " << opt.dataDir.string();
    return {};
  }

  void Shutdown() {
    for (auto &w : workers) w->stop = true;
    for (auto &w : workers) if (w->thread.joinable()) w->thread.join();
    {
      std::lock_guard<std::mutex> lk(restartMutex);
      for (auto &t : restartThreads) if (t.joinable()) t.join();
    }
    ara::phm::HealthMonitor::Instance().StopBackgroundEvaluation();
    ara::phm::HealthMonitor::Instance().Clear();
    StopEm();
    (void)ara::core::Deinitialize();
  }

  // ------------------------------------------------------------- persistency
  std::string InitPersistency() {
    json cfg = {
        {"storageRoot", (opt.dataDir / "per").string()},
        {"storages", json::array({{{"specifier", "Node/Kvs"}, {"type", "kvs"}, {"maxSize", 65536},
                                   {"initialValues", {{"bootCount", 0}, {"vehicleName", "Demo Car"}}}}})}};
    auto pc = ara::per::PersistencyConfig::FromJson(cfg.dump());
    if (!pc.HasValue()) return std::string("persistency config: ") + pc.Error().Message();
    if (auto r = ara::per::Configure(std::move(pc).Value()); !r.HasValue())
      return std::string("persistency configure: ") + r.Error().Message();
    auto h = ara::per::OpenKeyValueStorage(ara::core::InstanceSpecifier("Node/Kvs"));
    if (!h.HasValue()) return std::string("open kvs: ") + h.Error().Message();
    kvs = h.Value();
    return {};
  }
  std::uint32_t BootCount() {
    auto v = kvs->GetValue<std::uint32_t>("bootCount");
    return v.HasValue() ? v.Value() : 0;
  }
  void IncrementBootCount() {
    (void)kvs->SetValue<std::uint32_t>("bootCount", BootCount() + 1);
    (void)kvs->SyncToStorage();
  }
  json KvsDump() {
    json out = json::object();
    auto keys = kvs->GetAllKeys();
    if (!keys.HasValue()) return out;
    for (auto const &k : keys.Value()) {
      if (auto i = kvs->GetValue<std::int64_t>(k); i.HasValue()) out[k] = i.Value();
      else if (auto b = kvs->GetValue<bool>(k); b.HasValue()) out[k] = b.Value();
      else if (auto d = kvs->GetValue<double>(k); d.HasValue()) out[k] = d.Value();
      else if (auto s = kvs->GetValue<std::string>(k); s.HasValue()) out[k] = s.Value();
    }
    return out;
  }

  // ------------------------------------------------------------------- PHM
  Worker *FindByFg(std::string const &fg) {
    for (auto &w : workers) if (w->fg == fg) return w.get();
    return nullptr;
  }

  std::string SetupPhm() {
    auto &mon = ara::phm::HealthMonitor::Instance();
    mon.StopBackgroundEvaluation();
    mon.Clear();
    watchdog->fired = false;
    json sup = json::array(), glob = json::array();
    for (auto const *n : {"Brake", "Infotainment", "Telematics"}) {
      std::string name = n;
      sup.push_back({{"name", name + "Alive"}, {"type", "alive"}, {"entity", "Node/" + name}, {"checkpoint", 1},
                     {"expectedAliveIndications", 4}, {"minMargin", 2}, {"maxMargin", 2},
                     {"referenceCycleMs", 200}, {"failedCyclesTolerance", 1}});
      glob.push_back({{"name", name + "Health"}, {"members", json::array({name + "Alive"})},
                      {"functionGroup", "FG_" + name}, {"activeStates", json::array({"on"})},
                      {"recoveryAction", "Node/Recovery"}, {"executionError", 1}});
    }
    auto cfg = ara::phm::PhmConfig::FromJson(json{{"supervisions", sup}, {"globalSupervisions", glob}}.dump());
    if (!cfg.HasValue()) return std::string("phm config: ") + cfg.Error().Message();
    if (auto r = mon.Configure(std::move(cfg).Value()); !r.HasValue())
      return std::string("phm configure: ") + r.Error().Message();
    mon.SetWatchdog(watchdog);
    mon.SetStatusListener([this](std::string const &g, ara::phm::GlobalSupervisionStatus from,
                                 ara::phm::GlobalSupervisionStatus to) {
      {
        std::lock_guard<std::mutex> lk(eventsMutex);
        events.push_back({{"t", NowMs()}, {"name", g}, {"from", ara::phm::ToString(from)}, {"to", ara::phm::ToString(to)}});
        if (events.size() > 200) events.pop_front();
      }
      auto s = log->WithLevel(to == ara::phm::GlobalSupervisionStatus::kOK ? ara::log::LogLevel::kInfo
                              : to == ara::phm::GlobalSupervisionStatus::kDEACTIVATED ? ara::log::LogLevel::kDebug
                                                                                      : ara::log::LogLevel::kWarn);
      s << "supervision " << g << ": " << ara::phm::ToString(from) << " -> " << ara::phm::ToString(to);
    });
    (void)mon.OfferRecoveryAction("Node/Recovery", [this](ara::phm::ExpiredSupervision const &e) {
      ara::core::Promise<void> p;
      Worker *w = FindByFg(e.functionGroup);
      if (autoRecovery && w != nullptr) {
        log->LogWarn() << "recovery action: restarting function group " << e.functionGroup;
        std::lock_guard<std::mutex> lk(restartMutex);
        restartThreads.emplace_back([this, w, fg = e.functionGroup] {
          std::this_thread::sleep_for(2000ms); // time the "State Management" needs for the restart
          w->hung = false;
          ++w->restarts;
          auto &m = ara::phm::HealthMonitor::Instance();
          m.SetFunctionGroupState(fg, "off", ara::core::SteadyClock::now());
          std::this_thread::sleep_for(100ms);
          m.SetFunctionGroupState(fg, "on", ara::core::SteadyClock::now());
          log->LogInfo() << "function group " << fg << " restarted";
        });
        p.set_value();
      } else {
        log->LogError() << "recovery action declined for " << e.functionGroup << " (auto recovery off)";
        p.SetError(ara::phm::MakeErrorCode(ara::phm::PhmErrc::kSMCanNotHandleRecovery));
      }
      return p.get_future();
    });
    auto now = ara::core::SteadyClock::now();
    mon.Start(now);
    for (auto const *n : {"Brake", "Infotainment", "Telematics"})
      mon.SetFunctionGroupState(std::string("FG_") + n, "on", now);
    mon.StartBackgroundEvaluation(20ms);
    return {};
  }

  void WorkerLoop(Worker &w) {
    auto entity = ara::phm::SupervisedEntity::Create(ara::core::InstanceSpecifier("Node/" + w.name));
    if (!entity.HasValue()) return;
    while (!w.stop) {
      if (!w.hung && !watchdog->fired) entity.Value().ReportCheckpoint(1);
      std::this_thread::sleep_for(50ms);
    }
  }

  void SystemReset(char const *why) {
    log->LogFatal() << "SYSTEM RESET (" << why << ")";
    for (auto &w : workers) w->hung = false;
    IncrementBootCount();
    (void)SetupPhm();
  }

  json PhmSnapshot() {
    auto &mon = ara::phm::HealthMonitor::Instance();
    json ws = json::array();
    for (auto &w : workers) {
      auto el = mon.GetElementaryStatus(w->name + "Alive");
      auto gl = mon.GetGlobalStatus(w->name + "Health");
      ws.push_back({{"name", w->name}, {"functionGroup", w->fg}, {"hung", w->hung.load()},
                    {"restarts", w->restarts.load()},
                    {"elementary", el.HasValue() ? ara::phm::ToString(el.Value()) : "?"},
                    {"global", gl.HasValue() ? ara::phm::ToString(gl.Value()) : "?"}});
    }
    json ev = json::array();
    {
      std::lock_guard<std::mutex> lk(eventsMutex);
      for (auto const &e : events) ev.push_back(e);
    }
    return {{"watchdogFired", watchdog->fired.load()}, {"autoRecovery", autoRecovery.load()},
            {"workers", ws}, {"events", ev}};
  }

  // ------------------------------------------------------------ diagnostics
  void InitDiag() {
    using namespace ara::diag;
    using uds::Nrc;
    diag = std::make_unique<DiagnosticServer>();
    auto text = [](std::string s) { return ByteVector(s.begin(), s.end()); };
    auto addRo = [&](std::uint16_t did, std::function<ara::core::Result<ByteVector, Nrc>()> read) {
      DataIdentifier d;
      d.read = std::move(read);
      (void)diag->RegisterDataIdentifier(did, std::move(d));
    };
    addRo(0xF190, [=]() -> ara::core::Result<ByteVector, Nrc> { return text("ADAPTIVEPLAT00001"); });
    addRo(0xF189, [=]() -> ara::core::Result<ByteVector, Nrc> { return text("0.3.0-demo"); });
    addRo(0xF1A0, [this]() -> ara::core::Result<ByteVector, Nrc> {
      auto b = BootCount();
      return ByteVector{static_cast<std::uint8_t>(b >> 24), static_cast<std::uint8_t>(b >> 16),
                        static_cast<std::uint8_t>(b >> 8), static_cast<std::uint8_t>(b)};
    });
    addRo(0xF1A1, [this]() -> ara::core::Result<ByteVector, Nrc> { // one status byte per worker
      auto &mon = ara::phm::HealthMonitor::Instance();
      ByteVector v;
      for (auto &w : workers) {
        auto g = mon.GetGlobalStatus(w->name + "Health");
        v.push_back(g.HasValue() ? static_cast<std::uint8_t>(g.Value()) : 0xFF);
      }
      return v;
    });
    DataIdentifier name;
    name.read = [this]() -> ara::core::Result<ByteVector, Nrc> {
      auto s = kvs->GetValue<std::string>("vehicleName");
      return s.HasValue() ? ByteVector(s.Value().begin(), s.Value().end()) : ByteVector{};
    };
    name.write = [this](ByteVector const &d) -> ara::core::Result<void, Nrc> {
      if (d.size() > 24) return ara::core::Result<void, Nrc>::FromError(Nrc::kIncorrectMessageLengthOrInvalidFormat);
      if (!kvs->SetValue<std::string>("vehicleName", std::string(d.begin(), d.end())).HasValue() ||
          !kvs->SyncToStorage().HasValue())
        return ara::core::Result<void, Nrc>::FromError(Nrc::kGeneralProgrammingFailure);
      log->LogInfo() << "DID F1A2 written: vehicleName persisted";
      return {};
    };
    (void)diag->RegisterDataIdentifier(0xF1A2, std::move(name));
    // routine control (simplified): 31 01 FF 00 = "restart all workers"
    (void)diag->RegisterService(0x31, uds::SessionBit(uds::Session::kExtended),
                                [this](ByteVector const &d) -> ServiceResult {
                                  if (d.size() != 3 || d[0] != 0x01) return ServiceResult::FromError(Nrc::kIncorrectMessageLengthOrInvalidFormat);
                                  if (d[1] != 0xFF || d[2] != 0x00) return ServiceResult::FromError(Nrc::kRequestOutOfRange);
                                  for (auto &w : workers) w->hung = false;
                                  log->LogInfo() << "routine FF00: workers released";
                                  return ByteVector{0x01, 0xFF, 0x00};
                                });
    diag->SetResetHandler([this](std::uint8_t) {
      std::lock_guard<std::mutex> lk(restartMutex);
      restartThreads.emplace_back([this] {
        std::this_thread::sleep_for(200ms);
        SystemReset("UDS ECUReset");
      });
      return true;
    });
  }

  // ---------------------------------------------------------------- crypto
  void InitCrypto() {
#ifdef AP_NODE_CRYPTO
    using namespace ara::crypto;
    aesKey = SecretKey::Generate(32, kUsageEncrypt | kUsageDecrypt).Value();
    macKey = SecretKey::Generate(32, kUsageMac).Value();
    edKey = PrivateKey::Generate(SignatureAlgorithm::kEd25519).Value();
    ecKey = PrivateKey::Generate(SignatureAlgorithm::kEcdsaP256Sha256).Value();
#endif
  }

  HttpResponse Crypto(std::string const &op, json const &in) {
#ifdef AP_NODE_CRYPTO
    using namespace ara::crypto;
    auto bytes = [](std::string const &s) { return ByteVector(s.begin(), s.end()); };
    auto hexs = [](ByteVector const &v) { return ToHex(v); };
    std::string text = in.value("text", "");
    std::string aad = in.value("aad", "");
    auto fail = [](auto const &r) { return Err(r.Error().Message(), 200); };
    if (op == "hash") {
      std::string alg = in.value("alg", "sha256");
      HashAlgorithm a = alg == "sha512" ? HashAlgorithm::kSha512 : alg == "sha384" ? HashAlgorithm::kSha384 : HashAlgorithm::kSha256;
      auto h = Hash(a, bytes(text));
      if (!h.HasValue()) return fail(h);
      return Json({{"ok", true}, {"hex", hexs(h.Value())}});
    }
    if (op == "hmac") {
      auto m = MessageAuthnCode::Create(HashAlgorithm::kSha256, *macKey);
      if (!m.HasValue()) return fail(m);
      (void)m.Value().Update(bytes(text));
      auto t = m.Value().Finish();
      if (!t.HasValue()) return fail(t);
      return Json({{"ok", true}, {"hex", hexs(t.Value())}});
    }
    if (op == "seal") {
      auto s = AeadSeal(*aesKey, bytes(aad), bytes(text));
      if (!s.HasValue()) return fail(s);
      return Json({{"ok", true}, {"hex", hexs(s.Value())}});
    }
    if (op == "open") {
      ByteVector sealed;
      if (!FromHex(in.value("hex", ""), sealed)) return Err("not valid hex", 200);
      auto o = AeadOpen(*aesKey, bytes(aad), sealed);
      if (!o.HasValue()) return Json({{"ok", false}, {"error", o.Error().Message()}, {"authFailed", true}});
      return Json({{"ok", true}, {"text", std::string(o.Value().begin(), o.Value().end())}});
    }
    if (op == "sign" || op == "verify") {
      bool ed = in.value("alg", "ed25519") == "ed25519";
      PrivateKey &pk = ed ? *edKey : *ecKey;
      if (op == "sign") {
        auto s = pk.Sign(bytes(text));
        if (!s.HasValue()) return fail(s);
        return Json({{"ok", true}, {"hex", hexs(s.Value())}});
      }
      ByteVector sig;
      if (!FromHex(in.value("sig", ""), sig)) return Err("signature is not valid hex", 200);
      auto pub = pk.GetPublicKey();
      if (!pub.HasValue()) return fail(pub);
      auto v = pub.Value().Verify(bytes(text), sig);
      return Json({{"ok", true}, {"valid", v.HasValue() && v.Value()}});
    }
    return Err("unknown crypto operation", 404);
#else
    (void)op; (void)in;
    return Err("built without ara::crypto", 501);
#endif
  }

  // -------------------------------------------------------------------- EM
  bool EmRunning() {
    std::lock_guard<std::mutex> lk(emMutex);
    if (emPid <= 0) return false;
    int st = 0;
    if (::waitpid(emPid, &st, WNOHANG) == emPid) {
      emPid = -1;
      return false;
    }
    return true;
  }
  json StartEm() {
    if (EmRunning()) return {{"ok", false}, {"error", "already running"}};
    fs::path exe = opt.platformDir / "Adaptive_Platform";
    if (!fs::exists(exe)) return {{"ok", false}, {"error", "Adaptive_Platform not found in " + opt.platformDir.string()}};
    std::error_code ec;
    fs::create_directories(opt.platformDir / "processes" / "redirected", ec);
    pid_t pid = ::fork();
    if (pid < 0) return {{"ok", false}, {"error", "fork failed"}};
    if (pid == 0) {
      ::setsid();
      if (::chdir(opt.platformDir.c_str()) != 0) ::_exit(127);
      int null = ::open("/dev/null", O_RDWR);
      if (null >= 0) {
        ::dup2(null, 0);
        ::dup2(null, 1);
        ::dup2(null, 2);
      }
      char *argv[] = {const_cast<char *>("Adaptive_Platform"), nullptr};
      ::execv(exe.c_str(), argv);
      ::_exit(127);
    }
    std::lock_guard<std::mutex> lk(emMutex);
    emPid = pid;
    log->LogInfo() << "Execution Management started (pid " << pid << ")";
    return {{"ok", true}, {"pid", pid}};
  }
  void StopEm() {
    pid_t pid;
    {
      std::lock_guard<std::mutex> lk(emMutex);
      pid = emPid;
      emPid = -1;
    }
    if (pid <= 0) return;
    ::kill(-pid, SIGTERM);
    for (int i = 0; i < 30; ++i) {
      int st = 0;
      if (::waitpid(pid, &st, WNOHANG) == pid) break;
      std::this_thread::sleep_for(100ms);
    }
    ::kill(-pid, SIGKILL); // leftovers of the process group
    int st = 0;
    ::waitpid(pid, &st, WNOHANG);
    log->LogInfo() << "Execution Management stopped";
  }
  json EmProcesses() {
    json out = json::array();
    pid_t pg;
    {
      std::lock_guard<std::mutex> lk(emMutex);
      pg = emPid;
    }
    if (pg <= 0) return out;
    std::error_code ec;
    for (auto const &e : fs::directory_iterator("/proc", ec)) {
      auto n = e.path().filename().string();
      if (n.empty() || !std::isdigit(static_cast<unsigned char>(n[0]))) continue;
      std::ifstream f(e.path() / "stat");
      std::string line;
      if (!std::getline(f, line)) continue;
      auto lp = line.find('('), rp = line.rfind(')');
      if (lp == std::string::npos || rp == std::string::npos) continue;
      std::istringstream rest(line.substr(rp + 2));
      std::string state;
      int ppid = 0, pgrp = 0;
      rest >> state >> ppid >> pgrp;
      if (pgrp == pg && state != "Z") out.push_back({{"pid", std::stoi(n)}, {"name", line.substr(lp + 1, rp - lp - 1)}});
    }
    return out;
  }
  std::string ReadTail(fs::path const &p, std::size_t maxBytes) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    auto size = static_cast<std::size_t>(f.tellg());
    std::size_t off = size > maxBytes ? size - maxBytes : 0;
    f.seekg(static_cast<std::streamoff>(off));
    std::string s(size - off, '\0');
    f.read(s.data(), static_cast<std::streamsize>(s.size()));
    return s;
  }

  // ---------------------------------------------------------------- routing
  HttpResponse Static(std::string path) {
    if (path == "/") path = "/index.html";
    if (path.find("..") != std::string::npos) return {403, "text/plain", "forbidden"};
    fs::path p = opt.webRoot / path.substr(1);
    std::ifstream f(p, std::ios::binary);
    if (!f) return {404, "text/plain", "not found"};
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto ext = p.extension().string();
    std::string ct = ext == ".html" ? "text/html; charset=utf-8" : ext == ".css" ? "text/css"
                     : ext == ".js" ? "application/javascript" : ext == ".svg" ? "image/svg+xml" : "application/octet-stream";
    return {200, ct, std::move(body)};
  }

  HttpResponse Handle(HttpRequest const &req) {
    if (req.path.rfind("/api/", 0) != 0) return req.method == "GET" ? Static(req.path) : HttpResponse{405, "text/plain", "method not allowed"};
    json in = json::object();
    if (req.method == "POST" && !req.body.empty()) {
      in = json::parse(req.body, nullptr, false);
      if (in.is_discarded() || !in.is_object()) return Err("body must be a JSON object");
    }
    std::string const &p = req.path;

    if (p == "/api/overview") {
      auto since = req.query.count("since") ? std::strtoull(req.query.at("since").c_str(), nullptr, 10) : 0ULL;
      json j = {{"time", NowMs()},
                {"phm", PhmSnapshot()},
                {"diag", {{"session", static_cast<int>(diag->CurrentSession())}}},
                {"per", KvsDump()},
                {"em", {{"running", EmRunning()}, {"processes", EmProcesses()}}},
                {"crypto",
#ifdef AP_NODE_CRYPTO
                 true
#else
                 false
#endif
                },
                {"logs", logs->Since(since)}};
      return Json(j);
    }
    if (p == "/api/phm/hang" || p == "/api/phm/heal") {
      for (auto &w : workers)
        if (w->name == in.value("name", "")) {
          w->hung = (p == "/api/phm/hang");
          log->LogWarn() << (w->hung ? "fault injected: " : "fault cleared: ") << w->name;
          return Json({{"ok", true}});
        }
      return Err("unknown worker", 404);
    }
    if (p == "/api/phm/auto-recovery") {
      autoRecovery = in.value("enabled", true);
      log->LogInfo() << "auto recovery " << (autoRecovery ? "enabled" : "disabled");
      return Json({{"ok", true}});
    }
    if (p == "/api/system/reset") {
      SystemReset("operator");
      return Json({{"ok", true}});
    }
    if (p == "/api/diag") {
      std::vector<std::uint8_t> r;
      if (!FromHex(in.value("request", ""), r) || r.empty()) return Err("request must be hex bytes, e.g. 22 F1 90");
      auto resp = diag->Dispatch(r);
      json j = {{"ok", true}, {"request", ToHex(r)}, {"session", static_cast<int>(diag->CurrentSession())}};
      j["response"] = resp ? json(ToHex(*resp)) : json(nullptr);
      return Json(j);
    }
    if (p == "/api/per/set") {
      std::string key = in.value("key", "");
      if (key.empty() || !in.contains("value")) return Err("key and value required");
      auto const &v = in["value"];
      ara::core::Result<void> r = v.is_boolean() ? kvs->SetValue<bool>(key, v.get<bool>())
                                : v.is_number_integer() ? kvs->SetValue<std::int64_t>(key, v.get<std::int64_t>())
                                : v.is_number() ? kvs->SetValue<double>(key, v.get<double>())
                                : kvs->SetValue<std::string>(key, v.is_string() ? v.get<std::string>() : v.dump());
      if (r.HasValue()) r = kvs->SyncToStorage();
      if (!r.HasValue()) return Err(r.Error().Message(), 200);
      log->LogInfo() << "persisted key '" << key << "'";
      return Json({{"ok", true}});
    }
    if (p == "/api/per/delete") {
      auto r = kvs->RemoveKey(in.value("key", ""));
      if (r.HasValue()) r = kvs->SyncToStorage();
      return r.HasValue() ? Json({{"ok", true}}) : Err(r.Error().Message(), 200);
    }
    if (p.rfind("/api/crypto/", 0) == 0) return Crypto(p.substr(12), in);
    if (p == "/api/em/start") return Json(StartEm());
    if (p == "/api/em/stop") {
      StopEm();
      return Json({{"ok", true}});
    }
    if (p == "/api/em/log") {
      std::string name = req.query.count("name") ? req.query.at("name") : "EM.txt";
      if (name.find('/') != std::string::npos || name.find("..") != std::string::npos || name.size() < 5 || name.substr(name.size() - 4) != ".txt")
        return Err("bad file name", 403);
      return Json({{"ok", true}, {"name", name}, {"text", ReadTail(opt.platformDir / "processes" / "redirected" / name, 40000)}});
    }
    if (p == "/api/em/files") {
      json files = json::array();
      std::error_code ec;
      for (auto const &e : fs::directory_iterator(opt.platformDir / "processes" / "redirected", ec))
        if (e.path().extension() == ".txt") files.push_back(e.path().filename().string());
      return Json({{"ok", true}, {"files", files}});
    }
    return Err("not found", 404);
  }
};

PlatformNode::PlatformNode(NodeOptions opts) : impl_(std::make_unique<Impl>()) { impl_->opt = std::move(opts); }
PlatformNode::~PlatformNode() {
  if (impl_) impl_->Shutdown();
}
std::string PlatformNode::Init() { return impl_->Init(); }
HttpResponse PlatformNode::Handle(HttpRequest const &req) { return impl_->Handle(req); }

} // namespace apdash
