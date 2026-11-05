#include "ara/tsync/tsync_error_domain.h"

namespace ara::tsync {

char const *TsyncErrorDomain::Message(CodeType errorCode) const noexcept {
  switch (static_cast<TsyncErrc>(errorCode)) {
  case TsyncErrc::kNotSynchronized: return "time base not synchronized";
  case TsyncErrc::kInvalidArgument: return "invalid argument";
  case TsyncErrc::kAlreadyExists: return "time base already exists";
  case TsyncErrc::kNotFound: return "time base not found";
  }
  return "unknown time-synchronization error";
}

void TsyncErrorDomain::ThrowAsException(ara::core::ErrorCode const &errorCode) const
    noexcept(false) {
  ara::core::ThrowOrTerminate<TsyncException>(errorCode);
  std::terminate();
}

} // namespace ara::tsync
