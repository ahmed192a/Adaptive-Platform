/**
 * @file execution_client.cpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Implementation of the execution client class
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/exec/execution_client.h"
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


namespace ara::exec {
ExecutionClient::ExecutionClient() noexcept {

  // get file discreptor
  fd = open(fifo_l.c_str(), O_WRONLY);
  if (fd == -1) {
    std::cout << "[ERROR] => can't open fifo" << std::endl;
  }
}

ExecutionClient::~ExecutionClient() noexcept{
  // close file descriptor
  if (fd >= 0) close(fd);
}

std::variant<std::monostate, ExecErrc> ExecutionClient::ReportExecutionState(
    ara::exec::ExecutionState state) const noexcept {
  std::variant<std::monostate, ExecErrc> result;
  if (state == ara::exec::ExecutionState::kRunning &&
      mRunningReported.load(std::memory_order_acquire)) {
    // SWS_EM_02243: kRunning is only allowed once, from the Starting state.
    result.emplace<ExecErrc>(ExecErrc::kInvalidTransition);
    return result;
  }
  if (fd < 0 || write(fd, &state, sizeof(state)) != static_cast<ssize_t>(sizeof(state))) {
    result.emplace<ExecErrc>(ExecErrc::kCommunicationError);
  } else if (state == ara::exec::ExecutionState::kRunning) {
    mRunningReported.store(true, std::memory_order_release);
  }
  return result;
}

} // namespace ara::exec
