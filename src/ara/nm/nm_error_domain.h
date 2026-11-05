/**
 * @file nm_error_domain.h
 * @brief ara::nm::NmErrc / NmErrorDomain / NmException (SWS_ANM_01100 ff.)
 *
 * kServiceNotAvailable = 1 and kInvalidHandler = 2 are the normative codes (SWS_ANM_01111).
 * Codes >= 0x100 are implementation extensions used by the NmChannel engine. The numeric
 * domain id is not given in the retrievable SWS text (it defers to SWS_CORE_90023) and is an
 * implementation choice - see doc/spec-conformance/nm.md.
 */
#ifndef ARA_NM_NM_ERROR_DOMAIN_H_
#define ARA_NM_NM_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace ara::nm {

enum class NmErrc : ara::core::ErrorDomain::CodeType {
  kServiceNotAvailable = 1, ///< daemon connection lost / port not bound (SWS_ANM_01111)
  kInvalidHandler = 2,      ///< notifier does not exist or is empty (SWS_ANM_01111)
  // ---- implementation extensions (not in the SWS) ----
  kUnknownNetwork = 0x100, ///< no network with that name / partial network id out of range
  kNetworkExists = 0x101,  ///< a network with that name was already created
  kNotRequested = 0x102,   ///< Release() without a preceding Request()
  kBadPdu = 0x103,         ///< NM message has the wrong length or inconsistent content
  kInvalidConfig = 0x104,  ///< NmConfig has zero timers or a zero cycle time
};

class NmException : public ara::core::Exception {
public:
  explicit NmException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class NmErrorDomain final : public ara::core::ErrorDomain {
public:
  using Errc = NmErrc;             // SWS_ANM_01103
  using Exception = NmException;   // SWS_ANM_01104
  static constexpr IdType kId = 0x8000000000000501; // implementation choice (tsync uses ...0502)
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
