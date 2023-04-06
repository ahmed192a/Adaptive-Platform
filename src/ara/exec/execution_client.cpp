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
  close(fd);
}

std::variant<std::monostate, ExecErrc> ExecutionClient::ReportExecutionState(
    ara::exec::ExecutionState state) const noexcept {
  std::variant<std::monostate, ExecErrc> result;
  std::string m = "run";
  if (write(fd, &state, sizeof(state)) == -1) {
    // TO DO
    // Log Error : counldn't send the state to fifo
  }
  return result;
}

} // namespace ara::exec
