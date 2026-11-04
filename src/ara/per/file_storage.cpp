#include "ara/per/file_storage.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <mutex>
#include <set>

#include "ara/per/fs_util.h"

namespace ara::per {

namespace fs = std::filesystem;
using ara::core::Byte;
using ara::core::Result;
using ara::core::StringView;

namespace detail {
struct FileStorageState {
  StorageConfig cfg;
  std::mutex mutex;
  std::set<std::string> openFiles;

  fs::path FilesDir() const { return cfg.directory / "files"; }

  std::uint64_t TotalSize() const {
    std::uint64_t total = 0;
    std::error_code ec;
    for (auto const &e : fs::directory_iterator(FilesDir(), ec)) {
      if (e.is_regular_file(ec)) {
        total += e.file_size(ec);
      }
    }
    return total;
  }
};
} // namespace detail

namespace {
bool ValidName(StringView n) {
  return !n.empty() && n.size() <= 255 && n != "." && n != ".." &&
         n.find('/') == StringView::npos && n.find('\0') == StringView::npos;
}
Result<void> BadName() {
  return Result<void>(MakeErrorCode(PerErrc::kFileNotFound, 0, "invalid file name"));
}
} // namespace

// ---------------------------------------------------------------------------
// ReadAccessor
// ---------------------------------------------------------------------------
ReadAccessor::ReadAccessor(std::shared_ptr<detail::FileStorageState> state, std::string name,
                           int fd, bool canRead, bool canWrite, bool append)
    : mState(std::move(state)), mName(std::move(name)), mFd(fd), mCanRead(canRead),
      mCanWrite(canWrite), mAppend(append) {}

ReadAccessor::~ReadAccessor() {
  if (mFd >= 0) {
    (void)::close(mFd);
  }
  if (mState) {
    std::lock_guard<std::mutex> lock(mState->mutex);
    mState->openFiles.erase(mName);
  }
}

ReadAccessor::Position ReadAccessor::GetSize() const noexcept {
  struct stat st {};
  if (::fstat(mFd, &st) != 0) {
    return 0;
  }
  return static_cast<Position>(st.st_size);
}

Result<std::string> ReadAccessor::ReadRaw(std::uint64_t n) {
  if (!mCanRead) {
    return Result<std::string>(MakeErrorCode(PerErrc::kUnsupported, 0, "file is write-only"));
  }
  Position const size = GetSize();
  if (mPosition >= size) {
    return Result<std::string>(MakeErrorCode(PerErrc::kEof));
  }
  std::uint64_t const count = std::min<std::uint64_t>(n, size - mPosition);
  std::string out(static_cast<std::size_t>(count), '\0');
  std::size_t done = 0;
  while (done < out.size()) {
    ssize_t const r = ::pread(mFd, &out[done], out.size() - done,
                              static_cast<off_t>(mPosition + done));
    if (r < 0) {
      if (errno == EINTR) {
        continue;
      }
      return Result<std::string>(detail::ErrnoToError(errno));
    }
    if (r == 0) {
      break;
    }
    done += static_cast<std::size_t>(r);
  }
  out.resize(done);
  mPosition += done;
  return Result<std::string>(std::move(out));
}

Result<char> ReadAccessor::PeekChar() const {
  auto const saved = mPosition;
  auto *self = const_cast<ReadAccessor *>(this); // position is restored below
  auto r = self->ReadRaw(1);
  self->mPosition = saved;
  if (!r.HasValue()) {
    return Result<char>(r.Error());
  }
  return Result<char>(r.Value()[0]);
}
Result<Byte> ReadAccessor::PeekByte() const {
  auto c = PeekChar();
  if (!c.HasValue()) {
    return Result<Byte>(c.Error());
  }
  return Result<Byte>(static_cast<Byte>(static_cast<unsigned char>(c.Value())));
}
Result<char> ReadAccessor::GetChar() {
  auto r = ReadRaw(1);
  if (!r.HasValue()) {
    return Result<char>(r.Error());
  }
  return Result<char>(r.Value()[0]);
}
Result<Byte> ReadAccessor::GetByte() {
  auto c = GetChar();
  if (!c.HasValue()) {
    return Result<Byte>(c.Error());
  }
  return Result<Byte>(static_cast<Byte>(static_cast<unsigned char>(c.Value())));
}
Result<ara::core::String> ReadAccessor::ReadText(std::uint64_t n) {
  auto r = ReadRaw(n);
  if (!r.HasValue()) {
    return Result<ara::core::String>(r.Error());
  }
  return Result<ara::core::String>(std::move(r).Value());
}
Result<ara::core::Vector<Byte>> ReadAccessor::ReadBinary(std::uint64_t n) {
  auto r = ReadRaw(n);
  if (!r.HasValue()) {
    return Result<ara::core::Vector<Byte>>(r.Error());
  }
  ara::core::Vector<Byte> out;
  out.reserve(r.Value().size());
  for (char c : r.Value()) {
    out.push_back(static_cast<Byte>(static_cast<unsigned char>(c)));
  }
  return Result<ara::core::Vector<Byte>>(std::move(out));
}
Result<ara::core::String> ReadAccessor::ReadLine(char delimiter) {
  if (!mCanRead) {
    return Result<ara::core::String>(MakeErrorCode(PerErrc::kUnsupported, 0, "file is write-only"));
  }
  if (IsEof()) {
    return Result<ara::core::String>(MakeErrorCode(PerErrc::kEof));
  }
  ara::core::String line;
  while (true) {
    auto c = GetChar();
    if (!c.HasValue() || c.Value() == delimiter) {
      break;
    }
    line.push_back(c.Value());
  }
  return Result<ara::core::String>(std::move(line));
}
Result<void> ReadAccessor::SetPosition(Position position) {
  if (position > GetSize()) {
    return Result<void>(MakeErrorCode(PerErrc::kInvalidPosition));
  }
  mPosition = position;
  return Result<void>();
}
Result<void> ReadAccessor::MovePosition(Origin origin, Offset offset) {
  std::int64_t base = 0;
  switch (origin) {
  case Origin::kBeginning: base = 0; break;
  case Origin::kCurrent: base = static_cast<std::int64_t>(mPosition); break;
  case Origin::kEnd: base = static_cast<std::int64_t>(GetSize()); break;
  }
  std::int64_t const target = base + offset;
  if (target < 0 || static_cast<Position>(target) > GetSize()) {
    return Result<void>(MakeErrorCode(PerErrc::kInvalidPosition));
  }
  mPosition = static_cast<Position>(target);
  return Result<void>();
}

// ---------------------------------------------------------------------------
// ReadWriteAccessor
// ---------------------------------------------------------------------------
Result<void> ReadWriteAccessor::SyncToFile() {
  if (::fsync(mFd) != 0) {
    return Result<void>(detail::ErrnoToError(errno));
  }
  return Result<void>();
}

Result<void> ReadWriteAccessor::SetFileSize(std::uint64_t size) {
  if (!mCanWrite) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::lock_guard<std::mutex> lock(mState->mutex);
  std::uint64_t const current = GetSize();
  if (size > current && mState->cfg.maxSize != 0 &&
      mState->TotalSize() + (size - current) > mState->cfg.maxSize) {
    return Result<void>(MakeErrorCode(PerErrc::kQuotaExceeded));
  }
  if (::ftruncate(mFd, static_cast<off_t>(size)) != 0) {
    return Result<void>(detail::ErrnoToError(errno));
  }
  mPosition = std::min(mPosition, size);
  return Result<void>();
}

Result<void> ReadWriteAccessor::WriteBinary(Byte const *data, std::size_t size) {
  if (!mCanWrite) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::lock_guard<std::mutex> lock(mState->mutex);
  std::uint64_t const current = GetSize();
  if (mAppend) {
    mPosition = current;
  }
  std::uint64_t const end = mPosition + size;
  if (end > current && mState->cfg.maxSize != 0 &&
      mState->TotalSize() + (end - current) > mState->cfg.maxSize) {
    return Result<void>(MakeErrorCode(PerErrc::kQuotaExceeded));
  }
  auto const *p = reinterpret_cast<char const *>(data);
  std::size_t done = 0;
  while (done < size) {
    ssize_t const w = ::pwrite(mFd, p + done, size - done, static_cast<off_t>(mPosition + done));
    if (w < 0) {
      if (errno == EINTR) {
        continue;
      }
      return Result<void>(detail::ErrnoToError(errno));
    }
    done += static_cast<std::size_t>(w);
  }
  mPosition += size;
  return Result<void>();
}
Result<void> ReadWriteAccessor::WriteBinary(ara::core::Vector<Byte> const &data) {
  return WriteBinary(data.data(), data.size());
}
Result<void> ReadWriteAccessor::WriteText(StringView text) {
  return WriteBinary(reinterpret_cast<Byte const *>(text.data()), text.size());
}
ReadWriteAccessor &ReadWriteAccessor::operator<<(StringView text) {
  if (!WriteText(text).HasValue()) {
    mFail = true;
  }
  return *this;
}

// ---------------------------------------------------------------------------
// FileStorage
// ---------------------------------------------------------------------------
struct FsAccess {
  static Result<SharedHandle<FileStorage>> Open(StorageConfig cfg) {
    SharedHandle<FileStorage> fsStorage(new FileStorage(std::move(cfg)));
    std::error_code ec;
    fs::create_directories(fsStorage->mState->FilesDir(), ec);
    if (ec && !fsStorage->mState->cfg.readOnly) {
      return Result<SharedHandle<FileStorage>>(detail::ErrnoToError(ec.value()));
    }
    return Result<SharedHandle<FileStorage>>(std::move(fsStorage));
  }
};

// Defined here (not in the header) so that detail::FileStorageState is complete.
Result<SharedHandle<FileStorage>> OpenFileStorageImpl(StorageConfig cfg) {
  return FsAccess::Open(std::move(cfg));
}

FileStorage::FileStorage(StorageConfig cfg) : mState(std::make_shared<detail::FileStorageState>()) {
  mState->cfg = std::move(cfg);
}
FileStorage::~FileStorage() = default;

Result<ara::core::Vector<ara::core::String>> FileStorage::GetAllFileNames() const {
  ara::core::Vector<ara::core::String> names;
  std::error_code ec;
  for (auto const &e : fs::directory_iterator(mState->FilesDir(), ec)) {
    if (e.is_regular_file(ec)) {
      names.push_back(e.path().filename().string());
    }
  }
  std::sort(names.begin(), names.end());
  return Result<ara::core::Vector<ara::core::String>>(std::move(names));
}

Result<bool> FileStorage::FileExists(StringView fileName) const {
  if (!ValidName(fileName)) {
    return Result<bool>(false);
  }
  std::error_code ec;
  return Result<bool>(fs::is_regular_file(mState->FilesDir() / std::string(fileName), ec));
}

Result<void> FileStorage::DeleteFile(StringView fileName) {
  if (!ValidName(fileName)) {
    return BadName();
  }
  if (mState->cfg.readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::lock_guard<std::mutex> lock(mState->mutex);
  if (mState->openFiles.count(std::string(fileName)) != 0) {
    return Result<void>(MakeErrorCode(PerErrc::kResourceBusy));
  }
  std::error_code ec;
  if (!fs::remove(mState->FilesDir() / std::string(fileName), ec)) {
    return Result<void>(MakeErrorCode(PerErrc::kFileNotFound));
  }
  return Result<void>();
}

Result<void> FileStorage::RecoverFile(StringView) {
  return Result<void>(MakeErrorCode(PerErrc::kValidationFailed, 0, "no redundancy configured"));
}

Result<void> FileStorage::ResetFile(StringView fileName) {
  if (!ValidName(fileName)) {
    return BadName();
  }
  if (mState->cfg.readOnly) {
    return Result<void>(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  std::lock_guard<std::mutex> lock(mState->mutex);
  std::string const name(fileName);
  if (mState->openFiles.count(name) != 0) {
    return Result<void>(MakeErrorCode(PerErrc::kResourceBusy));
  }
  auto const initial = mState->cfg.directory / "initial" / name;
  std::error_code ec;
  if (!fs::is_regular_file(initial, ec)) {
    return Result<void>(MakeErrorCode(PerErrc::kInitValueNotAvailable));
  }
  fs::copy_file(initial, mState->FilesDir() / name, fs::copy_options::overwrite_existing, ec);
  if (ec) {
    return Result<void>(detail::ErrnoToError(ec.value()));
  }
  return Result<void>();
}

Result<std::uint64_t> FileStorage::GetCurrentFileSize(StringView fileName) const {
  auto info = GetFileInfo(fileName);
  if (!info.HasValue()) {
    return Result<std::uint64_t>(info.Error());
  }
  return Result<std::uint64_t>(info.Value().fileSize);
}

Result<FileInfo> FileStorage::GetFileInfo(StringView fileName) const {
  if (!ValidName(fileName)) {
    return Result<FileInfo>(MakeErrorCode(PerErrc::kFileNotFound));
  }
  auto const path = mState->FilesDir() / std::string(fileName);
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) {
    return Result<FileInfo>(MakeErrorCode(PerErrc::kFileNotFound));
  }
  FileInfo info;
  info.fileSize = fs::file_size(path, ec);
  auto const ftime = fs::last_write_time(path, ec);
  info.modificationTime = std::chrono::system_clock::now() +
                          std::chrono::duration_cast<std::chrono::system_clock::duration>(
                              ftime - fs::file_time_type::clock::now());
  return Result<FileInfo>(info);
}

Result<std::uint64_t> FileStorage::GetCurrentStorageSize() const {
  std::lock_guard<std::mutex> lock(mState->mutex);
  return Result<std::uint64_t>(mState->TotalSize());
}

Result<UniqueHandle<ReadAccessor>> FileStorage::OpenFileReadOnly(StringView fileName,
                                                                 OpenMode mode) {
  using R = Result<UniqueHandle<ReadAccessor>>;
  if (!ValidName(fileName)) {
    return R(MakeErrorCode(PerErrc::kFileNotFound, 0, "invalid file name"));
  }
  if (Has(mode, OpenMode::kTruncate) || Has(mode, OpenMode::kAppend) ||
      (Has(mode, OpenMode::kAtTheBeginning) && Has(mode, OpenMode::kAtTheEnd))) {
    return R(MakeErrorCode(PerErrc::kInvalidOpenMode));
  }
  std::string const name(fileName);
  std::lock_guard<std::mutex> lock(mState->mutex);
  if (mState->openFiles.count(name) != 0) {
    return R(MakeErrorCode(PerErrc::kResourceBusy));
  }
  int const fd = ::open((mState->FilesDir() / name).c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return R(detail::ErrnoToError(errno));
  }
  mState->openFiles.insert(name);
  UniqueHandle<ReadAccessor> acc(new ReadAccessor(mState, name, fd, true, false, false));
  if (Has(mode, OpenMode::kAtTheEnd)) {
    acc->mPosition = acc->GetSize();
  }
  return R(std::move(acc));
}

Result<UniqueHandle<ReadWriteAccessor>> FileStorage::OpenWritable(StringView fileName,
                                                                  OpenMode mode, bool readable) {
  using R = Result<UniqueHandle<ReadWriteAccessor>>;
  if (!ValidName(fileName)) {
    return R(MakeErrorCode(PerErrc::kFileNotFound, 0, "invalid file name"));
  }
  if (mState->cfg.readOnly) {
    return R(MakeErrorCode(PerErrc::kIllegalWriteAccess));
  }
  if (Has(mode, OpenMode::kAtTheBeginning) && (Has(mode, OpenMode::kAtTheEnd) || Has(mode, OpenMode::kAppend))) {
    return R(MakeErrorCode(PerErrc::kInvalidOpenMode));
  }
  std::string const name(fileName);
  std::lock_guard<std::mutex> lock(mState->mutex);
  if (mState->openFiles.count(name) != 0) {
    return R(MakeErrorCode(PerErrc::kResourceBusy));
  }
  auto const path = mState->FilesDir() / name;
  std::error_code ec;
  if (!fs::exists(path, ec) && mState->cfg.maxFiles != 0) {
    std::uint32_t count = 0;
    for (auto const &e : fs::directory_iterator(mState->FilesDir(), ec)) {
      count += e.is_regular_file(ec) ? 1U : 0U;
    }
    if (count >= mState->cfg.maxFiles) {
      return R(MakeErrorCode(PerErrc::kTooManyFiles));
    }
  }
  int flags = (readable ? O_RDWR : O_WRONLY) | O_CREAT | O_CLOEXEC;
  if (Has(mode, OpenMode::kTruncate)) {
    flags |= O_TRUNC;
  }
  int const fd = ::open(path.c_str(), flags, 0600);
  if (fd < 0) {
    return R(detail::ErrnoToError(errno));
  }
  mState->openFiles.insert(name);
  bool const append = Has(mode, OpenMode::kAppend);
  UniqueHandle<ReadWriteAccessor> acc(new ReadWriteAccessor(mState, name, fd, readable, true, append));
  if (Has(mode, OpenMode::kAtTheEnd) || append) {
    acc->mPosition = acc->GetSize();
  }
  return R(std::move(acc));
}

Result<UniqueHandle<ReadWriteAccessor>> FileStorage::OpenFileReadWrite(StringView fileName,
                                                                       OpenMode mode) {
  return OpenWritable(fileName, mode, /*readable=*/true);
}
Result<UniqueHandle<ReadWriteAccessor>> FileStorage::OpenFileWriteOnly(StringView fileName,
                                                                       OpenMode mode) {
  return OpenWritable(fileName, mode, /*readable=*/false);
}

} // namespace ara::per
