#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <map>
#include <thread>

#include <nlohmann/json.hpp>

#include "platform_node.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace apdash;
using namespace std::chrono_literals;

namespace {

/// Blocking HTTP client over a raw socket: returns "<status>\n<body>".
std::pair<int, std::string> HttpCall(unsigned short port, std::string const &method, std::string const &target,
                                     std::string const &body = {}) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0) return {0, ""};
  std::string req = method + " " + target + " HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\n\r\n" + body;
  // the server may legitimately answer (and close) before a huge body is fully sent
  for (std::size_t off = 0; off < req.size();) {
    ssize_t w = ::send(fd, req.data() + off, req.size() - off, MSG_NOSIGNAL);
    if (w <= 0) break;
    off += static_cast<std::size_t>(w);
  }
  std::string resp;
  char buf[4096];
  ssize_t n;
  while ((n = ::recv(fd, buf, sizeof buf, 0)) > 0) resp.append(buf, static_cast<std::size_t>(n));
  ::close(fd);
  int status = std::stoi(resp.substr(9, 3));
  return {status, resp.substr(resp.find("\r\n\r\n") + 4)};
}

TEST(HttpServerTest, RoutesQueryBodyAndErrors) {
  HttpServer s([](HttpRequest const &r) {
    if (r.path == "/boom") throw std::runtime_error("kaput");
    return HttpResponse{200, "text/plain", r.method + "|" + r.path + "|" + r.query.at("a") + "|" + r.body};
  });
  ASSERT_TRUE(s.Start("127.0.0.1", 0));
  auto [st, body] = HttpCall(s.Port(), "POST", "/x%20y?a=1%2B1&b=2", "payload");
  EXPECT_EQ(st, 200);
  EXPECT_EQ(body, "POST|/x y|1+1|payload");
  EXPECT_EQ(HttpCall(s.Port(), "GET", "/boom?a=").first, 500);
  s.Stop();
  EXPECT_EQ(HttpCall(s.Port(), "GET", "/").first, 0); // closed
}

TEST(HttpServerTest, RejectsOversizedBodyAndGarbage) {
  HttpServer s([](HttpRequest const &) { return HttpResponse{200, "text/plain", "ok"}; });
  ASSERT_TRUE(s.Start("127.0.0.1", 0));
  EXPECT_EQ(HttpCall(s.Port(), "POST", "/", std::string(2 * 1024 * 1024, 'a')).first, 413);
  EXPECT_EQ(UrlDecode("a%2Fb+c%zz"), "a/b c%zz");
}

class NodeTest : public ::testing::Test {
protected:
  void SetUp() override {
    dir = fs::temp_directory_path() / ("apdash_ut_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
    fs::create_directories(dir / "web");
    std::ofstream(dir / "web" / "index.html") << "<html>hi</html>";
    NodeOptions o;
    o.dataDir = dir / "data";
    o.platformDir = dir / "platform"; // intentionally empty: EM start must fail gracefully
    o.webRoot = dir / "web";
    o.demoDir = dir / "demo";
    fs::create_directories(o.demoDir / "nm_bus_monitor");
    {
      auto exe = o.demoDir / "nm_bus_monitor" / "nm_bus_monitor";
      std::ofstream(exe) << "#!/bin/sh\necho hello-from-demo \"$@\"\necho on-stderr >&2\nexit 3\n";
      ::chmod(exe.c_str(), 0755);
    }
    node = std::make_unique<PlatformNode>(o);
    ASSERT_EQ(node->Init(), "");
  }
  void TearDown() override {
    node.reset();
    fs::remove_all(dir);
  }
  json Call(std::string const &path, json const &body = nullptr, std::string const &method = "POST") {
    HttpRequest r;
    r.method = method;
    r.path = path;
    if (!body.is_null()) r.body = body.dump();
    auto resp = node->Handle(r);
    last_status = resp.status;
    return json::parse(resp.body, nullptr, false);
  }
  template <typename Pred> bool WaitFor(Pred p, std::chrono::milliseconds limit = 8000ms) {
    auto end = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < end) {
      if (p()) return true;
      std::this_thread::sleep_for(50ms);
    }
    return false;
  }
  std::string WorkerState(std::string const &name) {
    auto ov = Call("/api/overview", nullptr, "GET");
    for (auto const &w : ov["phm"]["workers"])
      if (w["name"] == name) return w["global"];
    return "?";
  }
  static inline int counter = 0;
  fs::path dir;
  std::unique_ptr<PlatformNode> node;
  int last_status{0};
};

