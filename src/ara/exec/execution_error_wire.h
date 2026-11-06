/**
 * @file execution_error_wire.h
 * @brief Wire format of execution errors from Execution Management to State Management
 *        (SWS_EM_01309 / SWS_EM_02543) plus the abnormal-termination decision, shared by the EM
 *        application and ara::exec::StateClient so both sides agree and can be unit tested.
 *
 * One newline-terminated line per event: "<function group>/<execution error>\n".
 */
#ifndef ARA_EXEC_EXECUTION_ERROR_WIRE_H_
#define ARA_EXEC_EXECUTION_ERROR_WIRE_H_

#include "ara/exec/execution_error_event.h"
#include <optional>
#include <string>
#include <sys/wait.h>
#include <utility>

namespace ara::exec {

/// Formats one execution-error line.
inline std::string FormatExecutionErrorLine(std::string const &functionGroup, ExecutionError error) {
  return functionGroup + "/" + std::to_string(error) + "\n";
}

/// Parses a line WITHOUT its trailing newline; nullopt when malformed.
inline std::optional<std::pair<std::string, ExecutionError>> ParseExecutionErrorLine(std::string const &line) {
  auto const slash = line.find('/');
  if (slash == std::string::npos || slash == 0 || slash + 1 >= line.size()) return std::nullopt;
  for (std::size_t i = slash + 1; i < line.size(); ++i) {
    if (line[i] < '0' || line[i] > '9') return std::nullopt;
  }
  try {
    return std::make_pair(line.substr(0, slash), static_cast<ExecutionError>(std::stoul(line.substr(slash + 1))));
  } catch (...) {
    return std::nullopt; // out of range
  }
}

/// A process that exits with status 0 on its own terminated normally; a non-zero exit or a signal is abnormal.
inline bool IsAbnormalTermination(int waitStatus) {
  return !(WIFEXITED(waitStatus) && WEXITSTATUS(waitStatus) == 0);
}

} // namespace ara::exec

#endif // ARA_EXEC_EXECUTION_ERROR_WIRE_H_
