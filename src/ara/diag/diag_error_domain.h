/**
 * @file diag_error_domain.h
 * @brief ara::diag::DiagErrc / DiagErrorDomain.
 *
 * Enumerator names follow SWS Diagnostics; numeric values and the domain id are
 * implementation choices and must be verified against the SWS table.
 */
#ifndef ARA_DIAG_DIAG_ERROR_DOMAIN_H_
#define ARA_DIAG_DIAG_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace ara::diag {

enum class DiagErrc : ara::core::ErrorDomain::CodeType {
  kAlreadyOffered = 1,   ///< service / DID is already registered
  kConflict = 2,         ///< request conflicts with the current state
  kNotOffered = 3,       ///< service / DID is not registered
  kInvalidArgument = 4,
  kMalformedRequest = 5, ///< request too short or inconsistent
  kRejected = 6,         ///< handler rejected the request
};

class DiagException : public ara::core::Exception {
public:
  explicit DiagException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class DiagErrorDomain final : public ara::core::ErrorDomain {
public:
  static constexpr IdType kId = 0x8000000000000401; // implementation choice
  constexpr DiagErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Diag"; }
  char const *Message(CodeType errorCode) const noexcept override;
  [[noreturn]] void ThrowAsException(ara::core::ErrorCode const &errorCode) const
      noexcept(false) override;
};

namespace internal {
inline constexpr DiagErrorDomain kDiagErrorDomain;
}
constexpr ara::core::ErrorDomain const &GetDiagDomain() noexcept {
  return internal::kDiagErrorDomain;
}
constexpr ara::core::ErrorCode MakeErrorCode(DiagErrc code,
                                             ara::core::ErrorDomain::SupportDataType data = 0,
                                             char const *message = nullptr) noexcept {
  return ara::core::ErrorCode(static_cast<ara::core::ErrorDomain::CodeType>(code), GetDiagDomain(),
                              data, message);
}

} // namespace ara::diag

#endif // ARA_DIAG_DIAG_ERROR_DOMAIN_H_
