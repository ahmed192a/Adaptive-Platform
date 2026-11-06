/**
 * @file execution_error_event.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Definition of execution error event struct
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_EXEC_EXECUTION_ERROR_EVENT_H_
#define ARA_EXEC_EXECUTION_ERROR_EVENT_H_

#include "ara/exec/function_group.h"
#include <cstdint>


namespace ara::exec {
using ExecutionError = std::uint32_t; /*!<  typedef for ExecutionError */

/// SWS_EM_02543: value used for an abnormal termination when no execution error is configured.
inline constexpr ExecutionError kDefaultExecutionError = 1;

/**
 * @brief  ExecutionErrorEvent struct to represent an execution error event
 * which happens in a Function Group
 *
 *
 */
struct ExecutionErrorEvent final {
  ExecutionError executionError; /*!<  The execution error which happened in the
                                    Function Group */
  FunctionGroup functionGroup;   /*!<  The Function Group in which the execution
                                    error happened */
};

} // namespace ara::exec

#endif // ARA_EXEC_EXECUTION_ERROR_EVENT_H_
