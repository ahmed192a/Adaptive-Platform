#ifndef ARA_SM_SM_ERROR_DOMAIN_H
#define ARA_SM_SM_ERROR_DOMAIN_H

#include <cstdint>

#include "ara/core/error_code.h"
#include "ara/core/error_domain.h"


namespace ara::sm {

using Error = ara::core::ErrorDomain;

enum class errorDomains : uint16_t // shall be updated to ErrorCode or
                                   // ErrorDomain **
{
  kOperationRejected = 5, // [SWS_SM_00213] operation rejected (e.g. outside an
                          // active update session). R25-11 name.
  kRejected = kOperationRejected, // legacy name, same value
  kVerifyFailed = 6,   // Verification step of update failed.
  kPrepareFailed = 7,  // Preparation step of update failed.
  kRollbackFailed = 8, // Rollback step of update failed.
  kNotAllowedMultipleUpdateSessions =
      9 // Request for new session was rejected as only single active (update)
        // session is allowed.
};

} // namespace ara::sm

#endif // ARA_SM_SM_ERROR_DOMAIN_H
