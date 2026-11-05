/**
 * @file core_error_domain.h
 * @brief ara::core::CoreErrc / CoreErrorDomain / CoreException (SWS_CORE_05200 ff.)
 */
#ifndef ARA_CORE_CORE_ERROR_DOMAIN_H_
#define ARA_CORE_CORE_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

namespace ara::core {

enum class CoreErrc : ErrorDomain::CodeType {
  kInvalidArgument = 22,             ///< an invalid argument was passed to a function
  kInvalidMetaModelShortname = 137,  ///< given string is not a valid model element shortname
  kInvalidMetaModelPath = 138,       ///< missing or invalid path to model element
  // ---- implementation specific extensions (not part of the SWS) ----------
  kAlreadyInitialized = 256,         ///< ara::core::Initialize() called twice
  kNotInitialized = 257,             ///< ara::core::Deinitialize() without Initialize()
  kInitializationFailed = 258,       ///< a functional cluster failed to initialize
};

class CoreException : public Exception {
public:
  explicit CoreException(ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class CoreErrorDomain final : public ErrorDomain {
public:
  constexpr CoreErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Core"; }
  char const *Message(CodeType errorCode) const noexcept override;
  [[noreturn]] void ThrowAsException(ErrorCode const &errorCode) const noexcept(false) override;

  static constexpr ErrorDomain::IdType kId = 0x8000000000000014;
};

namespace internal {
inline constexpr CoreErrorDomain kCoreErrorDomain;
}

constexpr ErrorDomain const &GetCoreErrorDomain() noexcept { return internal::kCoreErrorDomain; }

constexpr ErrorCode MakeErrorCode(CoreErrc code, ErrorDomain::SupportDataType data = 0,
                                  char const *message = nullptr) noexcept {
  return ErrorCode(static_cast<ErrorDomain::CodeType>(code), GetCoreErrorDomain(), data, message);
}

} // namespace ara::core

#endif // ARA_CORE_CORE_ERROR_DOMAIN_H_
