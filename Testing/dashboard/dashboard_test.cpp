#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
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

} // namespace
