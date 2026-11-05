/**
 * @file persistency.h
 * @brief Free functions of the Persistency functional cluster (ara::per).
 *
 * @code
 *   ara::per::Configure(ara::per::PersistencyConfig::FromFile("per.json").Value());
 *   auto kvs = ara::per::OpenKeyValueStorage(ara::core::InstanceSpecifier("App/KvsPort")).Value();
 *   kvs->SetValue("volume", 7);
 *   kvs->SyncToStorage();
 * @endcode
 */
#ifndef ARA_PER_PERSISTENCY_H_
#define ARA_PER_PERSISTENCY_H_

#include <filesystem>
#include <functional>

#include "ara/core/instance_specifier.h"
#include "ara/core/result.h"
#include "ara/core/string.h"
#include "ara/core/vector.h"
#include "ara/per/file_storage.h"
#include "ara/per/key_value_storage.h"
#include "ara/per/persistency_config.h"

namespace ara::per {

/// Installs the deployment configuration (replaces a previous one; only
/// allowed while no storage is open, otherwise kResourceBusy).
ara::core::Result<void> Configure(PersistencyConfig config);
ara::core::Result<void> ConfigureFromFile(std::filesystem::path const &file);
/// Reads the file named by $ARA_PER_CONFIG (kNotInitialized if unset).
ara::core::Result<void> ConfigureFromEnvironment();

/// Opening the same specifier twice returns the same underlying storage.
ara::core::Result<SharedHandle<KeyValueStorage>>
OpenKeyValueStorage(ara::core::InstanceSpecifier const &specifier) noexcept;
ara::core::Result<SharedHandle<FileStorage>>
OpenFileStorage(ara::core::InstanceSpecifier const &specifier) noexcept;

/// The following only succeed while the storage is not open (kResourceBusy).
ara::core::Result<void> RecoverKeyValueStorage(ara::core::InstanceSpecifier const &specifier) noexcept;
ara::core::Result<void> ResetKeyValueStorage(ara::core::InstanceSpecifier const &specifier) noexcept;
ara::core::Result<void> RecoverAllFiles(ara::core::InstanceSpecifier const &specifier) noexcept;
ara::core::Result<void> ResetAllFiles(ara::core::InstanceSpecifier const &specifier) noexcept;
/// Resets every configured storage to its initial state.
ara::core::Result<void> ResetPersistency() noexcept;

ara::core::Result<std::uint64_t>
GetCurrentKeyValueStorageSize(ara::core::InstanceSpecifier const &specifier) noexcept;
ara::core::Result<std::uint64_t>
GetCurrentFileStorageSize(ara::core::InstanceSpecifier const &specifier) noexcept;

/// Kind of a recovery report (SWS name `RecoveryReportKind`; the enumerators below are
/// this implementation's choice — **Unverified** against the SWS enumerator table).
enum class RecoveryReportKind : std::uint8_t { kRecovered = 0, kReset = 1 };

using RecoveryReportCallback =
    std::function<void(ara::core::InstanceSpecifier const &storage, RecoveryReportKind kind,
                       ara::core::Vector<ara::core::String> reportedElements,
                       ara::core::Vector<std::uint8_t> reportedInstances)>;
using ApplicationDataUpdateCallback =
    std::function<void(ara::core::InstanceSpecifier const &storage, ara::core::String version)>;

/// Registers the callback invoked after a successful Recover*/Reset* of a storage.
/// Replaces a previously registered callback; an empty function unregisters.
void RegisterRecoveryReportCallback(RecoveryReportCallback recoveryReportCallback) noexcept;

/// Registers the callback for application-data updates. This implementation has no
/// manifest/update source, so the callback is stored but never invoked.
void RegisterApplicationDataUpdateCallback(ApplicationDataUpdateCallback appDataUpdateCallback) noexcept;

/// Applies pending persistency updates. Without an update source this validates
/// that persistency is configured and no storage is open, then succeeds.
ara::core::Result<void> UpdatePersistency() noexcept;

} // namespace ara::per

#endif // ARA_PER_PERSISTENCY_H_
