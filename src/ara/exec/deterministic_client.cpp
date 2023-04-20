/**
 * @file deterministic_client.cpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Implementation of the DeterministicClient class
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/exec/deterministic_client.h"
#include "errno.h"
#include "sys/stat.h"
#include "sys/types.h"
#include "unistd.h"
#include "utility"
#include <fcntl.h>
#include <iterator>
#include <vector>

namespace ara::exec {
DeterministicClient::DeterministicClient() noexcept {
  // opens the Execution Management communication channel to access a wait point
  // for cyclic execution
  if (mkfifo(fifo_l.c_str(), 0777) == -1 && errno != EEXIST) {

    // TO DO
    // Log Error : coundn't create fifo
  }
  // get file discreptor
  fd = open(fifo_l.c_str(), O_RDONLY);
}
DeterministicClient::~DeterministicClient() noexcept {
  // close file descriptor
  close(fd);
}

template <typename ValueType, typename ContainerObj>
void DeterministicClient::RunWorkerPool(WorkerRunnable<ValueType> &runnableObj,
                                        ContainerObj &container) const noexcept {

  // TO DO : This part is sequential, it needs to be converted to parallel with
  // fork() - exec()
  int count = 0;
  int pid;
  auto c = std::begin(container);
  while (c != std::end(container)) {
    pid = fork();
    if (pid == 0) {
      // execute each element in Container
      runnableObj.Run(*c++, workers[count]);
      count++;
      return;
    }

    count %= workers.size();
  }
}

ActivationReturnType DeterministicClient::WaitForActivation() noexcept {
  // Blocks and returns with a process control value when the next activation is
  // triggered by the Runtime
  ara::exec::ActivationReturnType state;
  // read from the fifo
  read(fd, &state, sizeof(state));
  if (state == ActivationReturnType::kRun)
    DeterministicClient::Activated = std::chrono::system_clock::now();
  return state;
}
uint64_t DeterministicClient::GetRandom() const noexcept { return lrand48(); }
void DeterministicClient::SetRandomSeed(uint64_t seed) const noexcept {
  srand48(seed);
}

DeterministicClient::TimeStamp
DeterministicClient::GetActivationTime() const noexcept {
  return Activated;
}

DeterministicClient::TimeStamp
DeterministicClient::GetNextActivationTime() const noexcept {
  return Activated + std::chrono::milliseconds(1);
}

} // namespace ara::exec
