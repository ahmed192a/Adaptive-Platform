/**
 * @file phm_error_domain.h
 * @brief ara::phm::PhmErrc / PhmErrorDomain / PhmException
 *
 * kSMCanNotHandleRecovery is named in the SWS (SWS_PHM_00107).  The other
 * enumerators and all numeric values / the domain id are implementation
 * choices - verify against the SWS table before certification use.
 */
#ifndef ARA_PHM_PHM_ERROR_DOMAIN_H_
#define ARA_PHM_PHM_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace ara::phm {

enum class PhmErrc : ara::core::ErrorDomain::CodeType {
  kSMCanNotHandleRecovery = 1, ///< recovery handler could not recover the supervision
  kInvalidConfiguration = 2,   ///< (implementation specific) bad PHM configuration
  kUnknownEntity = 3,          ///< (implementation specific) entity not in configuration
  kUnknownSupervision = 4,     ///< (implementation specific)
  kAlreadyOffered = 5,         ///< (implementation specific) RecoveryAction offered twice
};

class PhmException : public ara::core::Exception {
public:
  explicit PhmException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class PhmErrorDomain final : public ara::core::ErrorDomain {
public:
  static constexpr IdType kId = 0x8000000000000407; // implementation choice
  constexpr PhmErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Phm"; }
  char const *Message(CodeType errorCode) const noexcept override;
  [[noreturn]] void ThrowAsException(ara::core::ErrorCode const &errorCode) const
      noexcept(false) override;
};

namespace internal {
inline constexpr PhmErrorDomain kPhmErrorDomain;
}
constexpr ara::core::ErrorDomain const &GetPhmDomain() noexcept { return internal::kPhmErrorDomain; }

constexpr ara::core::ErrorCode
MakeErrorCode(PhmErrc code, ara::core::ErrorDomain::SupportDataType data = 0,
              char const *message = nullptr) noexcept {
  return ara::core::ErrorCode(static_cast<ara::core::ErrorDomain::CodeType>(code), GetPhmDomain(),
                              data, message);
}

} // namespace ara::phm

#endif // ARA_PHM_PHM_ERROR_DOMAIN_H_
