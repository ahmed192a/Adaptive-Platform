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

#include "ara/core/instance_specifier.h"
#include "ara/core/result.h"
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
OpenKeyValueStorage(ara::core::InstanceSpecifier const &specifier);
ara::core::Result<SharedHandle<FileStorage>>
OpenFileStorage(ara::core::InstanceSpecifier const &specifier);

/// The following only succeed while the storage is not open (kResourceBusy).
ara::core::Result<void> RecoverKeyValueStorage(ara::core::InstanceSpecifier const &specifier);
ara::core::Result<void> ResetKeyValueStorage(ara::core::InstanceSpecifier const &specifier);
ara::core::Result<void> RecoverAllFiles(ara::core::InstanceSpecifier const &specifier);
ara::core::Result<void> ResetAllFiles(ara::core::InstanceSpecifier const &specifier);
/// Resets every configured storage to its initial state.
ara::core::Result<void> ResetPersistency();

ara::core::Result<std::uint64_t>
GetCurrentKeyValueStorageSize(ara::core::InstanceSpecifier const &specifier);
ara::core::Result<std::uint64_t>
GetCurrentFileStorageSize(ara::core::InstanceSpecifier const &specifier);

} // namespace ara::per

#endif // ARA_PER_PERSISTENCY_H_