TEST_F(NodeTest, ServesStaticFilesSafely) {
  HttpRequest r;
  r.method = "GET";
  r.path = "/";
  EXPECT_EQ(node->Handle(r).body, "<html>hi</html>");
  r.path = "/../../etc/passwd";
  EXPECT_EQ(node->Handle(r).status, 403);
  r.path = "/missing.js";
  EXPECT_EQ(node->Handle(r).status, 404);
}

TEST_F(NodeTest, OverviewShowsHealthyPlatformAndBootCount) {
  auto o = Call("/api/overview", nullptr, "GET");
  EXPECT_EQ(o["per"]["bootCount"], 1);
  EXPECT_EQ(o["phm"]["workers"].size(), 3U);
  ASSERT_TRUE(WaitFor([&] { return WorkerState("Brake") == "OK"; }));
  EXPECT_FALSE(o["em"]["running"].get<bool>());
}

TEST_F(NodeTest, InjectedFaultExpiresThenRecoversWithRestart) {
  ASSERT_TRUE(WaitFor([&] { return WorkerState("Brake") == "OK"; }));
  EXPECT_TRUE(Call("/api/phm/hang", {{"name", "Brake"}})["ok"].get<bool>());
  ASSERT_TRUE(WaitFor([&] { return WorkerState("Brake") == "EXPIRED"; }));
  ASSERT_TRUE(WaitFor([&] { return WorkerState("Brake") == "OK"; }, 10000ms));
  auto o = Call("/api/overview", nullptr, "GET");
  EXPECT_EQ(o["phm"]["workers"][0]["restarts"], 1);
  EXPECT_EQ(WorkerState("Infotainment"), "OK"); // other function groups unaffected
  EXPECT_FALSE(o["phm"]["watchdogFired"].get<bool>());
}

TEST_F(NodeTest, WithoutRecoveryTheWatchdogFiresAndResetBringsPlatformBack) {
  ASSERT_TRUE(WaitFor([&] { return WorkerState("Telematics") == "OK"; }));
  Call("/api/phm/auto-recovery", {{"enabled", false}});
  Call("/api/phm/hang", {{"name", "Telematics"}});
  ASSERT_TRUE(WaitFor([&] { return Call("/api/overview", nullptr, "GET")["phm"]["watchdogFired"].get<bool>(); }, 10000ms));
  Call("/api/system/reset", json::object());
  auto o = Call("/api/overview", nullptr, "GET");
  EXPECT_FALSE(o["phm"]["watchdogFired"].get<bool>());
  EXPECT_EQ(o["per"]["bootCount"], 2);
  ASSERT_TRUE(WaitFor([&] { return WorkerState("Telematics") == "OK"; }));
}

TEST_F(NodeTest, UnknownWorkerAndBadBodies) {
  Call("/api/phm/hang", {{"name", "Nope"}});
  EXPECT_EQ(last_status, 404);
  HttpRequest r;
  r.method = "POST";
  r.path = "/api/diag";
  r.body = "not json";
  EXPECT_EQ(node->Handle(r).status, 400);
  Call("/api/does-not-exist", json::object());
  EXPECT_EQ(last_status, 404);
}

TEST_F(NodeTest, DiagnosticsSessionsAndPersistedWrite) {
  auto d = [&](char const *hex) { return Call("/api/diag", {{"request", hex}}); };
  EXPECT_EQ(d("22 F1 90")["response"], "62 F1 90 41 44 41 50 54 49 56 45 50 4C 41 54 30 30 30 30 31");
  EXPECT_EQ(d("2E F1 A2 42 6F 62")["response"], "7F 2E 31"); // needs extended session
  EXPECT_EQ(d("10 03")["response"], "50 03 00 32 01 F4");
  EXPECT_EQ(d("2E F1 A2 42 6F 62")["response"], "6E F1 A2");
  EXPECT_EQ(d("22 F1 A2")["response"], "62 F1 A2 42 6F 62");
  EXPECT_EQ(Call("/api/overview", nullptr, "GET")["per"]["vehicleName"], "Bob"); // same store as persistency
  EXPECT_TRUE(Call("/api/diag", {{"request", "zz"}})["ok"] == false);
}

