/**
 * @file ota_main.cpp
 * @author Ahmed Moahmed (ahmed.moahmed.eng.25@gmail.com)
 * @brief
 * @version 0.1
 * @date 2022-07-09
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ota.h"



otaServer * instance = nullptr;
void handle_sigterm(int sig);



int main(void) {
  signal(SIGTERM, handle_sigterm); // register signal handler
  otaServer server;
  instance = &server;
  server.run();

  return 0;
}

/**
 * @brief Signal handler for SIGTERM
 * @param sig signal number
 * @return void
 */
void handle_sigterm(int ) {
    otaServer::isRunning = false;
    if (instance != nullptr) {
        instance->stop();
    }

    exit (0);
}
