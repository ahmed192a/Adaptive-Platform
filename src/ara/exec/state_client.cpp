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
#include "ara/exec/execution_error_wire.h"
#include "errno.h"
#include "sys/stat.h"
#include "sys/types.h"
#include "unistd.h"
#include <fcntl.h>
#include <poll.h>
#include <chrono>
#include <future>
#include <string>
#include <iostream>
#include <variant>

namespace ara::exec {
StateClient::StateClient() noexcept {

  // get file discreptor
  fd = open(fifo_l.c_str(), O_WRONLY);
}

StateClient::StateClient(std::function<void(ExecutionErrorEvent const &)> undefinedStateCallback) noexcept
    : StateClient() {
  mUndefinedStateCallback = std::move(undefinedStateCallback);
  mErrThread = std::thread([this] { ErrorListener(); });
}

void StateClient::ErrorListener() noexcept {
  // EM keeps the write end open, so the data survives until we attach; we poll without blocking forever.
  int efd = -1;
  std::string pending;
  while (!mStop.load()) {
    if (efd < 0) {
      efd = ::open("execution_error_fifo", O_RDONLY | O_NONBLOCK);
      if (efd < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        continue;
      }
    }
    struct pollfd p{efd, POLLIN, 0};
    int const pr = ::poll(&p, 1, 50);
    if (pr <= 0) continue;
    if (p.revents & POLLIN) {
      char buf[256];
      ssize_t const n = ::read(efd, buf, sizeof(buf));
      if (n > 0) pending.append(buf, static_cast<size_t>(n));
    } else if (p.revents & (POLLHUP | POLLERR)) {
      ::close(efd);
      efd = -1;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (size_t eol; (eol = pending.find('\n')) != std::string::npos;) {
      std::string const line = pending.substr(0, eol);
      pending.erase(0, eol + 1);
      auto const parsed = ParseExecutionErrorLine(line);
      if (!parsed) continue; // malformed line
      std::string const &fg = parsed->first;
      ExecutionError const code = parsed->second;
      {
        std::lock_guard<std::mutex> lock(mErrMutex);
        mErrors[fg] = code;
      }
      if (mUndefinedStateCallback) {
        FunctionGroup::CtorToken token = {{"Function_group_name", fg}, {"Mode_declarations", nlohmann::json::array()}};
        mUndefinedStateCallback(ExecutionErrorEvent{code, FunctionGroup(std::move(token))});
      }
    }
  }
  if (efd >= 0) ::close(efd);
}

ara::core::Result<ExecutionErrorEvent> StateClient::GetExecutionError(FunctionGroup const &functionGroup) const noexcept {
  std::lock_guard<std::mutex> lock(mErrMutex);
  auto const it = mErrors.find(functionGroup.get_FGname());
  if (it == mErrors.end()) {
    return ara::core::Result<ExecutionErrorEvent>::FromError(MakeErrorCode(ExecErrc::kGeneralError));
  }
  FunctionGroup::CtorToken token = {{"Function_group_name", it->first}, {"Mode_declarations", nlohmann::json::array()}};
  return ara::core::Result<ExecutionErrorEvent>::FromValue(ExecutionErrorEvent{it->second, FunctionGroup(std::move(token))});
}

ara::core::Future<void> StateClient::SetStateFuture(FunctionGroupState const &state) const noexcept {
  ara::core::Promise<void> p;
  auto f = p.get_future();
  auto r = SetState(state).get();
  if (std::holds_alternative<ExecErrc>(r)) {
    p.SetError(MakeErrorCode(std::get<ExecErrc>(r)));
  } else {
    p.set_value();
  }
  return f;
}

ara::core::Future<void> StateClient::GetInitialMachineStateTransitionFuture() const noexcept {
  ara::core::Promise<void> p;
  auto f = p.get_future();
  auto r = GetInitialMachineStateTransitionResult().get();
  if (std::holds_alternative<ExecErrc>(r)) {
    p.SetError(MakeErrorCode(std::get<ExecErrc>(r)));
  } else {
    p.set_value();
  }
  return f;
}

StateClient::~StateClient() noexcept {
  mStop.store(true);
  if (mErrThread.joinable()) mErrThread.join();
  // close file descriptor
  if (fd >= 0) close(fd);
}

std::future<std::variant<std::monostate, ara::exec::ExecErrc>>
StateClient::SetState(FunctionGroupState const &state) const noexcept {
  std::future<std::variant<std::monostate, ara::exec::ExecErrc>> _future;

  // Messages are newline-terminated: a FIFO is a byte stream, so back-to-back requests would otherwise be
  // read as one.
  // The request is copied: the caller's FunctionGroupState may be gone before the task runs.
  _future = std::async(std::launch::async, [this, msg = state.get_FGname() + "/" + state.get_states() + "\n"]() {
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