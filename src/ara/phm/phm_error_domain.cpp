#include "ara/phm/phm_error_domain.h"

namespace ara::phm {

char const *PhmErrorDomain::Message(CodeType errorCode) const noexcept {
  switch (static_cast<PhmErrc>(errorCode)) {
  case PhmErrc::kSMCanNotHandleRecovery: return "state management cannot handle the recovery";
  case PhmErrc::kInvalidConfiguration: return "invalid PHM configuration";
  case PhmErrc::kUnknownEntity: return "supervised entity not configured";
  case PhmErrc::kUnknownSupervision: return "supervision not configured";
  case PhmErrc::kAlreadyOffered: return "recovery action already offered";
  }
  return "unknown PHM error";
}

void PhmErrorDomain::ThrowAsException(ara::core::ErrorCode const &errorCode) const noexcept(false) {
  ara::core::ThrowOrTerminate<PhmException>(errorCode);
  std::terminate();
}

} // namespace ara::phm