TEST_F(NodeTest, UdsEcuResetRebootsThePlatform) {
  Call("/api/diag", {{"request", "11 01"}});
  ASSERT_TRUE(WaitFor([&] { return Call("/api/overview", nullptr, "GET")["per"]["bootCount"] == 2; }));
}

TEST_F(NodeTest, PersistencyApiRoundTripAndErrors) {
  EXPECT_TRUE(Call("/api/per/set", {{"key", "volume"}, {"value", 7}})["ok"].get<bool>());
  EXPECT_TRUE(Call("/api/per/set", {{"key", "muted"}, {"value", true}})["ok"].get<bool>());
  EXPECT_TRUE(Call("/api/per/set", {{"key", "ratio"}, {"value", 0.5}})["ok"].get<bool>());
  auto per = Call("/api/overview", nullptr, "GET")["per"];
  EXPECT_EQ(per["volume"], 7);
  EXPECT_EQ(per["muted"], true);
  EXPECT_DOUBLE_EQ(per["ratio"].get<double>(), 0.5);
  EXPECT_FALSE(Call("/api/per/set", {{"key", ""}, {"value", 1}})["ok"].get<bool>());
  EXPECT_TRUE(Call("/api/per/delete", {{"key", "volume"}})["ok"].get<bool>());
  EXPECT_FALSE(Call("/api/overview", nullptr, "GET")["per"].contains("volume"));
  EXPECT_FALSE(Call("/api/per/delete", {{"key", "volume"}})["ok"].get<bool>());
}

#ifdef AP_NODE_CRYPTO
TEST_F(NodeTest, CryptoEndpoints) {
  auto h = Call("/api/crypto/hash", {{"text", "abc"}});
  EXPECT_EQ(h["hex"], "BA 78 16 BF 8F 01 CF EA 41 41 40 DE 5D AE 22 23 B0 03 61 A3 96 17 7A 9C B4 10 FF 61 F2 00 15 AD");
  auto s = Call("/api/crypto/seal", {{"text", "secret"}, {"aad", "h"}});
  auto o = Call("/api/crypto/open", {{"hex", s["hex"]}, {"aad", "h"}});
  EXPECT_EQ(o["text"], "secret");
  EXPECT_TRUE(Call("/api/crypto/open", {{"hex", s["hex"]}, {"aad", "other"}})["authFailed"].get<bool>());
  for (char const *alg : {"ed25519", "ecdsa"}) {
    auto sig = Call("/api/crypto/sign", {{"text", "m"}, {"alg", alg}});
    EXPECT_TRUE(Call("/api/crypto/verify", {{"text", "m"}, {"sig", sig["hex"]}, {"alg", alg}})["valid"].get<bool>());
    EXPECT_FALSE(Call("/api/crypto/verify", {{"text", "m2"}, {"sig", sig["hex"]}, {"alg", alg}})["valid"].get<bool>());
  }
}
#endif

TEST_F(NodeTest, EmStartFailsGracefullyWithoutPlatformBinary) {
  auto r = Call("/api/em/start", json::object());
  EXPECT_FALSE(r["ok"].get<bool>());
  EXPECT_NE(r["error"].get<std::string>().find("not found"), std::string::npos);
  EXPECT_EQ(Call("/api/em/log", nullptr, "GET")["ok"], true); // log of a never-started EM is simply empty
  HttpRequest bad;
  bad.method = "GET";
  bad.path = "/api/em/log";
  bad.query["name"] = "../../etc/passwd";
  EXPECT_EQ(node->Handle(bad).status, 403);
}

// ------------------------------------------------------------------ dashboard v2: lab cards
TEST_F(NodeTest, OverviewCarriesTheLabSnapshot) {
  auto lab = Call("/api/overview", nullptr, "GET")["lab"];
  EXPECT_EQ(lab["nm"]["ecus"].size(), 3u);
  EXPECT_TRUE(lab["tsync"]["available"].get<bool>());
  EXPECT_EQ(lab["udsx"]["session"], 1);
  EXPECT_EQ(lab["apps"].size(), 6u);
  Call("/api/nm/reset", nullptr, "GET"); // a GET is a 405, never a command
  EXPECT_EQ(last_status, 405);
}

