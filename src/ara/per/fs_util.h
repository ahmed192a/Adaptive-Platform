/** @file fs_util.h  Internal POSIX helpers for crash-safe file persistence. */
#ifndef ARA_PER_FS_UTIL_H_
#define ARA_PER_FS_UTIL_H_

#include <filesystem>
#include <string>

#include "ara/core/result.h"

namespace ara::per::detail {

/// Maps errno to the matching PerErrc error code (ENOSPC -> kOutOfStorageSpace, ...).
ara::core::ErrorCode ErrnoToError(int err);

ara::core::Result<std::string> ReadWholeFile(std::filesystem::path const &file);

/// Writes `content` to `<target>.tmp`, fsync()s it, optionally keeps the
/// previous version as `<target>.bak`, renames atomically and fsync()s the
/// directory.  A crash at any point leaves either the old or the new version.
ara::core::Result<void> WriteFileAtomic(std::filesystem::path const &target,
                                        std::string const &content, bool keepBackup);

/// fsync() of a directory so that renames are durable.
void SyncDirectory(std::filesystem::path const &dir) noexcept;

} // namespace ara::per::detail

#endif // ARA_PER_FS_UTIL_H_
