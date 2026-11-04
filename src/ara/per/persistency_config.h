/**
 * @file persistency_config.h
 * @brief Deployment configuration of the persistency storages.
 *
 * The real manifest (ARXML PersistencyDeployment) is out of scope; the same
 * information is read from a JSON file:
 * @code{.json}
 * {
 *   "storageRoot": "/var/lib/ap/persistency",
 *   "storages": [
 *     { "specifier": "App/KvsPort",  "type": "kvs",  "maxSize": 65536,
 *       "initialValues": { "volume": 5, "name": "default" } },
 *     { "specifier": "App/FilePort", "type": "file", "maxSize": 1048576,
 *       "maxFiles": 8, "readOnly": false }
 *   ]
 * }
 * @endcode
 */
#ifndef ARA_PER_PERSISTENCY_CONFIG_H_
#define ARA_PER_PERSISTENCY_CONFIG_H_

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "ara/core/instance_specifier.h"
#include "ara/core/result.h"

namespace ara::per {

struct StorageConfig {
  enum class Kind : std::uint8_t { kKeyValue, kFile };
  Kind kind{Kind::kKeyValue};
  std::string specifier;            ///< InstanceSpecifier string
  std::filesystem::path directory;  ///< <storageRoot>/<specifier with '/' -> '.'>
  bool readOnly{false};
  std::uint64_t maxSize{0};         ///< bytes; 0 = unlimited
  std::uint32_t maxFiles{0};        ///< file storage only; 0 = unlimited
  nlohmann::json initialValues = nlohmann::json::object(); ///< kvs only
};

class PersistencyConfig {
public:
  /// Parses a JSON document (see file comment).
  static ara::core::Result<PersistencyConfig> FromJson(std::string const &json);
  static ara::core::Result<PersistencyConfig> FromFile(std::filesystem::path const &file);

  ara::core::Result<StorageConfig>
  Find(ara::core::InstanceSpecifier const &specifier, StorageConfig::Kind kind) const;
  std::filesystem::path const &StorageRoot() const noexcept { return mRoot; }
  std::map<std::string, StorageConfig> const &Storages() const noexcept { return mStorages; }

private:
  std::filesystem::path mRoot;
  std::map<std::string, StorageConfig> mStorages;
};

} // namespace ara::per

#endif // ARA_PER_PERSISTENCY_CONFIG_H_
