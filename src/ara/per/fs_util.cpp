#include "ara/per/fs_util.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <fstream>
#include <sstream>
#include <system_error>

#include "ara/per/per_error_domain.h"

namespace ara::per::detail {

using ara::core::ErrorCode;
using ara::core::Result;

ErrorCode ErrnoToError(int err) {
  switch (err) {
  case ENOSPC:
  case EDQUOT:
    return MakeErrorCode(PerErrc::kOutOfStorageSpace, err);
  case ENOENT:
    return MakeErrorCode(PerErrc::kFileNotFound, err);
  case EROFS:
  case EACCES:
  case EPERM:
    return MakeErrorCode(PerErrc::kIllegalWriteAccess, err);
  case EBUSY:
    return MakeErrorCode(PerErrc::kResourceBusy, err);
  default:
    return MakeErrorCode(PerErrc::kPhysicalStorageFailure, err);
  }
}

Result<std::string> ReadWholeFile(std::filesystem::path const &file) {
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) {
    return Result<std::string>(MakeErrorCode(PerErrc::kFileNotFound));
  }
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return Result<std::string>(MakeErrorCode(PerErrc::kPhysicalStorageFailure));
  }
  std::stringstream ss;
  ss << in.rdbuf();
  return Result<std::string>(ss.str());
}

void SyncDirectory(std::filesystem::path const &dir) noexcept {
  int const fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd >= 0) {
    (void)::fsync(fd);
    (void)::close(fd);
  }
}

Result<void> WriteFileAtomic(std::filesystem::path const &target, std::string const &content,
                             bool keepBackup) {
  std::error_code ec;
  std::filesystem::create_directories(target.parent_path(), ec);
  if (ec) {
    return Result<void>(ErrnoToError(ec.value()));
  }
  auto tmp = target;
  tmp += ".tmp";
  int const fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    return Result<void>(ErrnoToError(errno));
  }
  char const *p = content.data();
  std::size_t left = content.size();
  while (left > 0) {
    ssize_t const n = ::write(fd, p, left);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      int const e = errno;
      (void)::close(fd);
      std::filesystem::remove(tmp, ec);
      return Result<void>(ErrnoToError(e));
    }
    p += n;
    left -= static_cast<std::size_t>(n);
  }
  if (::fsync(fd) != 0) {
    int const e = errno;
    (void)::close(fd);
    std::filesystem::remove(tmp, ec);
    return Result<void>(ErrnoToError(e));
  }
  (void)::close(fd);
  if (keepBackup && std::filesystem::exists(target, ec)) {
    auto bak = target;
    bak += ".bak";
    std::filesystem::rename(target, bak, ec);
    if (ec) {
      std::filesystem::remove(tmp, ec);
      return Result<void>(ErrnoToError(ec.value()));
    }
  }
  std::filesystem::rename(tmp, target, ec);
  if (ec) {
    return Result<void>(ErrnoToError(ec.value()));
  }
  SyncDirectory(target.parent_path());
  return Result<void>();
}

} // namespace ara::per::detail
