/**
 * @file tsync_error_domain.h
 * @brief ara::tsync::TsyncErrc / TsyncErrorDomain.
 *
 * Numeric values and the domain id are implementation choices (verify against the SWS table).
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
};

class TsyncException : public ara::core::Exception {
public:
  explicit TsyncException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class TsyncErrorDomain final : public ara::core::ErrorDomain {
public:
  static constexpr IdType kId = 0x8000000000000501; // implementation choice
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
