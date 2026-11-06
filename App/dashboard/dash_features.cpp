#include "dash_features.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>

#include "ara/nm/nm_channel.h"
#include "ara/tsync/synchronized_time_base.h"
#include "ota_updater.h"
#include "uds_scenario.h"
#ifdef AP_NODE_CRYPTO
#include "ara/crypto/crypto.h"
#include "ara/crypto/key_slot.h"
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace apdash {
namespace {

HttpResponse Json(json const &j, int status = 200) { return {status, "application/json", j.dump()}; }
HttpResponse Err(std::string const &msg, int status = 200) { return Json({{"ok", false}, {"error", msg}}, status); }

/// Integer field with a default and a clamp; non-numbers fall back to the default.
std::int64_t IntField(json const &in, char const *key, std::int64_t def, std::int64_t lo, std::int64_t hi) {
  std::int64_t v = def;
  if (in.contains(key) && in[key].is_number()) v = in[key].get<std::int64_t>();
  return std::clamp(v, lo, hi);
}

std::string HexOf(std::vector<std::uint8_t> const &v) {
  static char const *d = "0123456789abcdef";
  std::string s;
  for (auto b : v) {
    s += d[b >> 4];
    s += d[b & 0xF];
  }
  return s;
}
bool UnhexOf(std::string const &text, std::vector<std::uint8_t> &out) {
  out.clear();
  int hi = -1;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) continue;
    int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    if (v < 0) return false;
    if (hi < 0) {
      hi = v;
    } else {
      out.push_back(static_cast<std::uint8_t>((hi << 4) | v));
      hi = -1;
    }
  }
  return hi < 0;
}

template <typename T> void Push(std::deque<json> &q, T &&item, std::size_t cap) {
  q.push_back(std::forward<T>(item));
  while (q.size() > cap) q.pop_front();
}

// =================================================================== NM
/// Three ECUs on a virtual bus, driven by a manual clock (time only moves on /api/nm/advance).
struct NmSim {
  struct Ecu {
    std::string name;
    std::uint32_t pnMask{0};
    std::unique_ptr<ara::nm::NmChannel> ch;
  };
  std::shared_ptr<ara::nm::ManualClock> clock;
  std::vector<std::unique_ptr<Ecu>> ecus;
  std::deque<json> events;
  std::uint64_t seq{0}, pdus{0};

  NmSim() { Reset(); }

  void Add(std::string name, std::uint8_t node, std::uint32_t pnMask) {
    using namespace ara::nm;
    NmConfig cfg;
    cfg.nodeId = node;
    cfg.pnFilterMask = pnMask;
    cfg.repeatMessageTime = Millis(600);
    cfg.messageCycleTime = Millis(200);
    cfg.nmTimeout = Millis(800);
    cfg.waitBusSleepTime = Millis(500);
    cfg.pnResetTime = Millis(1200);
    auto r = NmChannel::Create(name, cfg, clock);
    if (!r.HasValue()) return;
    auto e = std::make_unique<Ecu>();
    e->name = std::move(name);
    e->pnMask = pnMask;
    e->ch = std::move(r).Value();
    Ecu *raw = e.get();
    raw->ch->SetStateChangeHandler([this, raw](NmState from, NmState to) {
      Push(events, json{{"seq", ++seq}, {"t", clock->Now().count()}, {"ecu", raw->name}, {"from", ToString(from)}, {"to", ToString(to)}}, 80);
    });
    raw->ch->SetTxHandler([this, raw](NmPdu const &p) {
      ++pdus;
      for (auto &o : ecus)
        if (o.get() != raw) o->ch->OnPduReceived(p);
    });
    ecus.push_back(std::move(e));
  }

  void Reset() {
    ecus.clear();
    events.clear();
    seq = 0;
    pdus = 0;
    clock = std::make_shared<ara::nm::ManualClock>();
    Add("Gateway", 1, 0x3);
    Add("Door", 2, 0x1);
    Add("Camera", 3, 0x2);
  }

