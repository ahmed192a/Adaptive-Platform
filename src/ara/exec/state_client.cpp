/**
 * @file state_client.cpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Implementation of the StateClient class
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/exec/state_client.h"
#include "errno.h"
#include "sys/stat.h"
#include "sys/types.h"
#include "unistd.h"
#include <fcntl.h>
#include <future>
#include <string>
#include <iostream>
#include <variant>

namespace ara::exec {
StateClient::StateClient() noexcept {

  // get file discreptor
  fd = open(fifo_l.c_str(), O_WRONLY);
}

StateClient::~StateClient() noexcept {
  // close file descriptor
  if (fd >= 0) close(fd);
}

std::future<std::variant<std::monostate, ara::exec::ExecErrc>>
StateClient::SetState(FunctionGroupState const &state) const noexcept {
  std::future<std::variant<std::monostate, ara::exec::ExecErrc>> _future;

  // The request is copied: the caller's FunctionGroupState may be gone before the task runs.
  _future = std::async(std::launch::async, [this, msg = state.get_FGname() + "/" + state.get_states()]() {
    std::variant<std::monostate, ara::exec::ExecErrc> _variant = std::monostate();
    if (fd < 0 || write(fd, msg.data(), msg.size()) != static_cast<ssize_t>(msg.size())) {
      _variant= ara::exec::ExecErrc::kCommunicationError;
      return _variant;
    }
    return _variant;
  });

  return _future;
}

std::future<std::variant<std::monostate, ara::exec::ExecErrc>>
StateClient::GetInitialMachineStateTransitionResult() const noexcept {
  // No initial machine-state transition is tracked yet: report success immediately
  // instead of returning an invalid future whose get() would throw.
  std::promise<std::variant<std::monostate, ara::exec::ExecErrc>> p;
  p.set_value(std::monostate());
  return p.get_future();
}

} // namespace ara::exec