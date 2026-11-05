#include "ara/nm/nm_error_domain.h"

namespace ara::nm {

char const *NmErrorDomain::Message(CodeType errorCode) const noexcept {
  switch (static_cast<NmErrc>(errorCode)) {
  case NmErrc::kUnknownNetwork: return "unknown network or partial network";
  case NmErrc::kNetworkExists: return "network already exists";
  case NmErrc::kNotRequested: return "network was not requested";
  case NmErrc::kBadPdu: return "malformed NM message";
  case NmErrc::kInvalidConfig: return "invalid NM configuration";
  }
  return "unknown NM error";
}

void NmErrorDomain::ThrowAsException(ara::core::ErrorCode const &errorCode) const noexcept(false) {
  ara::core::ThrowOrTerminate<NmException>(errorCode);
  std::terminate();
}

} // namespace ara::nm