TEST_F(NodeTest, NetworkManagementLifecycleAndPartialNetworks) {
  auto states = [&] {
    std::map<std::string, std::string> m;
    auto ov = Call("/api/overview", nullptr, "GET");
    for (auto const &e : ov["lab"]["nm"]["ecus"]) m[e["name"].get<std::string>()] = e["state"].get<std::string>();
    return m;
  };
  EXPECT_EQ(states()["Gateway"], "BusSleep");
  EXPECT_TRUE(Call("/api/nm/request", {{"ecu", "Gateway"}})["ok"].get<bool>());
  EXPECT_EQ(states()["Gateway"], "RepeatMessage");
  Call("/api/nm/advance", {{"ms", 300}});
  EXPECT_EQ(states()["Door"], "BusSleep"); // PN filter: the PDU carries no partial network Door serves
  EXPECT_TRUE(Call("/api/nm/request", {{"ecu", "Gateway"}, {"pn", 0}})["ok"].get<bool>());
  Call("/api/nm/advance", {{"ms", 300}});
  EXPECT_EQ(states()["Door"], "RepeatMessage"); // woken: PN0 requested on the bus
  Call("/api/nm/release", {{"ecu", "Gateway"}, {"pn", 0}});
  Call("/api/nm/advance", {{"ms", 1000}});
  EXPECT_EQ(states()["Gateway"], "NormalOperation");
  EXPECT_GT(Call("/api/overview", nullptr, "GET")["lab"]["nm"]["pdus"].get<int>(), 0);
  // partial network request / release
  EXPECT_TRUE(Call("/api/nm/request", {{"ecu", "Door"}, {"pn", 0}})["ok"].get<bool>());
  bool pn0 = false;
  auto ovPn = Call("/api/overview", nullptr, "GET");
  for (auto const &e : ovPn["lab"]["nm"]["ecus"])
    if (e["name"] == "Door") pn0 = e["pn"][0].get<bool>();
  EXPECT_TRUE(pn0);
  // release everything; the bus must go to sleep again
  for (char const *n : {"Gateway", "Door", "Camera"}) {
    Call("/api/nm/release", {{"ecu", n}, {"pn", 0}});
    Call("/api/nm/release", {{"ecu", n}});
  }
  Call("/api/nm/advance", {{"ms", 6000}});
  EXPECT_EQ(states()["Gateway"], "BusSleep");
  EXPECT_FALSE(Call("/api/nm/request", {{"ecu", "Nope"}})["ok"].get<bool>());
  EXPECT_EQ(Call("/api/nm/bogus", json::object())["ok"], false);
  EXPECT_EQ(last_status, 404);
  Call("/api/nm/reset", json::object());
  EXPECT_TRUE(Call("/api/overview", nullptr, "GET")["lab"]["nm"]["events"].empty());
}

TEST_F(NodeTest, TimeSyncSlewStepTimeoutAndReset) {
  auto ts = [&] { return Call("/api/overview", nullptr, "GET")["lab"]["tsync"]; };
  EXPECT_EQ(ts()["status"], "NotSynchronized");
  EXPECT_TRUE(Call("/api/tsync/sync", {{"offsetMs", 0}})["ok"].get<bool>());
  EXPECT_EQ(ts()["status"], "Synchronized");
  // small offset: slewed (no leap); big offset: stepped (leap reported)
  Call("/api/tsync/advance", {{"ms", 100}});
  Call("/api/tsync/sync", {{"offsetMs", 20}});
  EXPECT_EQ(ts()["leap"], "none");
  Call("/api/tsync/advance", {{"ms", 100}});
  Call("/api/tsync/sync", {{"offsetMs", 2000}});
  EXPECT_EQ(ts()["leap"], "future");
  EXPECT_NEAR(ts()["offsetMs"].get<double>(), 0.0, 5.0);
  // no further update -> timeout
  Call("/api/tsync/advance", {{"ms", 5000}});
  EXPECT_EQ(ts()["status"], "TimeOut");
  EXPECT_FALSE(ts()["events"].empty());
  Call("/api/tsync/reset", json::object());
  EXPECT_EQ(ts()["status"], "NotSynchronized");
  EXPECT_EQ(Call("/api/tsync/bogus", json::object())["ok"], false);
  EXPECT_EQ(last_status, 404);
}

