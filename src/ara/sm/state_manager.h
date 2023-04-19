#ifndef ARA_SM_STATE_MANAGER_H
#define ARA_SM_STATE_MANAGER_H

#include <map>
#include <vector>


#include "ara/exec/function_group.h"
#include "ara/exec/function_group_state.h"
#include "ara/exec/state_client.h"

namespace ara::sm {

using FunctionGroup = ara::exec::FunctionGroup;
using FunctionGroupState = ara::exec::FunctionGroupState;

class StateManager {

public:
  /** Defining the needed funcion groups **/
  static FunctionGroup MachineFG; // The machine Function Group
  static std::vector<FunctionGroupState> MachineStates; // The machine States

  static std::vector<FunctionGroup, std::vector<FunctionGroupState>>
      FunctionGroups; // The Other function groups

  /** Execution Manager  connectivity API instance **/
  static ara::exec::StateClient EM;

  static void init(void);

}; // class StateManager

} // namespace ara::sm

#endif // ARA_SM_STATE_MANAGER_H