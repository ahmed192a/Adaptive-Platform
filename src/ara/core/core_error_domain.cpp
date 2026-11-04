#include "ara/core/core_error_domain.h"

namespace ara::core {

char const *CoreErrorDomain::Message(CodeType errorCode) const noexcept {
  switch (static_cast<CoreErrc>(errorCode)) {
  case CoreErrc::kInvalidArgument:
    return "an invalid argument was passed to a function";
  case CoreErrc::kInvalidMetaModelShortname:
    return "given string is not a valid model element shortname";
  case CoreErrc::kInvalidMetaModelPath:
    return "missing or invalid path to model element";
  case CoreErrc::kAlreadyInitialized:
    return "ara::core::Initialize() was already called";
  case CoreErrc::kNotInitialized:
    return "ara::core::Initialize() was not called";
  case CoreErrc::kInitializationFailed:
    return "a functional cluster failed to initialize";
  }
  return "unknown core error";
}

void CoreErrorDomain::ThrowAsException(ErrorCode const &errorCode) const noexcept(false) {
  ThrowOrTerminate<CoreException>(errorCode);
  std::terminate(); // not reached; keeps [[noreturn]] honest when ARA_NO_EXCEPTIONS is set
}

} // namespace ara::core
