/**
 * @file    ucm_server.cpp
 * @author  Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date
 *
 * @copyright Copyright (c) 2022
 *
 */
/************************************************************************
 * **********        INCLUDES         ***********************************
 * **********************************************************************/
#include "ucm.h"


/************************************************************************
 * **********        Functions declarations      ************************
 * **********************************************************************/
void handle_sigterm(int sig);

/************************************************************************
 * **********        Globale variables      ************************
 * **********************************************************************/



/**
 * @brief Main Function
 *
 * @return int
 */
int main() {
  signal(SIGTERM, handle_sigterm);

  UCM_Server server;
  server.mainThread();

  return 0;
}
/**
 * @brief handle_sigterm -->
 *
 * @param sig
 */
void handle_sigterm(int ) { UCM_Server::isRunning = false; }