  Ecu *Find(std::string const &n) {
    for (auto &e : ecus)
      if (e->name == n) return e.get();
    return nullptr;
  }

  void Advance(std::int64_t ms) {
    for (std::int64_t t = 0; t < ms; t += 10) {
      clock->Advance(ara::nm::Millis(10));
      for (auto &e : ecus) e->ch->Process();
    }
  }

  HttpResponse Handle(std::string const &op, json const &in) {
    if (op == "reset") {
      Reset();
      return Json({{"ok", true}});
    }
    if (op == "advance") {
      Advance(IntField(in, "ms", 200, 10, 60000));
      return Json({{"ok", true}});
    }
    if (op != "request" && op != "release") return Err("unknown nm operation", 404);
    Ecu *e = Find(in.value("ecu", ""));
    if (e == nullptr) return Err("unknown ecu");
    bool const pn = in.contains("pn") && in["pn"].is_number();
    auto const id = static_cast<std::uint8_t>(IntField(in, "pn", 0, 0, 31));
    ara::core::Result<void> r = op == "request" ? (pn ? e->ch->RequestPartialNetwork(id) : e->ch->Request())
                                                : (pn ? e->ch->ReleasePartialNetwork(id) : e->ch->Release());
    if (!r.HasValue()) return Err(std::string(r.Error().Message()));
    return Json({{"ok", true}});
  }

  json Snapshot() {
    json ecuList = json::array();
    for (auto &e : ecus) {
      json pn = json::array();
      for (int i = 0; i < 4; ++i) {
        auto r = e->ch->IsPartialNetworkRequested(static_cast<std::uint8_t>(i));
        pn.push_back(r.HasValue() && r.Value());
      }
      ecuList.push_back({{"name", e->name}, {"state", ara::nm::ToString(e->ch->GetState())}, {"requested", e->ch->IsRequested()},
                         {"pnMask", e->pnMask}, {"pn", pn}});
    }
    return {{"time", clock->Now().count()}, {"pdus", pdus}, {"ecus", ecuList}, {"events", events}};
  }
};

// =================================================================== time sync
class TsyncSim {
public:
  TsyncSim() { Reset(); }
  ~TsyncSim() { Teardown(); }

  void Reset() {
    Teardown();
    using namespace ara::tsync;
    now_ = origin_;
    masterMs_ = 5000;
    events_.clear();
    seq_ = 0;
    TimeBaseConfig cfg;
    cfg.name = kName;
    cfg.clock = [this] { return now_; };
    cfg.timeout = std::chrono::seconds(1);
    cfg.stepThreshold = std::chrono::milliseconds(50);
    cfg.slewWindow = std::chrono::milliseconds(400);
    cfg.allowProviderRateCorrection = true;
    cfg.providerRateDeviationMax = 1e-3;
    cfg.rateCorrectionThreshold = 1e-4;
    auto r = TimeBaseResource::Create(cfg);
    if (!r.HasValue()) return;
    tb_ = r.Value();
    TimeBaseRegistry::Bind(kName, tb_);
    bound_ = true;
    consumer_ = std::make_unique<SynchronizedTimeBaseConsumer>(ara::core::InstanceSpecifier{kName});
    provider_ = std::make_unique<TimeBaseProvider>(tb_);
    (void)consumer_->RegisterSynchronizationStateChangeNotifier(
        [this](SynchronizationStatus s) { Event(std::string("state -> ") + Name(s)); });
    (void)consumer_->RegisterTimeLeapNotifier([this](LeapJump j) { Event(std::string("time leap ") + Name(j)); });
  }

