/**
 * @file future_error_domain.h
 * @brief ara::core::FutureErrc / FutureErrorDomain / FutureException
 *
 * The domain id 0x8000'0000'0000'0013 is stated in the R23-11 SWS_Core (SWS_CORE_00013).
 * The numeric values of FutureErrc follow the std::future_errc values used by the SWS;
 * they could not be re-verified against the R25-11 chapter 8 text (see
 * doc/spec-conformance/core.md).
 */
#ifndef ARA_CORE_FUTURE_ERROR_DOMAIN_H_
#define ARA_CORE_FUTURE_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace ara::core {

enum class FutureErrc : ErrorDomain::CodeType {
  broken_promise = 101,            ///< the asynchronous task abandoned its shared state
  future_already_retrieved = 102,  ///< Promise::get_future() was called more than once
  promise_already_satisfied = 103, ///< a value or error was already set
  no_state = 104,                  ///< the Future/Promise has no associated shared state
};

class FutureException : public Exception {
public:
  explicit FutureException(ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class FutureErrorDomain final : public ErrorDomain {
public:
  constexpr FutureErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Future"; }
  char const *Message(CodeType errorCode) const noexcept override;
  [[noreturn]] void ThrowAsException(ErrorCode const &errorCode) const noexcept(false) override;

  static constexpr ErrorDomain::IdType kId = 0x8000000000000013;
};

namespace internal {
inline constexpr FutureErrorDomain kFutureErrorDomain;
}

constexpr ErrorDomain const &GetFutureDomain() noexcept { return internal::kFutureErrorDomain; }

constexpr ErrorCode MakeErrorCode(FutureErrc code, ErrorDomain::SupportDataType data = 0,
                                  char const *message = nullptr) noexcept {
  return ErrorCode(static_cast<ErrorDomain::CodeType>(code), GetFutureDomain(), data, message);
}

} // namespace ara::core

#endif // ARA_CORE_FUTURE_ERROR_DOMAIN_H_
