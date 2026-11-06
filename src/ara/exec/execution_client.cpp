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
#include <atomic>
#include <csignal>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


namespace ara::exec {
namespace {
// ---- SIGTERM -> termination handler (SWS_EM_02577) -----------------------------------------
// The signal handler only writes one byte to a self-pipe; a background thread runs the user handler.
std::atomic<bool> gSlotTaken{false};
int gPipe[2] = {-1, -1};
std::thread gSigThread;
std::function<void()> gTerminationHandler;
struct sigaction gOldSigterm;

void OnSigterm(int) {
  char b = 1;
  ssize_t const r = ::write(gPipe[1], &b, 1);
  (void)r;
}

bool InstallTerminationHandler(std::function<void()> handler) {
  if (::pipe(gPipe) != 0) return false;
  gTerminationHandler = std::move(handler);
  struct sigaction sa;
  sa.sa_handler = OnSigterm;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  if (::sigaction(SIGTERM, &sa, &gOldSigterm) != 0) {
    ::close(gPipe[0]);
    ::close(gPipe[1]);
    gPipe[0] = gPipe[1] = -1;
    return false;
  }
  gSigThread = std::thread([] {
    char b = 0;
    ssize_t n;
    do {
      n = ::read(gPipe[0], &b, 1);
    } while (n < 0 && errno == EINTR);
    if (n == 1 && b == 1 && gTerminationHandler) gTerminationHandler(); // b==0: shut down by the destructor
  });
  return true;
}

void RemoveTerminationHandler() {
  if (gPipe[1] >= 0) {
    char b = 0; // wake the thread without invoking the handler
    ssize_t const r = ::write(gPipe[1], &b, 1);
    (void)r;
  }
  if (gSigThread.joinable()) gSigThread.join();
  ::sigaction(SIGTERM, &gOldSigterm, nullptr);
  if (gPipe[0] >= 0) ::close(gPipe[0]);
  if (gPipe[1] >= 0) ::close(gPipe[1]);
  gPipe[0] = gPipe[1] = -1;
  gTerminationHandler = nullptr;
}
} // namespace

ara::core::Result<ExecutionClient> ExecutionClient::Create(std::function<void()> terminationHandler) noexcept {
  if (!terminationHandler) {
    return ara::core::Result<ExecutionClient>::FromError(MakeErrorCode(ExecErrc::kInvalidArguments));
  }
  bool expected = false;
  if (!gSlotTaken.compare_exchange_strong(expected, true)) {
    return ara::core::Result<ExecutionClient>::FromError(MakeErrorCode(ExecErrc::kGeneralError));
  }
  if (!InstallTerminationHandler(std::move(terminationHandler))) {
    gSlotTaken.store(false);
    return ara::core::Result<ExecutionClient>::FromError(MakeErrorCode(ExecErrc::kGeneralError));
  }
  ExecutionClient client;
  client.mOwnsSlot = true;
  return ara::core::Result<ExecutionClient>::FromValue(std::move(client));
}

ExecutionClient::ExecutionClient(ExecutionClient &&other) noexcept
    : fd(other.fd), mRunningReported(other.mRunningReported.load()), mOwnsSlot(other.mOwnsSlot) {
  other.fd = -1;
  other.mOwnsSlot = false;
}

ExecutionClient &ExecutionClient::operator=(ExecutionClient &&other) noexcept {
  if (this != &other) {
    if (fd >= 0) ::close(fd);
    if (mOwnsSlot) {
      RemoveTerminationHandler();
      gSlotTaken.store(false);
    }
    fd = other.fd;
    mRunningReported.store(other.mRunningReported.load());
    mOwnsSlot = other.mOwnsSlot;
    other.fd = -1;
    other.mOwnsSlot = false;
  }
  return *this;
}

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
  if (mOwnsSlot) {
    RemoveTerminationHandler();
    gSlotTaken.store(false);
  }
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

ara::core::Result<void> ExecutionClient::ReportExecutionStateResult(ExecutionState state) const noexcept {
  auto const r = ReportExecutionState(state);
  if (std::holds_alternative<ExecErrc>(r)) {
    return ara::core::Result<void>::FromError(MakeErrorCode(std::get<ExecErrc>(r)));
  }
  return ara::core::Result<void>::FromValue();
}

} // namespace ara::exec
