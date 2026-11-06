/**
 * ap_dashboard - run the Adaptive Platform demo node and its web dashboard.
 *
 *   ap_dashboard [--port 8080] [--bind 127.0.0.1] [--data-dir DIR]
 *                [--platform-dir DIR] [--web-root DIR] [--demo-dir DIR]
 */
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

#include "platform_node.h"

namespace {
volatile std::sig_atomic_t gStop = 0;
void OnSignal(int) { gStop = 1; }
} // namespace

int main(int argc, char **argv) {
  namespace fs = std::filesystem;
  apdash::NodeOptions opt;
  unsigned short port = 8080;
  std::string bind = "127.0.0.1";
  fs::path exeDir = fs::weakly_canonical(fs::path(argv[0])).parent_path();
  opt.webRoot = exeDir / "web";
  opt.platformDir = exeDir.parent_path();
  opt.dataDir = fs::temp_directory_path() / "ap_dashboard_data";

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--port") port = static_cast<unsigned short>(std::atoi(next().c_str()));
    else if (a == "--bind") bind = next();
    else if (a == "--data-dir") opt.dataDir = next();
    else if (a == "--platform-dir") opt.platformDir = next();
    else if (a == "--web-root") opt.webRoot = next();
    else if (a == "--demo-dir") opt.demoDir = next();
    else {
      std::cerr << "usage: ap_dashboard [--port N] [--bind ADDR] [--data-dir D] [--platform-dir D] [--web-root D] [--demo-dir D]\n";
      return a == "--help" ? 0 : 2;
    }
  }

  apdash::PlatformNode node(opt);
  if (auto err = node.Init(); !err.empty()) {
    std::cerr << "init failed: " << err << "\n";
    return 1;
  }
  apdash::HttpServer server([&](apdash::HttpRequest const &r) { return node.Handle(r); });
  if (!server.Start(bind, port)) {
    std::cerr << "cannot listen on " << bind << ":" << port << "\n";
    return 1;
  }
  std::signal(SIGINT, OnSignal);
  std::signal(SIGTERM, OnSignal);
  std::cout << "Adaptive Platform dashboard: http://" << bind << ":" << server.Port() << "/" << std::endl;
  while (gStop == 0) ::usleep(100 * 1000);
  std::cout << "shutting down" << std::endl;
  server.Stop();
  return 0;
}
