/**
 * @file exception.hpp
 * @brief  Exception class declaration
 * @version 0.1
 * @date 2022-07-13
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_CORE_EXCEPTION_H_
#define ARA_CORE_EXCEPTION_H_

#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>


#include "ara/core/error_code.h"

namespace ara::core {

// create enum for ApplicationError
enum class ApplicationError {
  Unknown = 0,
  InvalidArgument = 1,
  InvalidState = 2,
  InvalidOperation = 3,
  InvalidData = 4,
  NotFound = 5,
  AlreadyExists = 6,
  PermissionDenied = 7,
  NotImplemented = 8,
  NotSupported = 9,
  Timeout = 10,
  OutOfMemory = 11,
  SystemError = 12,
  InternalError = 13,
  FatalError = 14,
  UserError = 15,
  UserWarning = 16,
  UserInfo = 17,
  UserDebug = 18,
  UserTrace = 19,
  UserVerbose = 20,
  UserVeryVerbose = 21,
  UserExtremelyVerbose = 22,
  UserSilent = 23,
  UserMax = 24,
};

class Exception : public std::exception {
protected:
  ErrorCode const mErrorCode;

public:
  explicit Exception(ErrorCode err) noexcept : mErrorCode(std::move(err)) {}

  ErrorCode const &Error() const noexcept { return mErrorCode; }

  /// The message of the contained ErrorCode, as required by the SWS.
  char const *what() const noexcept override { return Error().Message(); }


  std::string toString() const {
    std::stringstream ss;
    ss << "Exception: " << Error() << " (" << what() << ")";
    return ss.str();
  }
};

inline std::ostream &operator<<(std::ostream &out, Exception const &ex) {
  return (out << "Exception: " << ex.Error() << " (" << ex.what() << ")");
}

/**
 * \brief Indicates errors that shall be delivered to the client in case an
 * application error happens during method call.
 *
 * Application errors are compatible to classic AUTOSAR errors and thus consist
 * of a single byte value that indicates the error. A server method may throw
 * this exception to deliver this error code to the calling site. Currently this
 * is the only exception that is guaranteed to be transmitted to the calling
 * site. All other exceptions may lead to uncaught exceptions on server side and
 * thus cause a terminate().
 */

//  class ApplicationErrorException : public Exception
//  {
//   private :
//     ApplicationError mError {ApplicationError::Unknown};
//     std::string mServiceInterface;
//     uint8_t mErrorCode;
//  public:
//      /**
//       * \brief Default constructor that deliberately leaves the exception in
//       an undefined state.
//       *
//       */
//     ApplicationErrorException()
//         : Exception(ErrorCode(mError))
//     {
//     }


//     /**
//      * \brief All subclassess of ApplicationErrorException shall override
//      this method.
//      * The string returned by the overriden method shall have the following
//      format:
//      * "<error> in <service interface> with ErrorCode: <error code>"
//      * where:
//      * <error> is the short name of the ApplicationError element within the
//      ARXML model
//      * <service interface> is the fullyy qualified short name (short name
//      path) of the service interface wherein
//      * the Application Error has been declared.
//      * <error code> the 6 bit error code defined within the ApplicationError
//      element in decimal format.
//      * \return string representation of this ApplicationError (see above).
//      */
//     virtual const char* what() const noexcept override = 0;

//     ara::core::ErrorCode
//   MakeErrorCode(ApplicationError error, 
//                 ara::core::ErrorDomain::SupportDataType data, char const *userMessage = nullptr) const noexcept{
//     return ara::core::ErrorCode(static_cast<ara::core::ErrorDomain::CodeType>(error), 2, data, userMessage);
//                 }
//  };

} // namespace ara::core

#endif // ARA_CORE_EXCEPTION_H_
