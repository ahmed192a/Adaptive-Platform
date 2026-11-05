#include "ara/diag/diag_error_domain.h"

namespace ara::diag {

char const *DiagErrorDomain::Message(CodeType errorCode) const noexcept {
  switch (static_cast<DiagErrc>(errorCode)) {
  case DiagErrc::kAlreadyOffered: return "already offered";
  case DiagErrc::kConflict: return "conflict";
  case DiagErrc::kNotOffered: return "not offered";
  case DiagErrc::kInvalidArgument: return "invalid argument";
  case DiagErrc::kMalformedRequest: return "malformed request";
  case DiagErrc::kRejected: return "rejected";
  }
  return "unknown diagnostics error";
}

void DiagErrorDomain::ThrowAsException(ara::core::ErrorCode const &errorCode) const
    noexcept(false) {
  ara::core::ThrowOrTerminate<DiagException>(errorCode);
  std::terminate();
}

} // namespace ara::diag
