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
#include "functional"
#include <iostream>
#include <string.h>
#include <unistd.h>
#include <variant>

namespace ara::sm::update_request::skeleton {

/// class update_request_Skeleton
class update_request_Skeleton
    : public ara::com::proxy_skeleton::skeleton::ServiceSkeleton {

public:
  static bool activeSession;

  enum class UpdateStatus {
    unactive, // initial state or after calling StopUpdateSession()
    acquired, // RequestUpdateSession() called and succeeded
    prepared, // PrepareUpdate() called and succeeded
    verified  // VerifyUpdate() called and succeeded
  };

  update_request_Skeleton(
      ara::com::InstanceIdentifier instance,
      ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle
          skeleton_handle)
      : ara::com::proxy_skeleton::skeleton::ServiceSkeleton(serviceid, instance,
                                                            skeleton_handle)

  {}

  /**
   * @brief Requests a reset of the machine. Before the reset is performed all
   * information within the machine shall be persisted. Request will be rejected
   * when RequestUpdateSession was not called successfully before.
   *
   * @return std::future<ara::sm::update_request::ResetMachineOutput>
   *    Application Errors : kRejected  (Requested operation was rejected due to
   * State Managements/machines proxy_skeleton state)
   */
  std::future<ara::sm::update_request::ResetMachineOutput> ResetMachine() {
    return std::async(std::launch::async, [this]() {
      ara::sm::update_request::ResetMachineOutput result = std::monostate();
      // resetting the machine will be rejected in case
      // - There is an active session
      // - The RequestUpdateSession() was not called successfully
      if ((activeSession == true) ||
          (update_status == UpdateStatus::unactive)) {
        result = ara::sm::errorDomains::kRejected;
      } else {
        /**
        ara::exec::FunctionGroupState state;
        StateManager::EM.SetState();
        StateManager::MachineFG.state = "Restart";
        */
      }
      return result;
    });
  }

  /**
   * @brief Has to be called by Update And Configuration Management once the
   update is finished to let State Management know that the update is done and
   the Machine is in a stable state. Request will be rejected when
   RequestUpdateSession was not called successfully before.
   *
   * @return std::future<void>
   *        Application Errors : kRejected  (Requested operation was rejected
   due to State Managements/machines proxy_skeleton state)
   */
  std::future<StopUpdateSessionOutput> StopUpdateSession() {
    return std::async(std::launch::async, [this]() {
      StopUpdateSessionOutput result = std::monostate();
      if (update_status == UpdateStatus::unactive) {
        // RequestUpdateSession() was not called successfully
        result = ara::sm::errorDomains::kRejected;
      } else {
        // closing the active session
        activeSession = false;
        update_status = UpdateStatus::unactive;
      }
      return result;
    });
  }

  /**
   * @brief Has to be called by Update And Configuration Management once it has
   to start interaction with State Management. State Management might decline
   this request when machine is not in a state to be updated.
   *
   * @return std::future<ara::sm::update_request::StartUpdateSessionOutput>
   *
   * Application Errors :
   *        - kRejected  (Requested operation was rejected due to State
   Managements/machines proxy_skeleton state)
   *        - kNotAllowedMultipleUpdateSessions (Request for new session was
   rejected as only single active (update) session is allowed)
   */
  std::future<ara::sm::update_request::RequestUpdateSessionOutput>
  RequestUpdateSession() {
    return std::async(std::launch::async, [this]() {
      ara::sm::update_request::RequestUpdateSessionOutput result =
          std::monostate();
      if (activeSession == true) {
        // there is already an active session
        result = ara::sm::errorDomains::kNotAllowedMultipleUpdateSessions;
      } else {
        // chekc if the state of the machine is running or not
        // if not running, return kRejected, The Machine current status do not
        // accept session activation else opening a new session
        //
        activeSession = true;
        update_status = UpdateStatus::acquired;
      }

      return result;
    });
  }

  /**
   * @brief Has to be called by Update And Configuration Management after State
   Management allowed to update. State Management will decline this request when
   RequestUpdateSession was not called before successfully.
   *
   * @param [in] functionGroupList  The list of FunctionGroups within the
   SoftwareCluster to be prepared to be updated.
   * @return std::future<ara::sm::update_request::PrepareUpdateOutput>
   *            Application Errors :
   *                - kRejected  (Requested operation was rejected due to State
   Managements/machines proxy_skeleton state)
   *                - kPrepareFailed (Preparation step of update failed.)
   */
  std::future<ara::sm::update_request::PrepareUpdateOutput>
  PrepareUpdate(FunctionGroupListType const& functionGroupList) {
    return std::async(std::launch::async, [this, &functionGroupList]() {
      ara::sm::update_request::PrepareUpdateOutput result = std::monostate();
      if (update_status == UpdateStatus::acquired) {
        for (auto const &functionGroup : functionGroupList) {
          // set the state of these function groups to "off" ans send the state
          // to the EM
        }
      } else {
        result = ara::sm::errorDomains::kRejected;
      }
      return result;
    });
  }

  /**
   * @brief Has to be called by Update And Configuration Management after State
   Management allowed to update and the update preparation has been done. State
   Management will decline this request when Prepare Update was not called
   before successfully.
   *
   * @param [in] functionGroupList The list of FunctionGroups within the
   SoftwareCluster to be verified
   * @return std::future<ara::sm::update_request::VerifyUpdateOutput>
   *         Application Errors :
   *                    - kRejected  (Requested operation was rejected due to
   State Managements/machines proxy_skeleton state)
   *                    - kVerifyFailed (Verification step of update failed.)
   */
  std::future<ara::sm::update_request::VerifyUpdateOutput>
  VerifyUpdate(FunctionGroupListType functionGroupList);

  /**
   * @brief Has to be called by Update And Configuration Management after State
   * Management allowed to update.
   *
   * @param [in] functionGroupList The list of FunctionGroups within the
   * SoftwareCluster to be prepared to roll back.
   * @return std::future<ara::sm::update_request::PrepareRollbackOutput>
   *            Application Errors:
   *                - kRejected  (Requested operation was rejected due to State
   * Managements/machines proxy_skeleton state)
   *               - kRollbackFailed (Rollback step of update failed.)
   */
  std::future<ara::sm::update_request::PrepareRollbackOutput>
  PrepareRollback(FunctionGroupListType functionGroupList);

  /**
   * @brief Dispatches the method call to the appropriate method
   *
   * @param message
   * @param cserver
   */
  void method_dispatch(ara::com::SOMEIP_MESSAGE::Message const &message,
                       Socket &cserver) {
    int methodID = message.MessageId().method_id;
    std::cout << "\t[SERVER] Dispatch " << methodID << std::endl;
    switch (methodID) {
    case 0:
      HandleCall(*this, &update_request_Skeleton::ResetMachine, message,
                 cserver);
      break;
    case 1:
      HandleCall(*this, &update_request_Skeleton::StopUpdateSession, message,
                 cserver);
      break;
    case 2:
      HandleCall(*this, &update_request_Skeleton::RequestUpdateSession, message,
                 cserver);
      break;
    case 3:
      HandleCall(*this, &update_request_Skeleton::PrepareUpdate, message,
                 cserver);
      break;
    case 4:
      HandleCall(*this, &update_request_Skeleton::VerifyUpdate, message,
                 cserver);
      break;
    case 5:
      HandleCall(*this, &update_request_Skeleton::PrepareRollback, message,
                 cserver);
      break;
    default:
      break;
    }
  }

private:
  struct sockaddr_in cliaddr;
  ara::com::proxy_skeleton::ServiceId serviceid{43};
  UpdateStatus update_status{UpdateStatus::unactive};
};

} // namespace ara::sm::update_request::skeleton
#endif // ARA_SM_UPDATEREQUEST_SKELETON_H_