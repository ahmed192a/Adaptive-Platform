/**
 * @file exec_error_domain.cpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Implementation of the ExecErrorDomain class which is responsible for
 * handling all Excuation Management exceptions
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/exec/exec_error_domain.h"
#include <exception>
#include <iostream>
#include <memory>

namespace ara {
namespace exec {
/*---------------------------------ExecException---------------------------------*/
ExecException::ExecException(ara::core::ErrorCode errorCode) noexcept
    : ara::core::Exception(std::move(errorCode)) {}

const ara::core::ErrorDomain &ExecException::GetExecErrorDomain() const noexcept {
  return this->mErrorCode.Domain();
}

ara::core::ErrorCode ExecException::MakeErrorCode(
    ara::exec::ExecErrc code,
    ara::core::ErrorDomain::SupportDataType data) const noexcept {
  return ara::core::ErrorCode(
      static_cast<ara::core::ErrorDomain::CodeType>(code),
      this->mErrorCode.Domain(), data);
}

/*---------------------------------ExecErrorDomain---------------------------------*/

char const *ExecErrorDomain::Name() const noexcept { return "Exec"; }

char const *ExecErrorDomain::Message(CodeType errorCode) const noexcept {
  // Must return storage that outlives the call: only string literals are used.
  switch (static_cast<ExecErrc>(errorCode)) {
  case ExecErrc::kGeneralError:
    return "Some unspecified error occurred";
  case ExecErrc::kInvalidArguments:
    return "Invalid argument was passed";
  case ExecErrc::kCommunicationError:
    return "Communication error occurred";
  case ExecErrc::kMetaModelError:
    return "Wrong meta model identifier passed to a function";
  case ExecErrc::kCancelled:
    return "Transition to the requested Function Group state was cancelled by a newer request";
  case ExecErrc::kFailed:
    return "Transition to the requested Function Group state failed";
  case ExecErrc::kFailedUnexpectedTerminationOnExit:
    return "Unexpected termination during transition in a process of the previous Function Group state";
  case ExecErrc::kFailedUnexpectedTerminationOnEnter:
    return "Unexpected termination during transition in a process of the target Function Group state";
  case ExecErrc::kInvalidTransition:
    return "Transition invalid";
  case ExecErrc::kAlreadyInState:
    return "Function Group is already in the requested state";
  case ExecErrc::kInTransitionToSameState:
    return "Transition to the requested Function Group state is already in progress";
  case ExecErrc::kNoTimeStamp:
    return "DeterministicClient time stamp information is not available";
  }
  return "Unknown execution management error";
}

void ExecErrorDomain::ThrowAsException(
    ara::core::ErrorCode const &errorCode) const noexcept(false) {
  ara::core::ThrowOrTerminate<ExecException>(errorCode);
  std::terminate(); // unreachable; satisfies [[noreturn]]
}
} // namespace exec
} // namespace ara