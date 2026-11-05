#include "ara/per/persistency.h"

#include <cstdlib>
#include <map>
#include <mutex>
#include <optional>

#include "ara/core/initialization.h"
#include "ara/per/fs_util.h"
#include <vector>

namespace ara::per {

// Implemented in key_value_storage.cpp / file_storage.cpp
ara::core::Result<SharedHandle<KeyValueStorage>> OpenKeyValueStorageImpl(StorageConfig cfg);
ara::core::Result<SharedHandle<FileStorage>> OpenFileStorageImpl(StorageConfig cfg);

using ara::core::InstanceSpecifier;
using ara::core::Result;

namespace {
namespace fs = std::filesystem;

struct Registry {
  std::mutex mutex;
  std::optional<PersistencyConfig> config;
  std::map<std::string, std::weak_ptr<KeyValueStorage>> kvs;
  std::map<std::string, std::weak_ptr<FileStorage>> files;
  std::mutex cbMutex;
  RecoveryReportCallback recoveryCb;
  ApplicationDataUpdateCallback appDataCb;

  bool AnyOpen() {
    for (auto &e : kvs) {
      if (!e.second.expired()) return true;
    }
    for (auto &e : files) {
      if (!e.second.expired()) return true;
    }
    return false;
  }
};

Registry &Reg() {
  static Registry r;
  return r;
}

/// Looks up the config; loads $ARA_PER_CONFIG lazily.  Caller holds the lock.
Result<StorageConfig> Lookup(Registry &r, InstanceSpecifier const &spec, StorageConfig::Kind kind) {
  if (!r.config.has_value()) {
    char const *env = std::getenv("ARA_PER_CONFIG");
    if (env == nullptr) {
      return Result<StorageConfig>(MakeErrorCode(PerErrc::kNotInitialized));
    }
    auto cfg = PersistencyConfig::FromFile(env);
    if (!cfg.HasValue()) {
      return Result<StorageConfig>(cfg.Error());
    }
    r.config = std::move(cfg).Value();
  }
  return r.config->Find(spec, kind);
}

std::string Key(InstanceSpecifier const &spec) { return std::string(spec.ToString()); }

/// Invokes the recovery callback outside of any registry lock.
void Report(InstanceSpecifier const &spec, RecoveryReportKind kind, std::string element) noexcept {
  RecoveryReportCallback cb;
  {
    std::lock_guard<std::mutex> lock(Reg().cbMutex);
    cb = Reg().recoveryCb;
  }
  if (!cb) return;
  try {
    ara::core::Vector<ara::core::String> elements;
    elements.push_back(std::move(element));
    cb(spec, kind, std::move(elements), {});
  } catch (...) { // user callbacks must not break persistency
  }
}
} // namespace

void RegisterRecoveryReportCallback(RecoveryReportCallback cb) noexcept {
  std::lock_guard<std::mutex> lock(Reg().cbMutex);
  Reg().recoveryCb = std::move(cb);
}

void RegisterApplicationDataUpdateCallback(ApplicationDataUpdateCallback cb) noexcept {
  std::lock_guard<std::mutex> lock(Reg().cbMutex);
  Reg().appDataCb = std::move(cb);
}

Result<void> UpdatePersistency() noexcept {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  if (!r.config.has_value()) {
    return Result<void>(MakeErrorCode(PerErrc::kNotInitialized));
  }
  if (r.AnyOpen()) {
    return Result<void>(MakeErrorCode(PerErrc::kResourceBusy));
  }
  return Result<void>();
}

Result<void> Configure(PersistencyConfig config) {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  if (r.AnyOpen()) {
    return Result<void>(MakeErrorCode(PerErrc::kResourceBusy));
  }
  r.kvs.clear();
  r.files.clear();
  r.config = std::move(config);
  return Result<void>();
}

Result<void> ConfigureFromFile(fs::path const &file) {
  auto cfg = PersistencyConfig::FromFile(file);
  if (!cfg.HasValue()) {
    return Result<void>(cfg.Error());
  }
  return Configure(std::move(cfg).Value());
}

Result<void> ConfigureFromEnvironment() {
  char const *env = std::getenv("ARA_PER_CONFIG");
  if (env == nullptr) {
    return Result<void>(MakeErrorCode(PerErrc::kNotInitialized));
  }
  return ConfigureFromFile(env);
}

Result<SharedHandle<KeyValueStorage>> OpenKeyValueStorage(InstanceSpecifier const &spec) noexcept {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  auto cfg = Lookup(r, spec, StorageConfig::Kind::kKeyValue);
  if (!cfg.HasValue()) {
    return Result<SharedHandle<KeyValueStorage>>(cfg.Error());
  }
  if (auto existing = r.kvs[Key(spec)].lock()) {
    return Result<SharedHandle<KeyValueStorage>>(std::move(existing));
  }
  auto opened = OpenKeyValueStorageImpl(std::move(cfg).Value());
  if (opened.HasValue()) {
    r.kvs[Key(spec)] = opened.Value();
  }
  return opened;
}

Result<SharedHandle<FileStorage>> OpenFileStorage(InstanceSpecifier const &spec) noexcept {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  auto cfg = Lookup(r, spec, StorageConfig::Kind::kFile);
  if (!cfg.HasValue()) {
    return Result<SharedHandle<FileStorage>>(cfg.Error());
  }
  if (auto existing = r.files[Key(spec)].lock()) {
    return Result<SharedHandle<FileStorage>>(std::move(existing));
  }
  auto opened = OpenFileStorageImpl(std::move(cfg).Value());
  if (opened.HasValue()) {
    r.files[Key(spec)] = opened.Value();
  }
  return opened;
}

Result<void> RecoverKeyValueStorage(InstanceSpecifier const &spec) noexcept {
  auto &r = Reg();
  std::unique_lock<std::mutex> lock(r.mutex);
  auto cfg = Lookup(r, spec, StorageConfig::Kind::kKeyValue);
  if (!cfg.HasValue()) {
    return Result<void>(cfg.Error());
  }
  if (!r.kvs[Key(spec)].expired()) {
    return Result<void>(MakeErrorCode(PerErrc::kResourceBusy));
  }
  auto primary = cfg.Value().directory / "kvs.json";
  auto backup = primary;
  backup += ".bak";
  // A backup is usable only if it passes the same integrity check as a normal load.
  StorageConfig probe = cfg.Value();
  probe.directory = cfg.Value().directory;
  std::error_code ec;
  if (!fs::exists(backup, ec)) {
    return Result<void>(MakeErrorCode(PerErrc::kValidationFailed));
  }
  fs::copy_file(backup, primary, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    return Result<void>(detail::ErrnoToError(ec.value()));
  }
  auto opened = OpenKeyValueStorageImpl(std::move(probe));
  if (!opened.HasValue()) {
    return Result<void>(MakeErrorCode(PerErrc::kValidationFailed));
  }
  lock.unlock();
  Report(spec, RecoveryReportKind::kRecovered, "kvs.json");
  return Result<void>();
}

Result<void> ResetKeyValueStorage(InstanceSpecifier const &spec) noexcept {
  auto &r = Reg();
  std::unique_lock<std::mutex> lock(r.mutex);
  auto cfg = Lookup(r, spec, StorageConfig::Kind::kKeyValue);
  if (!cfg.HasValue()) {
    return Result<void>(cfg.Error());
  }
  if (!r.kvs[Key(spec)].expired()) {
    return Result<void>(MakeErrorCode(PerErrc::kResourceBusy));
  }
  if (cfg.Value().readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::error_code ec;
  auto primary = cfg.Value().directory / "kvs.json";
  auto backup = primary;
  backup += ".bak";
  fs::remove(primary, ec);
  fs::remove(backup, ec);
  lock.unlock();
  Report(spec, RecoveryReportKind::kReset, "kvs.json");
  return Result<void>();
}

Result<void> RecoverAllFiles(InstanceSpecifier const &spec) noexcept {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  auto cfg = Lookup(r, spec, StorageConfig::Kind::kFile);
  if (!cfg.HasValue()) {
    return Result<void>(cfg.Error());
  }
  if (!r.files[Key(spec)].expired()) {
    return Result<void>(MakeErrorCode(PerErrc::kResourceBusy));
  }
  return Result<void>(MakeErrorCode(PerErrc::kValidationFailed, 0, "no redundancy configured"));
}

Result<void> ResetAllFiles(InstanceSpecifier const &spec) noexcept {
  auto &r = Reg();
  std::unique_lock<std::mutex> lock(r.mutex);
  auto cfg = Lookup(r, spec, StorageConfig::Kind::kFile);
  if (!cfg.HasValue()) {
    return Result<void>(cfg.Error());
  }
  if (!r.files[Key(spec)].expired()) {
    return Result<void>(MakeErrorCode(PerErrc::kResourceBusy));
  }
  if (cfg.Value().readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::error_code ec;
  auto const files = cfg.Value().directory / "files";
  auto const initial = cfg.Value().directory / "initial";
  fs::remove_all(files, ec);
  fs::create_directories(files, ec);
  if (fs::is_directory(initial, ec)) {
    fs::copy(initial, files, fs::copy_options::recursive, ec);
    if (ec) {
      return Result<void>(detail::ErrnoToError(ec.value()));
    }
  }
  lock.unlock();
  Report(spec, RecoveryReportKind::kReset, "files");
  return Result<void>();
}

Result<void> ResetPersistency() noexcept {
  std::vector<InstanceSpecifier> kvsSpecs;
  std::vector<InstanceSpecifier> fileSpecs;
  {
    auto &r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (!r.config.has_value()) {
      return Result<void>(MakeErrorCode(PerErrc::kNotInitialized));
    }
    for (auto const &e : r.config->Storages()) {
      InstanceSpecifier s(e.second.specifier);
      (e.second.kind == StorageConfig::Kind::kKeyValue ? kvsSpecs : fileSpecs).push_back(s);
    }
  }
  for (auto const &s : kvsSpecs) {
    auto res = ResetKeyValueStorage(s);
    if (!res.HasValue()) {
      return res;
    }
  }
  for (auto const &s : fileSpecs) {
    auto res = ResetAllFiles(s);
    if (!res.HasValue()) {
      return res;
    }
  }
  return Result<void>();
}

Result<std::uint64_t> GetCurrentKeyValueStorageSize(InstanceSpecifier const &spec) noexcept {
  auto kvs = OpenKeyValueStorage(spec);
  if (!kvs.HasValue()) {
    return Result<std::uint64_t>(kvs.Error());
  }
  return kvs.Value()->GetCurrentStorageSize();
}

Result<std::uint64_t> GetCurrentFileStorageSize(InstanceSpecifier const &spec) noexcept {
  auto st = OpenFileStorage(spec);
  if (!st.HasValue()) {
    return Result<std::uint64_t>(st.Error());
  }
  return st.Value()->GetCurrentStorageSize();
}

// ---- participation in ara::core::Initialize() ------------------------------
namespace {
Result<void> InitPer() {
  if (std::getenv("ARA_PER_CONFIG") == nullptr) {
    return Result<void>(); // configuration may be supplied later through Configure()
  }
  return ConfigureFromEnvironment();
}
Result<void> DeinitPer() {
  auto &r = Reg();
  std::lock_guard<std::mutex> lock(r.mutex);
  r.kvs.clear();
  r.files.clear();
  r.config.reset();
  return Result<void>();
}
} // namespace
ARA_REGISTER_LIFECYCLE_HOOK("ara::per", &InitPer, &DeinitPer)

} // namespace ara::per
