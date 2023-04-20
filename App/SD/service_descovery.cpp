/**
 * @file service_descovery.cpp
 * @version 0.1
 * @date 2022-07-09
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "sd.h"

void handle_sigterm(int sig);

serviceRegistry * instance = nullptr;

/**
 * @brief main function of service discovery
 *
 * @return int
 */
int main() {
  signal(SIGTERM, handle_sigterm);


  serviceRegistry sd;
  instance = &sd;
  sd.run();

  return 0;
}

/**
 * @brief Signal handler for SIGTERM
 * @param sig signal number
 * @return void
 */
void handle_sigterm(int ) {
  serviceRegistry::isRunning = false;
  cout << "[SD] SIGTERM received" << endl;
  if (instance != nullptr) {
    instance->stop();
  }
  exit(0);
}
