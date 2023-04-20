/**
 * @file sd.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2023-04-20
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef SD_H
#define SD_H

#include <arpa/inet.h>
#include <iostream>
#include <memory>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "ara/com/SOMEIP/SomeipSDMessage.h"
#include "ara/com/SOMEIP/entry/eventgroup_entry.h"
#include "ara/com/ServiceDiscovery/csv.h"
#include "ara/com/ipc/server/socket_Server.h"
#include "ara/exec/execution_client.h"




constexpr auto CSV_FILE = "data.csv";
constexpr auto portNumber = 1690;

class serviceRegistry {
private:
  CSV csv;
  ara::exec::ExecutionClient client;
  std::vector<std::thread> threads;

public:
  static bool isRunning;

  serviceRegistry() ;

  void run() ;

  void handler();
};

#endif /* SERVICEREGISTRY_H_ */