  HttpResponse Handle(std::string const &op, json const &in) {
    using namespace ara::tsync;
    if (!provider_) return Err("time base unavailable");
    if (op == "reset") {
      Reset();
      return Json({{"ok", true}});
    }
    if (op == "advance") {
      auto ms = IntField(in, "ms", 100, 1, 60000);
      now_ += std::chrono::milliseconds(ms);
      masterMs_ += ms;
      (void)consumer_->GetTimeWithStatus(); // lets the notifiers fire (timeout detection)
      return Json({{"ok", true}});
    }
    if (op == "sync") {
      auto offset = IntField(in, "offsetMs", 0, -100000, 100000);
      double rate = in.contains("rate") && in["rate"].is_number() ? in["rate"].get<double>() : 0.0;
      masterMs_ += offset;
      auto t = Timestamp(std::chrono::duration_cast<Duration>(std::chrono::milliseconds(masterMs_)));
      auto r = rate != 0.0 ? provider_->Update(t, rate) : provider_->Update(t);
      if (!r.HasValue()) return Err(std::string(r.Error().Message()));
      Event("master update " + std::to_string(masterMs_) + " ms" + (rate != 0.0 ? " (rate " + std::to_string(rate) + ")" : ""));
      return Json({{"ok", true}});
    }
    return Err("unknown tsync operation", 404);
  }

  json Snapshot() {
    using namespace ara::tsync;
    if (!consumer_) return {{"available", false}};
    auto s = consumer_->GetTimeWithStatus();
    auto timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(s.GetCreationTime().time_since_epoch()).count();
    return {{"available", true},
            {"localMs", std::chrono::duration_cast<std::chrono::milliseconds>(now_ - origin_).count()},
            {"timeMs", timeMs},
            {"masterMs", masterMs_},
            {"offsetMs", timeMs - masterMs_},
            {"status", Name(s.GetSynchronizationStatus())},
            {"leap", Name(s.GetLeapJump())},
            {"rateCorrected", s.GetRateCorrected()},
            {"rateExceeded", s.GetRateExceeded()},
            {"events", events_}};
  }

private:
  static constexpr char const *kName = "Dashboard/VehicleTime";
  static char const *Name(ara::tsync::SynchronizationStatus s) {
    using S = ara::tsync::SynchronizationStatus;
    switch (s) {
      case S::kNotSynchronizedUntilStartup: return "NotSynchronized";
      case S::kTimeOut: return "TimeOut";
      case S::kSynchronized: return "Synchronized";
      case S::kSynchToGateway: return "SynchToGateway";
    }
    return "?";
  }
  static char const *Name(ara::tsync::LeapJump j) {
    return j == ara::tsync::LeapJump::kTimeLeapNone ? "none" : j == ara::tsync::LeapJump::kTimeLeapFuture ? "future" : "past";
  }
  void Event(std::string text) {
    Push(events_, json{{"seq", ++seq_},
                       {"t", std::chrono::duration_cast<std::chrono::milliseconds>(now_ - origin_).count()},
                       {"text", std::move(text)}},
         60);
  }
  void Teardown() {
    consumer_.reset();
    provider_.reset();
    if (bound_) ara::tsync::TimeBaseRegistry::Unbind(kName);
    bound_ = false;
    tb_.reset();
  }

  std::chrono::steady_clock::time_point const origin_{std::chrono::steady_clock::time_point{} + std::chrono::hours(1)};
  std::chrono::steady_clock::time_point now_{origin_};
  std::int64_t masterMs_{5000};
  std::shared_ptr<ara::tsync::TimeBaseResource> tb_;
  std::unique_ptr<ara::tsync::SynchronizedTimeBaseConsumer> consumer_;
  std::unique_ptr<ara::tsync::TimeBaseProvider> provider_;
  bool bound_{false};
  std::deque<json> events_;
  std::uint64_t seq_{0};
};

// =================================================================== extended diagnostics
struct UdsxSim {
  std::unique_ptr<ap::demo::DemoEcu> ecu;
  std::deque<json> trace;
  std::uint64_t seq{0};

  UdsxSim() { Reset(); }
  void Reset() {
    ecu = std::make_unique<ap::demo::DemoEcu>();
    trace.clear();
    seq = 0;
  }

