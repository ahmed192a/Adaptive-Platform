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
#include "ara/exec/function_group.h"
#include <cstdint>


namespace ara::exec {
using ExecutionError = std::uint32_t; /*!<  typedef for ExecutionError */

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
