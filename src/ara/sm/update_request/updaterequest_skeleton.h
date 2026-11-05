/**
 * @file updaterequest_skeleton.h
 * @author  Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-02-23
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_SM_UPDATEREQUEST_SKELETON_H_
#define ARA_SM_UPDATEREQUEST_SKELETON_H_

#include "ara/com/ipc/server/socket_Server.h"
#include "ara/com/proxy_skeleton/definitions.h"
#include "ara/com/proxy_skeleton/skeleton/service_skeleton.h"
#include "ara/sm/update_request/update_request_return_types.h"
#include "ara/sm/update_request/update_session.h"
#include <functional>
#include <future>
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <variant>

namespace ara::sm::update_request::skeleton {

/// class update_request_Skeleton
/// The session rules live in UpdateSession (unit tested); this class adapts
/// them to the SOME/IP service interface.
class update_request_Skeleton
    : public ara::com::proxy_skeleton::skeleton::ServiceSkeleton {

public:
  update_request_Skeleton(
      ara::com::InstanceIdentifier instance,
      ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle
          skeleton_handle)
      : ara::com::proxy_skeleton::skeleton::ServiceSkeleton(serviceid, instance,
                                                            skeleton_handle)
  {}

  /// Installed by State Management: performs the actual machine restart.
  /// Returning false makes ResetMachine fail with kOperationRejected.
  void SetResetHandler(std::function<bool()> h) { resetHandler_ = std::move(h); }

  UpdateSession &Session() { return session_; }

  /**
   * @brief Requests a reset of the machine [SWS_SM_00202]. Rejected with
   * kOperationRejected when no update session is active.
   */
  std::future<ara::sm::update_request::ResetMachineOutput> ResetMachine() {
    return std::async(std::launch::async, [this]() {
      ara::sm::update_request::ResetMachineOutput result = std::monostate();
      if (auto e = session_.ResetMachine()) return Err(*e);
      if (resetHandler_ && !resetHandler_()) return Err(ara::sm::errorDomains::kOperationRejected);
      return result;
    });
  }

  /// [SWS_SM_00205]
  std::future<StopUpdateSessionOutput> StopUpdateSession() {
    return std::async(std::launch::async, [this]() -> StopUpdateSessionOutput {
      if (auto e = session_.StopUpdateSession()) return *e;
      return std::monostate();
    });
  }

  /// [SWS_SM_00203] kNotAllowedMultipleUpdateSessions when one is active.
  std::future<ara::sm::update_request::RequestUpdateSessionOutput>
  RequestUpdateSession() {
    return std::async(std::launch::async, [this]() -> RequestUpdateSessionOutput {
      if (auto e = session_.RequestUpdateSession()) return *e;
      return std::monostate();
    });
  }

  /// [SWS_SM_00206]
  std::future<ara::sm::update_request::PrepareUpdateOutput>
  PrepareUpdate(FunctionGroupListType functionGroupList) {
    return std::async(std::launch::async, [this, l = std::move(functionGroupList)]() -> PrepareUpdateOutput {
      (void)l; // function-group preparation is delegated to Execution Management by the caller
      if (auto e = session_.PrepareUpdate()) return *e;
      return std::monostate();
    });
  }

  /// [SWS_SM_00207]
  std::future<ara::sm::update_request::VerifyUpdateOutput>
  VerifyUpdate(FunctionGroupListType functionGroupList) {
    return std::async(std::launch::async, [this, l = std::move(functionGroupList)]() -> VerifyUpdateOutput {
      (void)l;
      if (auto e = session_.VerifyUpdate()) return *e;
      return std::monostate();
    });
  }

  /// [SWS_SM_00208]
  std::future<ara::sm::update_request::PrepareRollbackOutput>
  PrepareRollback(FunctionGroupListType functionGroupList) {
    return std::async(std::launch::async, [this, l = std::move(functionGroupList)]() -> PrepareRollbackOutput {
      (void)l;
      if (auto e = session_.PrepareRollback()) return *e;
      return std::monostate();
    });
  }

  void method_dispatch(ara::com::SOMEIP_MESSAGE::Message const &message,
                       Socket &cserver) {
    int methodID = message.MessageId().method_id;
    std::cout << "\t[SERVER] Dispatch " << methodID << std::endl;
    switch (methodID) {
    case 0:
      HandleCall(*this, &update_request_Skeleton::ResetMachine, message, cserver);
      break;
    case 1:
      HandleCall(*this, &update_request_Skeleton::StopUpdateSession, message, cserver);
      break;
    case 2:
      HandleCall(*this, &update_request_Skeleton::RequestUpdateSession, message, cserver);
      break;
    case 3:
      HandleCall(*this, &update_request_Skeleton::PrepareUpdate, message, cserver);
      break;
    case 4:
      HandleCall(*this, &update_request_Skeleton::VerifyUpdate, message, cserver);
      break;
    case 5:
      HandleCall(*this, &update_request_Skeleton::PrepareRollback, message, cserver);
      break;
    default:
      break;
    }
  }

private:
  static ara::sm::update_request::ResetMachineOutput Err(ara::sm::errorDomains e) { return e; }

  struct sockaddr_in cliaddr;
  ara::com::proxy_skeleton::ServiceId serviceid{43};
  UpdateSession session_;
  std::function<bool()> resetHandler_;
};

} // namespace ara::sm::update_request::skeleton

#endif // ARA_SM_UPDATEREQUEST_SKELETON_H_
