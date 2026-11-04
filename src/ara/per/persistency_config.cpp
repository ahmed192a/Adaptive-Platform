#include "ara/per/persistency_config.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "ara/per/per_error_domain.h"

namespace ara::per {

using ara::core::Result;

Result<PersistencyConfig> PersistencyConfig::FromJson(std::string const &json) {
  nlohmann::json doc = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object() || !doc.contains("storageRoot") ||
      !doc["storageRoot"].is_string()) {
    return Result<PersistencyConfig>(MakeErrorCode(PerErrc::kNotInitialized, 0,
                                                   "persistency config is not valid JSON"));
  }
  PersistencyConfig cfg;
  cfg.mRoot = doc["storageRoot"].get<std::string>();
  if (!doc.contains("storages")) {
    return Result<PersistencyConfig>(std::move(cfg));
  }
  if (!doc["storages"].is_array()) {
    return Result<PersistencyConfig>(MakeErrorCode(PerErrc::kNotInitialized, 0,
                                                   "'storages' must be an array"));
  }
  for (auto const &s : doc["storages"]) {
    if (!s.is_object() || !s.contains("specifier") || !s["specifier"].is_string() ||
        !s.contains("type") || !s["type"].is_string()) {
      return Result<PersistencyConfig>(MakeErrorCode(PerErrc::kNotInitialized, 0,
                                                     "storage needs 'specifier' and 'type'"));
    }
    StorageConfig sc;
    sc.specifier = s["specifier"].get<std::string>();
    if (!ara::core::InstanceSpecifier::Create(sc.specifier).HasValue()) {
      return Result<PersistencyConfig>(MakeErrorCode(PerErrc::kNotInitialized, 0,
                                                     "invalid InstanceSpecifier in config"));
    }
    auto const type = s["type"].get<std::string>();
    if (type == "kvs") {
      sc.kind = StorageConfig::Kind::kKeyValue;
    } else if (type == "file") {
      sc.kind = StorageConfig::Kind::kFile;
    } else {
      return Result<PersistencyConfig>(MakeErrorCode(PerErrc::kNotInitialized, 0,
                                                     "storage type must be 'kvs' or 'file'"));
    }
    sc.readOnly = s.value("readOnly", false);
    sc.maxSize = s.value("maxSize", std::uint64_t{0});
    sc.maxFiles = s.value("maxFiles", std::uint32_t{0});
    if (s.contains("initialValues") && s["initialValues"].is_object()) {
      sc.initialValues = s["initialValues"];
    }
    std::string dirName = sc.specifier;
    std::replace(dirName.begin(), dirName.end(), '/', '.');
    sc.directory = cfg.mRoot / dirName;
    // The same specifier may legitimately name one kvs and one file storage.
    cfg.mStorages[(sc.kind == StorageConfig::Kind::kKeyValue ? "kvs:" : "file:") + sc.specifier] = sc;
  }
  return Result<PersistencyConfig>(std::move(cfg));
}

Result<PersistencyConfig> PersistencyConfig::FromFile(std::filesystem::path const &file) {
  std::ifstream in(file);
  if (!in) {
    return Result<PersistencyConfig>(MakeErrorCode(PerErrc::kNotInitialized, 0,
                                                   "cannot open persistency config"));
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return FromJson(ss.str());
}

Result<StorageConfig> PersistencyConfig::Find(ara::core::InstanceSpecifier const &specifier,
                                              StorageConfig::Kind kind) const {
  auto const key = (kind == StorageConfig::Kind::kKeyValue ? "kvs:" : "file:") +
                   std::string(specifier.ToString());
  auto it = mStorages.find(key);
  if (it == mStorages.end()) {
    return Result<StorageConfig>(MakeErrorCode(PerErrc::kStorageNotFound));
  }
  return Result<StorageConfig>(it->second);
}

} // namespace ara::per