  std::optional<ara::diag::ByteVector> Send(ara::diag::ByteVector const &req) {
    auto resp = ecu->Server().Dispatch(req);
    bool const neg = resp && !resp->empty() && (*resp)[0] == 0x7F;
    json row = {{"seq", ++seq},
                {"kind", "uds"},
                {"req", ap::demo::HexString(req)},
                {"reqText", ap::demo::Describe(req)},
                {"neg", neg}};
    row["resp"] = resp ? json(ap::demo::HexString(*resp)) : json(nullptr);
    row["respText"] = resp ? json(ap::demo::Describe(*resp)) : json("no response (suppressed or ignored)");
    Push(trace, std::move(row), 40);
    return resp;
  }

  HttpResponse Handle(std::string const &op, json const &in) {
    if (op == "reset") {
      Reset();
      return Json({{"ok", true}});
    }
    if (op == "advance") {
      ecu->Advance(std::chrono::milliseconds(IntField(in, "ms", 1000, 1, 600000)));
      return Json({{"ok", true}});
    }
    if (op == "request") {
      auto bytes = ap::demo::ParseHex(in.value("request", ""));
      if (!bytes || bytes->empty()) return Err("request must be hex bytes, e.g. 22 F1 90");
      Send(*bytes);
      return Json({{"ok", true}});
    }
    if (op == "fault") {
      auto dtc = static_cast<std::uint32_t>(IntField(in, "dtc", 0xC10100, 1, 0xFFFFFF));
      bool active = in.value("active", true);
      ecu->Server().ReportDtc(dtc, active);
      if (active && dtc == 0xC10100) ecu->Server().SetDtcSnapshot(dtc, 1, {{0xF190, ara::diag::ByteVector{'A', 'D', 'A', 'P', 'T', 'I', 'V', 'E'}}});
      char buf[16];
      std::snprintf(buf, sizeof buf, "%06X", dtc);
      Push(trace, json{{"seq", ++seq}, {"kind", "fault"}, {"text", std::string(active ? "fault injected: DTC " : "fault healed: DTC ") + buf}}, 40);
      return Json({{"ok", true}});
    }
    if (op == "unlock") { // seed/key exchange with the demo algorithm (key = seed XOR 0xA5)
      auto seed = Send({0x27, 0x01});
      if (!seed || seed->size() < 3 || (*seed)[0] != 0x67) return Json({{"ok", false}, {"error", "seed request refused"}});
      ara::diag::ByteVector key{0x27, 0x02};
      for (std::size_t i = 2; i < seed->size(); ++i) key.push_back(static_cast<std::uint8_t>((*seed)[i] ^ 0xA5));
      auto r = Send(key);
      return Json({{"ok", r && !r->empty() && (*r)[0] == 0x67}});
    }
    return Err("unknown udsx operation", 404);
  }

  json Snapshot() {
    return {{"name", ecu->Name()}, {"session", static_cast<int>(ecu->Server().CurrentSession())}, {"trace", trace}};
  }
};

// =================================================================== software update
struct UpdateSim {
  json last = nullptr;
  std::uint64_t runs{0};

  HttpResponse Handle(std::string const &op, json const &in) {
    if (op != "run") return Err("unknown update operation", 404);
    ap::demo::UpdateOptions o;
    o.packageSize = static_cast<std::uint64_t>(IntField(in, "size", 20000, 1, 1 << 20));
    o.tamper = in.value("tamper", false);
    o.rejectSession = in.value("rejectSession", false);
    o.failVerify = in.value("failVerify", false);
    o.quiet = true;
    std::ostringstream sink;
    auto rep = ap::demo::RunUpdate(o, sink);
    last = {{"run", ++runs},
            {"outcome", ap::demo::ToString(rep.outcome)},
            {"exit", ap::demo::ExitCode(rep)},
            {"ucmStatus", rep.ucmStatus},
            {"machineState", rep.machineState},
            {"ucmError", rep.ucmError},
            {"blocks", rep.blocks},
            {"size", o.packageSize},
            {"message", rep.message},
            {"statusTrail", rep.statusTrail},
            {"machineTrail", rep.machineTrail},
            {"options", {{"tamper", o.tamper}, {"rejectSession", o.rejectSession}, {"failVerify", o.failVerify}}}};
    return Json({{"ok", true}, {"report", last}});
  }
};

