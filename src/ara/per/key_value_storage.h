/**
 * @file key_value_storage.h
 * @brief ara::per::KeyValueStorage (SWS_PER, chapter "Key-Value Storage")
 *
 * Semantics implemented:
 *  - Reads/writes operate on an in-memory image; SyncToStorage() persists it
 *    atomically (temp file + fsync + rename) together with a CRC-32.  The
 *    previous version is kept as redundancy ("<file>.bak").
 *  - DiscardPendingChanges() rolls back to the last synced state.
 *  - All methods are thread safe.
 *  - Destroying the handle does NOT implicitly sync; call SyncToStorage().
 */
#ifndef ARA_PER_KEY_VALUE_STORAGE_H_
#define ARA_PER_KEY_VALUE_STORAGE_H_

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "ara/core/result.h"
#include "ara/core/string.h"
#include "ara/core/StringView.h"
#include "ara/core/vector.h"
#include "ara/per/per_error_domain.h"
#include "ara/per/persistency_config.h"

namespace ara::per {

template <typename T> using SharedHandle = std::shared_ptr<T>;
template <typename T> using UniqueHandle = std::unique_ptr<T>;

class KeyValueStorage final {
public:
  KeyValueStorage(KeyValueStorage const &) = delete;
  KeyValueStorage &operator=(KeyValueStorage const &) = delete;
  ~KeyValueStorage() = default;

  ara::core::Result<ara::core::Vector<ara::core::String>> GetAllKeys() const noexcept;
  ara::core::Result<bool> KeyExists(ara::core::StringView key) const noexcept;

  /// Reads `key` as T (arithmetic, bool, std::string, or JSON-convertible
  /// containers).  kKeyNotFound / kDataTypeMismatch on failure.
  template <typename T> ara::core::Result<T> GetValue(ara::core::StringView key) const noexcept {
    auto json = GetJson(key);
    if (!json.HasValue()) {
      return ara::core::Result<T>(json.Error());
    }
    return Convert<T>(json.Value());
  }

  /// Output-parameter form (SWS: `Result<void> GetValue(StringView, T&)`).
  template <typename T> ara::core::Result<void> GetValue(ara::core::StringView key, T &value) const noexcept {
    auto r = GetValue<T>(key);
    if (!r.HasValue()) {
      return ara::core::Result<void>(r.Error());
    }
    value = std::move(r).Value();
    return ara::core::Result<void>();
  }

  /// Stores `value` under `key` (in memory until SyncToStorage()).
  template <typename T>
  ara::core::Result<void> SetValue(ara::core::StringView key, T const &value) noexcept {
    try {
      return SetJson(key, nlohmann::json(value));
    } catch (...) { // value not representable as JSON
      return ara::core::Result<void>(MakeErrorCode(PerErrc::kDataTypeMismatch));
    }
  }

  ara::core::Result<void> RemoveKey(ara::core::StringView key) noexcept;
  ara::core::Result<void> RemoveAllKeys() noexcept;
  ara::core::Result<void> SyncToStorage() noexcept;
  ara::core::Result<void> DiscardPendingChanges() noexcept;
  /// Restores the configured initial value (kInitValueNotAvailable if none).
  ara::core::Result<void> ResetKey(ara::core::StringView key) noexcept;
  /// Restores `key` from the redundant copy on disk (kValidationFailed if absent).
  ara::core::Result<void> RecoverKey(ara::core::StringView key) noexcept;
  /// Serialized size of the current in-memory image in bytes.
  ara::core::Result<std::uint64_t> GetCurrentStorageSize() const;
  bool HasPendingChanges() const;

private:
  friend struct KvsAccess;
  explicit KeyValueStorage(StorageConfig cfg) : mConfig(std::move(cfg)) {}

  ara::core::Result<nlohmann::json> GetJson(ara::core::StringView key) const noexcept;
  ara::core::Result<void> SetJson(ara::core::StringView key, nlohmann::json value) noexcept;

  template <typename T> static ara::core::Result<T> Convert(nlohmann::json const &j) {
    using ara::core::Result;
    auto const mismatch = [] { return Result<T>(MakeErrorCode(PerErrc::kDataTypeMismatch)); };
    if constexpr (std::is_same_v<T, bool>) {
      if (!j.is_boolean()) {
        return mismatch();
      }
    } else if constexpr (std::is_integral_v<T>) {
      if (j.is_number_unsigned()) {
        if (j.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
          return mismatch();
        }
      } else if (j.is_number_integer()) {
        auto const v = j.get<std::int64_t>();
        if (v < 0 ? !std::is_signed_v<T> || v < static_cast<std::int64_t>(std::numeric_limits<T>::min())
                  : static_cast<std::uint64_t>(v) > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
          return mismatch();
        }
      } else {
        return mismatch();
      }
    } else if constexpr (std::is_floating_point_v<T>) {
      if (!j.is_number()) {
        return mismatch();
      }
    } else if constexpr (std::is_same_v<T, std::string>) {
      if (!j.is_string()) {
        return mismatch();
      }
    }
    try {
      return Result<T>(j.get<T>());
    } catch (nlohmann::json::exception const &) {
      return mismatch();
    }
  }

  static ara::core::Result<SharedHandle<KeyValueStorage>> Open(StorageConfig cfg);
  ara::core::Result<void> Load();

  StorageConfig mConfig;
  mutable std::mutex mMutex;
  nlohmann::json mData = nlohmann::json::object();      ///< working image
  nlohmann::json mCommitted = nlohmann::json::object(); ///< last persisted image
};

} // namespace ara::per

#endif // ARA_PER_KEY_VALUE_STORAGE_H_
