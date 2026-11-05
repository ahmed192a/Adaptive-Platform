#include "ara/log/logging_types.h"

namespace ara::log {

char const *LogErrorDomain::Message(CodeType errorCode) const noexcept {
  switch (static_cast<LogErrc>(errorCode)) {
  case LogErrc::kInvalidArgument: return "invalid argument";
  case LogErrc::kNotInitialized: return "logging not initialized";
  case LogErrc::kSinkFailure: return "log sink failure";
  }
  return "unknown logging error";
}

void LogErrorDomain::ThrowAsException(ara::core::ErrorCode const &errorCode) const noexcept(false) {
  ara::core::ThrowOrTerminate<LogException>(errorCode);
  std::terminate();
}

} // namespace ara::log