TEST_F(NodeTest, ExtendedDiagnosticsFaultsSecurityAndClear) {
  auto uds = [&](char const *hex) {
    auto r = Call("/api/udsx/request", {{"request", hex}});
    EXPECT_TRUE(r["ok"].get<bool>());
    auto tr = Call("/api/overview", nullptr, "GET")["lab"]["udsx"]["trace"];
    return tr.back();
  };
  EXPECT_EQ(uds("22 F1 90")["resp"].get<std::string>().substr(0, 8), "62 F1 90");
  EXPECT_TRUE(uds("31 01 FF 00")["neg"].get<bool>()); // needs extended session + security
  EXPECT_FALSE(uds("10 03")["neg"].get<bool>());
  EXPECT_TRUE(Call("/api/udsx/unlock", json::object())["ok"].get<bool>());
  EXPECT_FALSE(uds("31 01 FF 00")["neg"].get<bool>());
  Call("/api/udsx/fault", {{"dtc", 0xC10100}, {"active", true}});
  auto dtc = uds("19 02 FF");
  EXPECT_FALSE(dtc["neg"].get<bool>());
  EXPECT_NE(dtc["resp"].get<std::string>().find("C1 01 00"), std::string::npos);
  EXPECT_FALSE(uds("14 FF FF FF")["neg"].get<bool>());
  EXPECT_EQ(uds("19 02 FF")["resp"].get<std::string>().find("C1 01 00"), std::string::npos);
  EXPECT_EQ(Call("/api/udsx/request", {{"request", "zz"}})["ok"], false);
  EXPECT_EQ(Call("/api/udsx/advance", {{"ms", 10000}})["ok"], true); // S3 timeout drops the session
  EXPECT_EQ(Call("/api/overview", nullptr, "GET")["lab"]["udsx"]["session"], 1);
  Call("/api/udsx/reset", json::object());
  EXPECT_TRUE(Call("/api/overview", nullptr, "GET")["lab"]["udsx"]["trace"].empty());
}

TEST_F(NodeTest, SoftwareUpdateOutcomes) {
  auto run = [&](json body) { return Call("/api/update/run", body)["report"]; };
  EXPECT_EQ(run({{"size", 30000}})["outcome"], "updated");
  EXPECT_GT(run({{"size", 30000}})["blocks"].get<int>(), 1);
  EXPECT_EQ(run({{"tamper", true}})["outcome"], "package-rejected");
  EXPECT_EQ(run({{"failVerify", true}})["outcome"], "rolled-back");
  EXPECT_EQ(run({{"rejectSession", true}})["outcome"], "session-rejected");
  EXPECT_EQ(Call("/api/overview", nullptr, "GET")["lab"]["update"]["outcome"], "session-rejected");
  EXPECT_EQ(run({{"size", 99999999999.0}})["size"], 1 << 20); // clamped
  EXPECT_EQ(Call("/api/update/other", json::object())["ok"], false);
}

