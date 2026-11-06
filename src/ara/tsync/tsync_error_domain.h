/**
 * @file tsync_error_domain.h
 * @brief ara::tsync::TsyncErrc / TsyncErrorDomain.
 *
 * The SWS (R25-11) names kDaemonConnectionLost, kFunctionNotSupported and kLimitsExceeded; the
 * retrievable SWS text gives neither their numeric values nor the domain id (it defers to
 * SWS_CORE_90023), so values and id are implementation choices - see
 * doc/spec-conformance/tsync.md. The domain id is distinct from ara::nm's (they used to collide).
 */
#ifndef ARA_TSYNC_TSYNC_ERROR_DOMAIN_H_
#define ARA_TSYNC_TSYNC_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace ara::tsync {

enum class TsyncErrc : ara::core::ErrorDomain::CodeType {
  kNotSynchronized = 1, ///< the time base has never received a synchronized time
  kInvalidArgument = 2, ///< e.g. non-finite rate deviation or zero timeout
  kAlreadyExists = 3,   ///< time base name already registered
  kNotFound = 4,        ///< unknown time base name
  kDaemonConnectionLost = 5, ///< SWS: the time base is not bound / the sync daemon is unreachable
  kFunctionNotSupported = 6, ///< SWS_TS_00061: rate correction not allowed for this time base
  kLimitsExceeded = 7,       ///< SWS_TS_00063: value outside the configured limit
  kTimeCannotSet = 8,        ///< SWS_TS_01107/01207: the time cannot be set (e.g. not an offset time base)
};

class TsyncException : public ara::core::Exception {
public:
  explicit TsyncException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class TsyncErrorDomain final : public ara::core::ErrorDomain {
public:
  static constexpr IdType kId = 0x8000000000000502; // implementation choice (ara::nm uses ...0501)
  constexpr TsyncErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Tsync"; }
  char const *Message(CodeType errorCode) const noexcept override;
  [[noreturn]] void ThrowAsException(ara::core::ErrorCode const &errorCode) const
      noexcept(false) override;
};

namespace internal {
inline constexpr TsyncErrorDomain kTsyncErrorDomain;
}
constexpr ara::core::ErrorDomain const &GetTsyncDomain() noexcept {
  return internal::kTsyncErrorDomain;
}
constexpr ara::core::ErrorCode MakeErrorCode(TsyncErrc code,
                                             ara::core::ErrorDomain::SupportDataType data = 0,
                                             char const *message = nullptr) noexcept {
  return ara::core::ErrorCode(static_cast<ara::core::ErrorDomain::CodeType>(code), GetTsyncDomain(),
                              data, message);
}

} // namespace ara::tsync

#endif // ARA_TSYNC_TSYNC_ERROR_DOMAIN_H_