// =================================================================== key slots
#ifdef AP_NODE_CRYPTO
struct KeySim {
  ara::per::SharedHandle<ara::per::KeyValueStorage> storage;
  std::optional<ara::crypto::KeyStorageProvider> provider;
  ara::crypto::ByteVector wrap;

  using Proto = ara::crypto::KeySlotPrototype;
  static std::vector<Proto> Protos() {
    using namespace ara::crypto;
    return {{"vault-aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, true},
            {"vault-hmac", SlotAlgorithm::kHmacSha256, kSlotMac, false, true},
            {"vault-ed25519", SlotAlgorithm::kEd25519, kSlotSign | kSlotVerify, false, true},
            {"session-aes", SlotAlgorithm::kAes256Gcm, kSlotEncrypt | kSlotDecrypt, false, false}};
  }

  std::string Init(fs::path const &dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    fs::path keyFile = dir / "vault.key";
    std::string hex;
    {
      std::ifstream f(keyFile);
      std::getline(f, hex);
    }
    if (!UnhexOf(hex, wrap) || wrap.size() != 32) {
      auto rnd = ara::crypto::GenerateRandom(32);
      if (!rnd.HasValue()) return "no randomness";
      wrap = rnd.Value();
      std::ofstream f(keyFile, std::ios::trunc);
      f << HexOf(wrap) << "\n";
      f.close();
      ::chmod(keyFile.c_str(), 0600);
    }
    return Reload();
  }

  std::string Reload() {
    provider.reset();
    auto p = ara::crypto::KeyStorageProvider::Create(storage, ara::crypto::ByteView(wrap));
    if (!p.HasValue()) return std::string(p.Error().Message());
    provider.emplace(std::move(p).Value());
    for (auto const &proto : Protos()) (void)provider->DefineSlot(proto);
    return {};
  }

  static char const *AlgName(ara::crypto::SlotAlgorithm a) {
    using A = ara::crypto::SlotAlgorithm;
    return a == A::kAes256Gcm ? "AES-256-GCM" : a == A::kHmacSha256 ? "HMAC-SHA256" : a == A::kEd25519 ? "Ed25519" : "ECDSA-P256";
  }
  static std::string UsageText(std::uint32_t u) {
    std::string s;
    auto add = [&](std::uint32_t bit, char const *n) {
      if ((u & bit) == 0) return;
      if (!s.empty()) s += ",";
      s += n;
    };
    add(ara::crypto::kSlotEncrypt, "encrypt");
    add(ara::crypto::kSlotDecrypt, "decrypt");
    add(ara::crypto::kSlotMac, "mac");
    add(ara::crypto::kSlotSign, "sign");
    add(ara::crypto::kSlotVerify, "verify");
    return s;
  }

  HttpResponse Handle(std::string const &op, json const &in) {
    if (!provider) return Err("key storage unavailable");
    using namespace ara::crypto;
    std::string const name = in.value("name", "");
    std::string const text = in.value("text", "");
    ByteVector const msg(text.begin(), text.end());
    std::string const aadText = in.value("aad", "");
    ByteVector const aad(aadText.begin(), aadText.end());
    auto fail = [](auto const &r) { return Err(std::string(r.Error().Message())); };
    if (op == "reload") {
      auto e = Reload();
      return e.empty() ? Json({{"ok", true}}) : Err(e);
    }
    if (op == "generate") {
      auto r = provider->Generate(name);
      return r.HasValue() ? Json({{"ok", true}}) : fail(r);
    }
    if (op == "clear") {
      auto r = provider->Clear(name);
      return r.HasValue() ? Json({{"ok", true}}) : fail(r);
    }
    if (op == "export") {
      auto r = provider->Export(name);
      return r.HasValue() ? Json({{"ok", true}, {"hex", HexOf(r.Value())}}) : fail(r);
    }
    if (op == "seal") {
      auto r = provider->Seal(name, ByteView(aad), ByteView(msg));
      return r.HasValue() ? Json({{"ok", true}, {"hex", HexOf(r.Value())}}) : fail(r);
    }
    if (op == "open") {
      ByteVector sealed;
      if (!UnhexOf(in.value("hex", ""), sealed)) return Err("not valid hex");
      auto r = provider->Open(name, ByteView(aad), ByteView(sealed));
      if (!r.HasValue()) return Json({{"ok", false}, {"error", std::string(r.Error().Message())}, {"authFailed", true}});
      return Json({{"ok", true}, {"text", std::string(r.Value().begin(), r.Value().end())}});
    }
    if (op == "mac") {
      auto r = provider->ComputeMac(name, ByteView(msg));
      return r.HasValue() ? Json({{"ok", true}, {"hex", HexOf(r.Value())}}) : fail(r);
    }
    if (op == "sign") {
      auto r = provider->Sign(name, ByteView(msg));
      return r.HasValue() ? Json({{"ok", true}, {"hex", HexOf(r.Value())}}) : fail(r);
    }
    if (op == "verify") {
      ByteVector sig;
      if (!UnhexOf(in.value("hex", ""), sig)) return Err("not valid hex");
      auto r = provider->Verify(name, ByteView(msg), ByteView(sig));
      return r.HasValue() ? Json({{"ok", true}, {"valid", r.Value()}}) : fail(r);
    }
    if (op == "tamper") { // flips one hex digit of the stored blob and reloads, like a storage attacker would
      bool known = false;
      for (auto const &p : Protos()) known = known || (p.name == name && p.persistent);
      if (!known) return Err("not a persistent slot");
      std::string key = "keyslot/" + name;
      auto v = storage->GetValue<std::string>(key);
      if (!v.HasValue()) return Err("slot has no stored key yet: generate it first");
      std::string s = v.Value();
      if (s.size() < 8) return Err("stored blob is too short");
      s[30 % s.size()] = s[30 % s.size()] == '0' ? '1' : '0';
      if (!storage->SetValue(key, s).HasValue() || !storage->SyncToStorage().HasValue()) return Err("storage write failed");
      auto e = Reload();
      return e.empty() ? Json({{"ok", true}}) : Err(e);
    }
    return Err("unknown keys operation", 404);
  }

  json Snapshot() {
    json slots = json::array();
    if (provider)
      for (auto const &i : provider->ListSlots()) {
        char const *st = i.state == ara::crypto::SlotState::kEmpty ? "empty" : i.state == ara::crypto::SlotState::kOccupied ? "occupied" : "corrupted";
        slots.push_back({{"name", i.prototype.name}, {"algorithm", AlgName(i.prototype.algorithm)},
                         {"usage", UsageText(i.prototype.usage)}, {"persistent", i.prototype.persistent},
                         {"exportable", i.prototype.exportable}, {"state", st}});
      }
    return {{"enabled", provider.has_value()}, {"slots", slots}};
  }
};
#endif

// =================================================================== demo app launcher
struct AppSpec {
  char const *name;
  char const *title;
  char const *about;
  std::vector<std::pair<std::string, std::vector<std::string>>> modes; ///< mode -> arguments
};

std::vector<AppSpec> const &Apps() {
  static std::vector<AppSpec> const apps = {
      {"supervised_service", "Supervised service", "alive supervision, hang, expiry and recovery (ara::phm + ara::exec)", {{"scenario", {"--scenario"}}}},
      {"uds_tester", "UDS tester", "scripted diagnostic session against a demo ECU (ara::diag)", {{"run", {}}}},
      {"nm_bus_monitor", "NM bus monitor", "3 ECUs, partial networks, state timeline (ara::nm)", {{"run", {}}}},
      {"time_sync_clock", "Time-sync clock", "slew vs step, rate correction, timeout (ara::tsync)", {{"run", {}}}},
      {"ota_updater", "OTA updater", "package update with rollback (ara::ucm + ara::sm)",
       {{"update", {}}, {"tamper", {"--tamper"}}, {"rollback", {"--fail-verify"}}, {"reject", {"--reject-session"}}}},
      {"persist_logger_app", "Persist & log", "logging levels, boot counter, file recovery (ara::log + ara::per)",
       {{"run", {"--dir", "@DATA@/pla-run"}}, {"corrupt", {"--dir", "@DATA@/pla-corrupt", "--corrupt"}}}},
  };
  return apps;
}

struct RunResult {
  int exitCode{-1};
  bool timedOut{false};
  bool truncated{false};
  std::string output;
};

/// fork/exec with stdout+stderr captured (bounded in size and time). Only called with whitelisted binaries.
RunResult RunProcess(fs::path const &exe, std::vector<std::string> const &args, fs::path const &cwd, int timeoutMs) {
  RunResult res;
  int pipefd[2];
  if (::pipe(pipefd) != 0) {
    res.output = "pipe failed";
    return res;
  }
  std::vector<std::string> store{exe.string()};
  store.insert(store.end(), args.begin(), args.end());
  std::vector<char *> argv;
  for (auto &s : store) argv.push_back(s.data());
  argv.push_back(nullptr);
  std::string const cwdStr = cwd.string();

  pid_t pid = ::fork();
  if (pid < 0) {
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    res.output = "fork failed";
    return res;
  }
  if (pid == 0) {
    ::setsid();
    int devnull = ::open("/dev/null", O_RDONLY);
    if (devnull >= 0) ::dup2(devnull, 0);
    ::dup2(pipefd[1], 1);
    ::dup2(pipefd[1], 2);
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    if (::chdir(cwdStr.c_str()) != 0) ::_exit(126);
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }
  ::close(pipefd[1]);
  constexpr std::size_t kMaxOut = 64 * 1024;
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0) {
      res.timedOut = true;
      break;
    }
    pollfd pfd{pipefd[0], POLLIN, 0};
    int pr = ::poll(&pfd, 1, static_cast<int>(std::min<long long>(left, 200)));
    if (pr < 0 && errno != EINTR) break;
    if (pr > 0) {
      char buf[4096];
      ssize_t n = ::read(pipefd[0], buf, sizeof buf);
      if (n <= 0) break;
      if (res.output.size() < kMaxOut) res.output.append(buf, static_cast<std::size_t>(n));
      else res.truncated = true;
    }
  }
  ::close(pipefd[0]);
  if (res.timedOut) ::kill(-pid, SIGKILL);
  int status = 0;
  ::waitpid(pid, &status, 0);
  if (WIFEXITED(status)) res.exitCode = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) res.exitCode = 128 + WTERMSIG(status);
  if (res.output.size() > kMaxOut) {
    res.output.resize(kMaxOut);
    res.truncated = true;
  }
  return res;
}

} // namespace

