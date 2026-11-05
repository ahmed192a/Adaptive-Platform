/**
 * @file crypto_error_domain.h
 * @brief ara::crypto::CryptoErrc / CryptoErrorDomain.
 * Names follow the SWS Cryptography; numeric values / domain id are implementation
 * choices to be verified against the SWS table.
 */
#ifndef ARA_CRYPTO_CRYPTO_ERROR_DOMAIN_H_
#define ARA_CRYPTO_CRYPTO_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace ara::crypto {

enum class CryptoErrc : ara::core::ErrorDomain::CodeType {
  kInvalidArgument = 1,
  kUnknownIdentifier = 2,
  kInsufficientCapacity = 3,
  kInvalidInputSize = 4,
  kIncompatibleArguments = 5,
  kInOutBuffersIntersect = 6,
  kEmptyContainer = 7,
  kUninitializedContext = 8,
  kProcessingNotStarted = 9,
  kProcessingNotFinished = 10,
  kUnsupported = 11,
  kAuthTagNotValid = 12,   ///< AEAD tag / signature / MAC mismatch
  kUsageViolation = 13,    ///< key used for a purpose it was not created for
  kKeyNotExportable = 14,
  kRuntimeFault = 15,      ///< underlying crypto provider failed
  kInvalidKeyLength = 16,
  kBadObjectReference = 17,
  kBusyResource = 18,      ///< resource is in use by another context (SWS name; value unverified)
};

class CryptoException : public ara::core::Exception {
public:
  explicit CryptoException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class CryptoErrorDomain final : public ara::core::ErrorDomain {
public:
  static constexpr IdType kId = 0x8000000000000801; // implementation choice
  constexpr CryptoErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Crypto"; }
  char const *Message(CodeType errorCode) const noexcept override;
  [[noreturn]] void ThrowAsException(ara::core::ErrorCode const &errorCode) const
      noexcept(false) override;
};

namespace internal {
inline constexpr CryptoErrorDomain kCryptoErrorDomain;
}
constexpr ara::core::ErrorDomain const &GetCryptoDomain() noexcept {
  return internal::kCryptoErrorDomain;
}
constexpr ara::core::ErrorCode MakeErrorCode(CryptoErrc code,
                                             ara::core::ErrorDomain::SupportDataType data = 0,
                                             char const *message = nullptr) noexcept {
  return ara::core::ErrorCode(static_cast<ara::core::ErrorDomain::CodeType>(code),
                              GetCryptoDomain(), data, message);
}

} // namespace ara::crypto

#endif // ARA_CRYPTO_CRYPTO_ERROR_DOMAIN_H_
