/**
 * @file per_error_domain.h
 * @brief ara::per::PerErrc / PerErrorDomain / PerException (SWS_PER_xxxxx)
 *
 * NOTE: the enumerator *names* follow SWS Persistency R24-11.  The numeric
 * values and the domain id are the implementation's choice and must be
 * cross-checked against the SWS table "PerErrc" before certification use.
 */
#ifndef ARA_PER_PER_ERROR_DOMAIN_H_
#define ARA_PER_PER_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace ara::per {

enum class PerErrc : ara::core::ErrorDomain::CodeType {
  kStorageNotFound = 1,        ///< InstanceSpecifier not mapped to a storage
  kKeyNotFound = 2,            ///< key does not exist
  kIllegalWriteAccess = 3,     ///< write on a read-only storage / accessor
  kPhysicalStorageFailure = 4, ///< OS / hardware access failed
  kIntegrityCorrupted = 5,     ///< stored data fails its integrity check
  kValidationFailed = 6,       ///< redundancy insufficient to recover
  kEncryptionFailed = 7,
  kDataTypeMismatch = 8,       ///< stored value cannot be read as the requested type
  kInitValueNotAvailable = 9,  ///< no initial value configured for the key / file
  kResourceBusy = 10,          ///< storage or file is currently open / in use
  kOutOfStorageSpace = 11,     ///< device is full
  kFileNotFound = 12,
  kEof = 13,                   ///< read past the end of file
  kTooManyFiles = 14,          ///< maxNumberOfFiles exceeded
  kQuotaExceeded = 15,         ///< maximumAllowedSize exceeded
  kInvalidPosition = 16,
  kInvalidOpenMode = 17,
  kInvalidSize = 18,
  kNotInitialized = 19,        ///< persistency not configured
  kUnsupported = 20,
  kAuthenticationFailed = 21,
};

class PerException : public ara::core::Exception {
public:
  explicit PerException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class PerErrorDomain final : public ara::core::ErrorDomain {
public:
  static constexpr IdType kId = 0x8000000000000406; // implementation choice, see note above
  constexpr PerErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Per"; }
  char const *Message(CodeType errorCode) const noexcept override;
  [[noreturn]] void ThrowAsException(ara::core::ErrorCode const &errorCode) const
      noexcept(false) override;
};

namespace internal {
inline constexpr PerErrorDomain kPerErrorDomain;
}
constexpr ara::core::ErrorDomain const &GetPerDomain() noexcept { return internal::kPerErrorDomain; }

constexpr ara::core::ErrorCode
MakeErrorCode(PerErrc code, ara::core::ErrorDomain::SupportDataType data = 0,
              char const *message = nullptr) noexcept {
  return ara::core::ErrorCode(static_cast<ara::core::ErrorDomain::CodeType>(code), GetPerDomain(),
                              data, message);
}

} // namespace ara::per

#endif // ARA_PER_PER_ERROR_DOMAIN_H_
