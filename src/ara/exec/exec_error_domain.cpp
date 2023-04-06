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

constexpr ExecErrorDomain::ExecErrorDomain(IdType id) noexcept
    : ara::core::ErrorDomain(id) {}

char const *ExecErrorDomain::Name() const noexcept { return "Exec"; }

char const *ExecErrorDomain::Message(CodeType errorCode) const noexcept {
  // return "CodeType of error message: " + std::to_string(errorCode) as char
  // const* std::string message = "CodeType of error message: " +
  // std::to_string(errorCode);
  // return message.c_str(); // return the message as a const char*
  std::string message =
      "CodeType of error message: " + std::to_string(errorCode);
  std::shared_ptr<char> msg(new char[message.size() + 1],
							std::default_delete<char[]>());
  return msg.get();                 // return pointer to message
}

void ExecErrorDomain::ThrowAsException(
    ara::core::ErrorCode const &errorCode) const noexcept(false) {
  ExecException exp(errorCode);
  std::string c = exp.what();
}
} // namespace exec
} // namespace ara