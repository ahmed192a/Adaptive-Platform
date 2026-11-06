/**
 * @file com_error_domain.h
 * @brief ara::com::ComErrc / ComErrorDomain / ComException.
 *
 * The enumerator names are those of the Communication Management SWS (R24-11 chapter 8 index).
 * The numeric values and the domain id come from the chapter-8 table, which could not be read
 * in any available edition, so they are *Unverified* (see doc/spec-conformance/com.md).
 */
#ifndef ARA_COM_COM_ERROR_DOMAIN_H_
#define ARA_COM_COM_ERROR_DOMAIN_H_

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"
#include "ara/core/exception.h"

#include <exception>

namespace ara::com {

enum class ComErrc : ara::core::ErrorDomain::CodeType {
  kServiceNotAvailable = 1,
  kMaxSamplesReached = 2,
  kBindingFailure = 3,
  kGrantEnforcementError = 4,
  kPeerIsUnreachable = 5,
  kFieldValueIsNotValid = 6,
  kSetHandlerNotSet = 7,
  kUnsetFailure = 8,
  kSampleAllocationFailure = 9,
  kIllegalUseOfAllocate = 10,
  kServiceNotOffered = 11,
  kCommunicationLinkError = 12,
  kNoClients = 13,
  kCommunicationStackError = 14,
  kMaxSampleCountNotRealizable = 15,
  kMaxSubscribersExceeded = 16,
  kWrongMethodCallProcessingMode = 17,
  kErroneousFileHandle = 18,
  kCouldNotExecute = 19,
  kInvalidInstanceIdentifierString = 20,
  kInvalidMetaModelShortname = 21
};

class ComException : public ara::core::Exception {
public:
  explicit ComException(ara::core::ErrorCode err) noexcept : Exception(std::move(err)) {}
};

class ComErrorDomain final : public ara::core::ErrorDomain {
public:
  using Errc = ComErrc;
  using Exception = ComException;
  static constexpr IdType kId = 0x8000000000000503; // implementation choice (nm ...501, tsync ...502)
  constexpr ComErrorDomain() noexcept : ErrorDomain(kId) {}
  char const *Name() const noexcept override { return "Com"; }
  char const *Message(CodeType errorCode) const noexcept override {
    switch (static_cast<ComErrc>(errorCode)) {
    case ComErrc::kServiceNotAvailable: return "Service is not available";
    case ComErrc::kMaxSamplesReached: return "Application holds more SamplePtrs than commited in Subscribe()";
    case ComErrc::kBindingFailure: return "Binding failure";
    case ComErrc::kGrantEnforcementError: return "Request was refused by Grant enforcement layer";
    case ComErrc::kPeerIsUnreachable: return "TLS handshake failed";
    case ComErrc::kFieldValueIsNotValid: return "Field Value is not valid";
    case ComErrc::kSetHandlerNotSet: return "SetHandler has not been registered";
    case ComErrc::kUnsetFailure: return "Failure has occurred when unsetting event receive handler";
    case ComErrc::kSampleAllocationFailure: return "Not enough memory to allocate a sample";
    case ComErrc::kIllegalUseOfAllocate: return "The allocation was illegally done via custom allocator";
    case ComErrc::kServiceNotOffered: return "Service not offered";
    case ComErrc::kCommunicationLinkError: return "Communication link is broken";
    case ComErrc::kNoClients: return "No clients connected";
    case ComErrc::kCommunicationStackError: return "Communication Stack Error";
    case ComErrc::kMaxSampleCountNotRealizable: return "Provided maxSampleCount not realizable";
    case ComErrc::kMaxSubscribersExceeded: return "Subscriber count is not realizable";
    case ComErrc::kWrongMethodCallProcessingMode: return "Wrong processing mode for a method call";
    case ComErrc::kErroneousFileHandle: return "File handle is erroneous";
    case ComErrc::kCouldNotExecute: return "Could not execute";
    case ComErrc::kInvalidInstanceIdentifierString: return "Invalid instance identifier string";
    case ComErrc::kInvalidMetaModelShortname: return "Invalid meta model short name";
    }
    return "Unknown error";
  }
  [[noreturn]] void ThrowAsException(ara::core::ErrorCode const &errorCode) const noexcept(false) override {
    ara::core::ThrowOrTerminate<ComException>(errorCode);
    std::terminate();
  }
};

namespace internal {
inline constexpr ComErrorDomain kComErrorDomain;
}
constexpr ara::core::ErrorDomain const &GetComDomain() noexcept { return internal::kComErrorDomain; }

constexpr ara::core::ErrorCode MakeErrorCode(ComErrc code, ara::core::ErrorDomain::SupportDataType data = 0,
                                             char const *message = nullptr) noexcept {
  return ara::core::ErrorCode(static_cast<ara::core::ErrorDomain::CodeType>(code), GetComDomain(), data, message);
}

} // namespace ara::com

#endif // ARA_COM_COM_ERROR_DOMAIN_H_
