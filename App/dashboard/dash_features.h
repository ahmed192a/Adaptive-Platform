/**
 * @file features.h
 * @brief Dashboard v2 feature lab: virtual NM bus, time-sync, extended diagnostics, software update,
 *        key slots and the demo-app launcher. Every feature drives the real in-process libraries.
 *
 * All state is simulated with manual clocks, so every endpoint is deterministic and unit-testable.
 * Routes handled (all under /api/): nm, tsync, udsx, update, keys and apps (POST, one sub-path per operation).
 */
#ifndef AP_DASHBOARD_FEATURES_H_
#define AP_DASHBOARD_FEATURES_H_

#include <filesystem>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "ara/per/key_value_storage.h"
#include "http_server.h"

namespace apdash {

class Features {
public:
  struct Options {
    std::filesystem::path dataDir;  ///< scratch space for the demo apps and the vault key
    std::filesystem::path demoDir;  ///< folder holding <name>/<name> demo binaries
    /// Storage that backs the persistent key slots (may be null: the key card is then disabled).
    ara::per::SharedHandle<ara::per::KeyValueStorage> keyStorage;
  };

  explicit Features(Options opts);
  ~Features();
  Features(Features const &) = delete;
  Features &operator=(Features const &) = delete;

  /// True when @p path belongs to a feature route (so the caller does not need to know the list).
  static bool Owns(std::string const &path);
  /// Handles one request; only call it when Owns() is true.
  HttpResponse Handle(std::string const &path, nlohmann::json const &in);
  /// State of every feature, merged into /api/overview under the key "lab".
  nlohmann::json Snapshot();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace apdash

#endif // AP_DASHBOARD_FEATURES_H_