#ifdef AP_NODE_CRYPTO
TEST_F(NodeTest, KeySlotsGenerateUseTamperAndPersist) {
  auto slot = [&](std::string const &name) {
    auto ov = Call("/api/overview", nullptr, "GET");
    for (auto const &s : ov["lab"]["keys"]["slots"])
      if (s["name"] == name) return s;
    return json(nullptr);
  };
  ASSERT_TRUE(Call("/api/overview", nullptr, "GET")["lab"]["keys"]["enabled"].get<bool>());
  EXPECT_EQ(slot("vault-aes")["state"], "empty");
  EXPECT_FALSE(Call("/api/keys/seal", {{"name", "vault-aes"}, {"text", "x"}})["ok"].get<bool>());
  EXPECT_TRUE(Call("/api/keys/generate", {{"name", "vault-aes"}})["ok"].get<bool>());
  EXPECT_EQ(slot("vault-aes")["state"], "occupied");
  auto sealed = Call("/api/keys/seal", {{"name", "vault-aes"}, {"text", "hello"}, {"aad", "a"}});
  ASSERT_TRUE(sealed["ok"].get<bool>());
  EXPECT_EQ(Call("/api/keys/open", {{"name", "vault-aes"}, {"hex", sealed["hex"]}, {"aad", "a"}})["text"], "hello");
  EXPECT_TRUE(Call("/api/keys/open", {{"name", "vault-aes"}, {"hex", sealed["hex"]}, {"aad", "b"}})["authFailed"].get<bool>());
  // survives a reload from storage
  EXPECT_TRUE(Call("/api/keys/reload", json::object())["ok"].get<bool>());
  EXPECT_EQ(slot("vault-aes")["state"], "occupied");
  // MAC and signature slots
  Call("/api/keys/generate", {{"name", "vault-hmac"}});
  EXPECT_TRUE(Call("/api/keys/mac", {{"name", "vault-hmac"}, {"text", "m"}})["ok"].get<bool>());
  Call("/api/keys/generate", {{"name", "vault-ed25519"}});
  auto sig = Call("/api/keys/sign", {{"name", "vault-ed25519"}, {"text", "m"}});
  EXPECT_TRUE(Call("/api/keys/verify", {{"name", "vault-ed25519"}, {"text", "m"}, {"hex", sig["hex"]}})["valid"].get<bool>());
  EXPECT_FALSE(Call("/api/keys/verify", {{"name", "vault-ed25519"}, {"text", "n"}, {"hex", sig["hex"]}})["valid"].get<bool>());
  // wrong usage is refused; non-exportable key cannot leave
  EXPECT_FALSE(Call("/api/keys/mac", {{"name", "vault-aes"}, {"text", "m"}})["ok"].get<bool>());
  EXPECT_FALSE(Call("/api/keys/export", {{"name", "vault-aes"}})["ok"].get<bool>());
  // storage tampering is detected, regenerating repairs
  EXPECT_TRUE(Call("/api/keys/tamper", {{"name", "vault-aes"}})["ok"].get<bool>());
  EXPECT_EQ(slot("vault-aes")["state"], "corrupted");
  EXPECT_FALSE(Call("/api/keys/seal", {{"name", "vault-aes"}, {"text", "x"}})["ok"].get<bool>());
  EXPECT_TRUE(Call("/api/keys/generate", {{"name", "vault-aes"}})["ok"].get<bool>());
  EXPECT_EQ(slot("vault-aes")["state"], "occupied");
  EXPECT_TRUE(Call("/api/keys/clear", {{"name", "vault-aes"}})["ok"].get<bool>());
  EXPECT_EQ(slot("vault-aes")["state"], "empty");
  EXPECT_FALSE(Call("/api/keys/tamper", {{"name", "session-aes"}})["ok"].get<bool>()); // volatile slot
  EXPECT_FALSE(Call("/api/keys/generate", {{"name", "nope"}})["ok"].get<bool>());
}
#endif

TEST_F(NodeTest, DemoAppLauncherIsWhitelistedAndCapturesOutput) {
  auto r = Call("/api/apps/run", {{"name", "nm_bus_monitor"}, {"mode", "run"}});
  EXPECT_TRUE(r["ok"].get<bool>());
  EXPECT_EQ(r["exit"], 3);
  EXPECT_NE(r["output"].get<std::string>().find("hello-from-demo"), std::string::npos);
  EXPECT_NE(r["output"].get<std::string>().find("on-stderr"), std::string::npos);
  EXPECT_FALSE(r["timedOut"].get<bool>());
  EXPECT_EQ(Call("/api/overview", nullptr, "GET")["lab"]["lastApp"]["name"], "nm_bus_monitor");
  // not whitelisted / path tricks / unknown mode / unbuilt binary
  EXPECT_EQ(Call("/api/apps/run", {{"name", "../../bin/sh"}, {"mode", "run"}})["ok"], false);
  EXPECT_EQ(last_status, 404);
  EXPECT_EQ(Call("/api/apps/run", {{"name", "nm_bus_monitor"}, {"mode", "--evil"}})["ok"], false);
  EXPECT_EQ(last_status, 404);
  auto missing = Call("/api/apps/run", {{"name", "uds_tester"}, {"mode", "run"}});
  EXPECT_FALSE(missing["ok"].get<bool>());
  EXPECT_NE(missing["error"].get<std::string>().find("not built"), std::string::npos);
  EXPECT_EQ(Call("/api/apps/other", json::object())["ok"], false);
}

} // namespace