// ===================================================================== Features
struct Features::Impl {
  Options opt;
  std::mutex mutex;
  NmSim nm;
  TsyncSim tsync;
  UdsxSim udsx;
  UpdateSim update;
#ifdef AP_NODE_CRYPTO
  KeySim keys;
#endif
  std::mutex appMutex;
  json lastApp = nullptr;
};

Features::Features(Options opts) : impl_(std::make_unique<Impl>()) {
  impl_->opt = std::move(opts);
#ifdef AP_NODE_CRYPTO
  if (impl_->opt.keyStorage) {
    impl_->keys.storage = impl_->opt.keyStorage;
    (void)impl_->keys.Init(impl_->opt.dataDir / "vault");
  }
#endif
}
Features::~Features() = default;

bool Features::Owns(std::string const &p) {
  for (char const *prefix : {"/api/nm/", "/api/tsync/", "/api/udsx/", "/api/update/", "/api/keys/", "/api/apps/"})
    if (p.rfind(prefix, 0) == 0) return true;
  return false;
}

HttpResponse Features::Handle(std::string const &path, json const &in) {
  auto slash = path.find('/', 5);
  std::string const area = path.substr(5, slash - 5);
  std::string const op = slash == std::string::npos ? "" : path.substr(slash + 1);
  Impl &m = *impl_;

  if (area == "apps") {
    if (op != "run") return Err("unknown apps operation", 404);
    std::string const name = in.value("name", "");
    std::string const mode = in.value("mode", "");
    for (auto const &spec : Apps()) {
      if (name != spec.name) continue;
      for (auto const &md : spec.modes) {
        if (md.first != mode) continue;
        fs::path exe = m.opt.demoDir / spec.name / spec.name;
        std::error_code ec;
        if (!fs::exists(exe, ec) || ::access(exe.c_str(), X_OK) != 0) return Err("demo binary not built: " + exe.string());
        std::unique_lock<std::mutex> lk(m.appMutex, std::try_to_lock);
        if (!lk.owns_lock()) return Err("another demo app is still running", 409);
        fs::path work = m.opt.dataDir / "apps";
        fs::create_directories(work, ec);
        std::vector<std::string> args;
        for (auto a : md.second) {
          if (a.rfind("@DATA@", 0) == 0) {
            a = (work / a.substr(7)).string();
            fs::remove_all(a, ec); // every run starts from a clean slate
          }
          args.push_back(std::move(a));
        }
        auto t0 = std::chrono::steady_clock::now();
        RunResult r = RunProcess(exe, args, work, 40000);
        json j = {{"ok", true}, {"name", name}, {"mode", mode}, {"exit", r.exitCode}, {"timedOut", r.timedOut},
                  {"truncated", r.truncated}, {"output", r.output},
                  {"ms", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()}};
        std::lock_guard<std::mutex> g(m.mutex);
        m.lastApp = j;
        return Json(j);
      }
      return Err("unknown mode", 404);
    }
    return Err("unknown demo app", 404);
  }

  std::lock_guard<std::mutex> g(m.mutex);
  if (area == "nm") return m.nm.Handle(op, in);
  if (area == "tsync") return m.tsync.Handle(op, in);
  if (area == "udsx") return m.udsx.Handle(op, in);
  if (area == "update") return m.update.Handle(op, in);
  if (area == "keys") {
#ifdef AP_NODE_CRYPTO
    return m.keys.Handle(op, in);
#else
    return Err("built without crypto support");
#endif
  }
  return Err("not found", 404);
}

json Features::Snapshot() {
  Impl &m = *impl_;
  json apps = json::array();
  for (auto const &s : Apps()) {
    json modes = json::array();
    for (auto const &md : s.modes) modes.push_back(md.first);
    std::error_code ec;
    fs::path exe = m.opt.demoDir / s.name / s.name;
    apps.push_back({{"name", s.name}, {"title", s.title}, {"about", s.about}, {"modes", modes}, {"available", fs::exists(exe, ec)}});
  }
  std::lock_guard<std::mutex> g(m.mutex);
  json keys = {{"enabled", false}, {"slots", json::array()}};
#ifdef AP_NODE_CRYPTO
  keys = m.keys.Snapshot();
#endif
  return {{"nm", m.nm.Snapshot()}, {"tsync", m.tsync.Snapshot()}, {"udsx", m.udsx.Snapshot()}, {"update", m.update.last},
          {"keys", keys}, {"apps", apps}, {"lastApp", m.lastApp}};
}

} // namespace apdash
