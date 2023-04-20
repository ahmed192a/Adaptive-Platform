#ifndef ARA_SM_STATE_MANAGER_H
#define ARA_SM_STATE_MANAGER_H

#include <map>
#include <vector>

#include "ara/exec/function_group.h"
#include "ara/exec/function_group_state.h"
#include "ara/exec/state_client.h"

#include "ara/com/proxy_skeleton/definitions.h"
#include "ara/exec/execution_client.h"
#include "ara/sm/triggerin/triggerin_skeleton.h"
#include "ara/sm/triggerin/triggerin_types.h"






namespace ara::sm::server {

using FunctionGroup = ara::exec::FunctionGroup;
using FunctionGroupState = ara::exec::FunctionGroupState;
constexpr auto SERVER_PORT = 5375;
constexpr auto SD_PORT = 1690;
constexpr auto T_IN_UCM_INSTANCE_ID = 1;
constexpr auto T_IN_OTA_INSTANCE_ID = 2;
constexpr auto MAX_QUEUE_CLIENTS = 3;

class StateManager {

enum class State {
    STATE_UNKNOWN,
    STATE_INITIAL,
    STATE_RUNNING,
    STATE_UPDATE,
    STATE_STOPPED,
    STATE_ERROR
};
State SM_State{State::STATE_UNKNOWN};


  /**    UCM Trigger In Skeleton    **/
  // instance id of Trigger in UCM service (1)
  ara::com::InstanceIdentifier instance{T_IN_UCM_INSTANCE_ID};
  // skeleton handle for constructor Trigger in UCM service
  ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle
      triggerin_handle{SERVER_PORT, SD_PORT};
  // Create shared pointer object of Trigger in UCM service skeleton
  std::shared_ptr<ara::sm::triggerin::skeleton::Trigger_In_skeleton<
      ara::sm::triggerin::UCM_State>>
      UCM_triggerin_skeleton_ptr =
          std::make_shared<ara::sm::triggerin::skeleton::Trigger_In_skeleton<
              ara::sm::triggerin::UCM_State>>(55, instance, triggerin_handle);

  /**     OTA Trigger In Skeleton    **/
  // instance id of Trigger in OTA service (2)
  ara::com::InstanceIdentifier instance_ota{T_IN_OTA_INSTANCE_ID};
  // skeleton handle for constructor Trigger in OTA service
  ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle
      triggerin_handle_ota{SERVER_PORT, SD_PORT};
  // Create shared pointer object of Trigger in OTA service skeleton
  std::shared_ptr<ara::sm::triggerin::skeleton::Trigger_In_skeleton<
      ara::sm::triggerin::OTA_State>>
      OTA_triggerin_skeleton_ptr =
          std::make_shared<ara::sm::triggerin::skeleton::Trigger_In_skeleton<
              ara::sm::triggerin::OTA_State>>(56, instance_ota,
                                              triggerin_handle_ota);

  CServer server_main_socket{SOCK_STREAM};   // socket for methods handle (TCP)
  ara::exec::ExecutionClient client;
  ara::exec::StateClient state_client;
public:
  static bool isRunning;
  /**
  // Defining the needed funcion groups 
  static FunctionGroup MachineFG; // The machine Function Group
  static std::vector<FunctionGroupState> MachineStates; // The machine States

  static std::vector<FunctionGroup, std::vector<FunctionGroupState>>
      FunctionGroups; // The Other function groups

  // Execution Manager  connectivity API instance
  static ara::exec::StateClient EM;

  
  */
  static void init(void); 

  StateManager(){
  std::cout << "\n[SM] State Management intitializing..." << std::endl;
  // print SM PID
  std::cout << "\t[SM] PID: " << getpid() << std::endl;
  // set hamdle function for user signal

//   client.ReportExecutionState(
//       ara::exec::ExecutionState::kRunning); // report execution state to the
//                                             // execution server

  std::cout << std::endl << "[SM]creating execution client " << std::endl;
}

  void run();


  void shutdownSeq();

  

}; // class StateManager

} // namespace ara::sm

#endif // ARA_SM_STATE_MANAGER_H