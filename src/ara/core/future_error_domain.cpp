#include "ara/core/future_error_domain.h"

namespace ara::core {

char const *FutureErrorDomain::Message(CodeType errorCode) const noexcept {
  switch (static_cast<FutureErrc>(errorCode)) {
  case FutureErrc::broken_promise:
    return "the asynchronous task abandoned its shared state";
  case FutureErrc::future_already_retrieved:
    return "the contained shared state was already accessed by a Future";
  case FutureErrc::promise_already_satisfied:
    return "the contained shared state is already ready";
  case FutureErrc::no_state:
    return "the Future or Promise has no shared state";
  }
  return "unknown future error";
}

void FutureErrorDomain::ThrowAsException(ErrorCode const &errorCode) const noexcept(false) {
  ThrowOrTerminate<FutureException>(errorCode);
  std::terminate();
}

} // namespace ara::core
