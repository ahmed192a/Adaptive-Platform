/**
 * @file platform_node.h
 * @brief "ap_dashboard": a runnable demo node that hosts the new clusters
 *        (ara::phm, ara::per, ara::diag, ara::crypto, ara::log) and drives the
 *        legacy Execution Management, exposing everything over a JSON API for
 *        the web dashboard.
 */
#ifndef AP_DASHBOARD_PLATFORM_NODE_H_
#define AP_DASHBOARD_PLATFORM_NODE_H_

#include <filesystem>
#include <memory>

#include "http_server.h"

namespace apdash {

struct NodeOptions {
  std::filesystem::path dataDir;      ///< writable state (persistency)
  std::filesystem::path platformDir;  ///< folder with Adaptive_Platform, Manifests/, processes/
  std::filesystem::path webRoot;      ///< static dashboard files
};

class PlatformNode {
public:
  explicit PlatformNode(NodeOptions opts);
  ~PlatformNode();
  PlatformNode(PlatformNode const &) = delete;
  PlatformNode &operator=(PlatformNode const &) = delete;

  /// Returns an empty string on success, otherwise the reason.
  std::string Init();
  HttpResponse Handle(HttpRequest const &req);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace apdash

#endif // AP_DASHBOARD_PLATFORM_NODE_H_
