/**
 * @file file_storage.h
 * @brief ara::per::FileStorage, ReadAccessor, ReadWriteAccessor, OpenMode (SWS_PER)
 *
 * Files live in `<storage directory>/files/`.  A file can be open only once
 * at a time (kResourceBusy).  Quotas (maxSize over all files, maxFiles) are
 * enforced when growing / creating files.
 */
#ifndef ARA_PER_FILE_STORAGE_H_
#define ARA_PER_FILE_STORAGE_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "ara/core/StringView.h"
#include "ara/core/byte.h"
#include "ara/core/result.h"
#include "ara/core/string.h"
#include "ara/core/vector.h"
#include "ara/per/key_value_storage.h" // SharedHandle / UniqueHandle
#include "ara/per/per_error_domain.h"
#include "ara/per/persistency_config.h"

namespace ara::per {

enum class OpenMode : std::uint32_t {
  kAtTheBeginning = 0x1, ///< position at start of file
  kAtTheEnd = 0x2,       ///< position at end of file
  kAppend = 0x4,         ///< every write goes to the end (implies kAtTheEnd)
  kTruncate = 0x8,       ///< discard existing content
};
constexpr OpenMode operator|(OpenMode a, OpenMode b) noexcept {
  return static_cast<OpenMode>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr OpenMode operator&(OpenMode a, OpenMode b) noexcept {
  return static_cast<OpenMode>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline OpenMode &operator|=(OpenMode &a, OpenMode b) noexcept { return a = a | b; }
constexpr bool Has(OpenMode set, OpenMode flag) noexcept {
  return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(flag)) != 0U;
}

struct FileInfo {
  std::chrono::system_clock::time_point modificationTime;
  std::uint64_t fileSize{0};
};

namespace detail {
struct FileStorageState;
}

class ReadAccessor {
public:
  using Position = std::uint64_t;
  using Offset = std::int64_t;
  enum class Origin : std::uint8_t { kBeginning, kCurrent, kEnd };

  ReadAccessor(ReadAccessor const &) = delete;
  ReadAccessor &operator=(ReadAccessor const &) = delete;
  virtual ~ReadAccessor();

  ara::core::Result<char> PeekChar() const noexcept;
  ara::core::Result<ara::core::Byte> PeekByte() const noexcept;
  ara::core::Result<char> GetChar() noexcept;
  ara::core::Result<ara::core::Byte> GetByte() noexcept;
  /// Reads up to n bytes as text; kEof if already at the end.
  ara::core::Result<ara::core::String> ReadText(std::uint64_t n = UINT64_MAX) noexcept;
  ara::core::Result<ara::core::Vector<ara::core::Byte>> ReadBinary(std::uint64_t n = UINT64_MAX) noexcept;
  /// Reads up to (and consumes, but does not return) `delimiter`.
  ara::core::Result<ara::core::String> ReadLine(char delimiter = '\n') noexcept;

  Position GetSize() const noexcept;
  Position GetPosition() const noexcept { return mPosition; }
  bool IsEof() const noexcept { return mPosition >= GetSize(); }
  ara::core::Result<void> SetPosition(Position position) noexcept;
  /// Returns the new position.
  ara::core::Result<Position> MovePosition(Origin origin, Offset offset) noexcept;

protected:
  friend class FileStorage;
  ReadAccessor(std::shared_ptr<detail::FileStorageState> state, std::string name, int fd,
               bool canRead, bool canWrite, bool append);

  ara::core::Result<std::string> ReadRaw(std::uint64_t n);

  std::shared_ptr<detail::FileStorageState> mState;
  std::string mName;
  int mFd;
  bool mCanRead;
  bool mCanWrite;
  bool mAppend;
  Position mPosition{0};
};

class ReadWriteAccessor final : public ReadAccessor {
public:
  ~ReadWriteAccessor() override = default;

  ara::core::Result<void> SyncToFile();
  ara::core::Result<void> SetFileSize(std::uint64_t size);
  ara::core::Result<void> WriteText(ara::core::StringView text);
  ara::core::Result<void> WriteBinary(ara::core::Vector<ara::core::Byte> const &data);
  ara::core::Result<void> WriteBinary(ara::core::Byte const *data, std::size_t size);
  /// Stream-style write; failures set the fail flag (see Good()).
  ReadWriteAccessor &operator<<(ara::core::StringView text);
  bool Good() const noexcept { return !mFail; }

private:
  friend class FileStorage;
  using ReadAccessor::ReadAccessor;
  bool mFail{false};
};

class FileStorage final {
public:
  FileStorage(FileStorage const &) = delete;
  FileStorage &operator=(FileStorage const &) = delete;
  ~FileStorage();

  ara::core::Result<ara::core::Vector<ara::core::String>> GetAllFileNames() const noexcept;
  ara::core::Result<bool> FileExists(ara::core::StringView fileName) const noexcept;
  ara::core::Result<void> DeleteFile(ara::core::StringView fileName) noexcept;
  /// No redundancy is kept for files: always kValidationFailed.
  ara::core::Result<void> RecoverFile(ara::core::StringView fileName) noexcept;
  /// Restores `<storage dir>/initial/<fileName>`; kInitValueNotAvailable if absent.
  ara::core::Result<void> ResetFile(ara::core::StringView fileName);
  ara::core::Result<std::uint64_t> GetCurrentFileSize(ara::core::StringView fileName) const;
  ara::core::Result<FileInfo> GetFileInfo(ara::core::StringView fileName) const;
  ara::core::Result<std::uint64_t> GetCurrentStorageSize() const;

  ara::core::Result<UniqueHandle<ReadAccessor>>
  OpenFileReadOnly(ara::core::StringView fileName, OpenMode mode = OpenMode::kAtTheBeginning);
  ara::core::Result<UniqueHandle<ReadWriteAccessor>>
  OpenFileReadWrite(ara::core::StringView fileName, OpenMode mode = OpenMode::kAtTheBeginning);
  ara::core::Result<UniqueHandle<ReadWriteAccessor>>
  OpenFileWriteOnly(ara::core::StringView fileName, OpenMode mode = OpenMode::kTruncate);

private:
  friend struct FsAccess;
  explicit FileStorage(StorageConfig cfg);
  ara::core::Result<UniqueHandle<ReadWriteAccessor>> OpenWritable(ara::core::StringView fileName,
                                                                  OpenMode mode, bool readable);

  std::shared_ptr<detail::FileStorageState> mState;
};

} // namespace ara::per

#endif // ARA_PER_FILE_STORAGE_H_
