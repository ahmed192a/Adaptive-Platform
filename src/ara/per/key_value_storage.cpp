#include "ara/per/key_value_storage.h"

#include <filesystem>

#include "ara/per/crc32.h"
#include "ara/per/fs_util.h"

namespace ara::per {

using ara::core::Result;
using ara::core::StringView;

namespace {
constexpr char const *kFileName = "kvs.json";

std::filesystem::path Primary(StorageConfig const &c) { return c.directory / kFileName; }
std::filesystem::path Backup(StorageConfig const &c) {
  auto p = Primary(c);
  p += ".bak";
  return p;
}

std::string Serialize(nlohmann::json const &data) {
  std::string const payload = data.dump();
  nlohmann::json envelope;
  envelope["version"] = 1;
  envelope["crc32"] = detail::Crc32(payload);
  envelope["data"] = data;
  return envelope.dump(2);
}

/// Returns the validated data object, or kIntegrityCorrupted.
Result<nlohmann::json> Parse(std::string const &text) {
  nlohmann::json env = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (env.is_discarded() || !env.is_object() || !env.contains("data") || !env["data"].is_object() ||
      !env.contains("crc32") || !env["crc32"].is_number_unsigned()) {
    return Result<nlohmann::json>(MakeErrorCode(PerErrc::kIntegrityCorrupted));
  }
  auto const expected = env["crc32"].get<std::uint32_t>();
  if (detail::Crc32(env["data"].dump()) != expected) {
    return Result<nlohmann::json>(MakeErrorCode(PerErrc::kIntegrityCorrupted));
  }
  return Result<nlohmann::json>(env["data"]);
}

Result<nlohmann::json> LoadFile(std::filesystem::path const &file) {
  auto text = detail::ReadWholeFile(file);
  if (!text.HasValue()) {
    return Result<nlohmann::json>(text.Error());
  }
  return Parse(text.Value());
}
} // namespace

struct KvsAccess {
  static Result<SharedHandle<KeyValueStorage>> Open(StorageConfig cfg) {
    return KeyValueStorage::Open(std::move(cfg));
  }
};

Result<SharedHandle<KeyValueStorage>> OpenKeyValueStorageImpl(StorageConfig cfg) {
  return KvsAccess::Open(std::move(cfg));
}

Result<SharedHandle<KeyValueStorage>> KeyValueStorage::Open(StorageConfig cfg) {
  SharedHandle<KeyValueStorage> kvs(new KeyValueStorage(std::move(cfg)));
  auto loaded = kvs->Load();
  if (!loaded.HasValue()) {
    return Result<SharedHandle<KeyValueStorage>>(loaded.Error());
  }
  return Result<SharedHandle<KeyValueStorage>>(std::move(kvs));
}

Result<void> KeyValueStorage::Load() {
  std::lock_guard<std::mutex> lock(mMutex);
  auto primary = LoadFile(Primary(mConfig));
  if (primary.HasValue()) {
    mData = mCommitted = primary.Value();
    return Result<void>();
  }
  auto backup = LoadFile(Backup(mConfig));
  if (backup.HasValue()) { // primary missing (crash between renames) or corrupted
    mData = mCommitted = backup.Value();
    return Result<void>();
  }
  bool const primaryMissing = primary.Error().Value() == static_cast<ara::core::ErrorDomain::CodeType>(
                                                             PerErrc::kFileNotFound);
  bool const backupMissing = backup.Error().Value() == static_cast<ara::core::ErrorDomain::CodeType>(
                                                           PerErrc::kFileNotFound);
  if (primaryMissing && backupMissing) { // brand-new storage
    mData = mCommitted = mConfig.initialValues;
    return Result<void>();
  }
  return Result<void>(MakeErrorCode(PerErrc::kIntegrityCorrupted));
}

Result<ara::core::Vector<ara::core::String>> KeyValueStorage::GetAllKeys() const noexcept {
  std::lock_guard<std::mutex> lock(mMutex);
  ara::core::Vector<ara::core::String> keys;
  for (auto it = mData.begin(); it != mData.end(); ++it) {
    keys.push_back(it.key());
  }
  return Result<ara::core::Vector<ara::core::String>>(std::move(keys));
}

Result<bool> KeyValueStorage::KeyExists(StringView key) const noexcept {
  std::lock_guard<std::mutex> lock(mMutex);
  return Result<bool>(mData.contains(std::string(key)));
}

Result<nlohmann::json> KeyValueStorage::GetJson(StringView key) const noexcept {
  std::lock_guard<std::mutex> lock(mMutex);
  auto it = mData.find(std::string(key));
  if (it == mData.end()) {
    return Result<nlohmann::json>(MakeErrorCode(PerErrc::kKeyNotFound));
  }
  return Result<nlohmann::json>(*it);
}

Result<void> KeyValueStorage::SetJson(StringView key, nlohmann::json value) noexcept {
  if (key.empty()) {
    return Result<void>(MakeErrorCode(PerErrc::kUnsupported, 0, "empty key"));
  }
  if (mConfig.readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::lock_guard<std::mutex> lock(mMutex);
  nlohmann::json next = mData;
  next[std::string(key)] = std::move(value);
  if (mConfig.maxSize != 0 && next.dump().size() > mConfig.maxSize) {
    return Result<void>(MakeErrorCode(PerErrc::kQuotaExceeded));
  }
  mData = std::move(next);
  return Result<void>();
}

Result<void> KeyValueStorage::RemoveKey(StringView key) noexcept {
  if (mConfig.readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::lock_guard<std::mutex> lock(mMutex);
  if (mData.erase(std::string(key)) == 0) {
    return Result<void>(MakeErrorCode(PerErrc::kKeyNotFound));
  }
  return Result<void>();
}

Result<void> KeyValueStorage::RemoveAllKeys() noexcept {
  if (mConfig.readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::lock_guard<std::mutex> lock(mMutex);
  mData = nlohmann::json::object();
  return Result<void>();
}

Result<void> KeyValueStorage::SyncToStorage() noexcept {
  if (mConfig.readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::lock_guard<std::mutex> lock(mMutex);
  if (mData == mCommitted && std::filesystem::exists(Primary(mConfig))) {
    return Result<void>(); // nothing to do
  }
  auto written = detail::WriteFileAtomic(Primary(mConfig), Serialize(mData), /*keepBackup=*/true);
  if (!written.HasValue()) {
    return written;
  }
  mCommitted = mData;
  return Result<void>();
}

Result<void> KeyValueStorage::DiscardPendingChanges() noexcept {
  std::lock_guard<std::mutex> lock(mMutex);
  mData = mCommitted;
  return Result<void>();
}

Result<void> KeyValueStorage::ResetKey(StringView key) noexcept {
  if (mConfig.readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::lock_guard<std::mutex> lock(mMutex);
  auto it = mConfig.initialValues.find(std::string(key));
  if (it == mConfig.initialValues.end()) {
    return Result<void>(MakeErrorCode(PerErrc::kInitValueNotAvailable));
  }
  mData[std::string(key)] = *it;
  return Result<void>();
}

Result<void> KeyValueStorage::RecoverKey(StringView key) noexcept {
  if (mConfig.readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  auto backup = LoadFile(Backup(mConfig));
  if (!backup.HasValue()) {
    return Result<void>(MakeErrorCode(PerErrc::kValidationFailed));
  }
  auto it = backup.Value().find(std::string(key));
  if (it == backup.Value().end()) {
    return Result<void>(MakeErrorCode(PerErrc::kKeyNotFound));
  }
  std::lock_guard<std::mutex> lock(mMutex);
  mData[std::string(key)] = *it;
  return Result<void>();
}

Result<std::uint64_t> KeyValueStorage::GetCurrentStorageSize() const {
  std::lock_guard<std::mutex> lock(mMutex);
  return Result<std::uint64_t>(mData.dump().size());
}

bool KeyValueStorage::HasPendingChanges() const {
  std::lock_guard<std::mutex> lock(mMutex);
  return mData != mCommitted;
}

} // namespace ara::per
