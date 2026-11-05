/**
 * @file nm_error_domain.h
 * @brief ara::nm::NmErrc / NmErrorDomain / NmException
 *
 * AUTOSAR Adaptive does not define a normative NM error domain for this
 * implementation; numeric values and the domain id are implementation choices.
 */
#ifndef ARA_NM_NM_ERROR_DOMAIN_H_
#define ARA_NM_NM_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace ara::nm {

enum class NmErrc : ara::core::ErrorDomain::CodeType {
  kUnknownNetwork = 1,  ///< no network with that name / partial network id out of range
  kNetworkExists = 2,   ///< a network with that name was already created
  kNotRequested = 3,    ///< Release() without a preceding Request()
  kBadPdu = 4,          ///< NM message has the wrong length or inconsistent content
  kInvalidConfig = 5,   ///< NmConfig has zero timers or a zero cycle time
};

class NmException : public ara::core::Exception {
public:
  explicit NmException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class NmErrorDomain final : public ara::core::ErrorDomain {
public:
  static constexpr IdType kId = 0x8000000000000501; // implementation choice
  constexpr NmErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Nm"; }
  char const *Message(CodeType errorCode) const noexcept override;
  [[noreturn]] void ThrowAsException(ara::core::ErrorCode const &errorCode) const
      noexcept(false) override;
};

namespace internal {
inline constexpr NmErrorDomain kNmErrorDomain;
}
constexpr ara::core::ErrorDomain const &GetNmDomain() noexcept { return internal::kNmErrorDomain; }

constexpr ara::core::ErrorCode MakeErrorCode(NmErrc code,
                                             ara::core::ErrorDomain::SupportDataType data = 0,
                                             char const *message = nullptr) noexcept {
  return ara::core::ErrorCode(static_cast<ara::core::ErrorDomain::CodeType>(code), GetNmDomain(),
                              data, message);
}

} // namespace ara::nm

#endif // ARA_NM_NM_ERROR_DOMAIN_H_
