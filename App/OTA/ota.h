/**
 * @file ota.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2023-04-20
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef OTA_H
#define OTA_H

#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
// OTA related includes
#include "OTA/client.h"
#include "OTA/meta_data_storage.h"
// ara::com related includes
#include "ara/com/SOMEIP/SomeipSDMessage.h"
#include "ara/com/SOMEIP/entry/eventgroup_entry.h"
#include "ara/com/ipc/client/socket_Client.h"
#include "ara/com/ipc/server/socket_Server.h"
// ara::ucm related includes
#include "ara/sm/triggerin/triggerin_types.h"
#include "ara/ucm/pkgmgr/packagemanagement_proxy.h"
#include "ara/ucm/pkgmgr/ucm_return_types.h"
// ara::sm related includes
#include "ara/sm/triggerin/triggerin_proxy.h"
// ara::exec related includes
#include "ara/exec/execution_client.h"



constexpr auto META_DATA_FILE_PATH = "./meta-data.dat";
constexpr auto FREQUENCY = 10; // 10 seconds
// ara::com related defines
constexpr auto SD_PORT = 1690;
constexpr auto PKG_SERVICE_ID = 45;
constexpr auto SM_OTA_TRIGGERIN_SERVICE_ID = 56;
constexpr auto UDP_PORT_EVENTS = 7575;
constexpr auto NUM_THREADS = 3;


class otaServer {
private:
  //// Globale variables
  CServer server_udp_socket{SOCK_DGRAM};

  /** pakage management proxy **/
  ara::com::FindServiceHandle findhandle_pkg{PKG_SERVICE_ID, 0, SD_PORT};
  std::shared_ptr<ara::ucm::pkgmgr::proxy::PackageManagementProxy>
      pkg_proxy_ptr;
  /** OTA Trigger in Proxy    **/
  ara::com::FindServiceHandle findhandle_ota_triggerin{
      SM_OTA_TRIGGERIN_SERVICE_ID, 0, SD_PORT};
  std::shared_ptr<
      ara::sm::triggerin::proxy::Trigger_In<ara::sm::triggerin::OTA_State>>
      ota_triggerin_proxy_ptr;

  ara::sm::triggerin::OTA_State ota_state_g =
      ara::sm::triggerin::OTA_State::OTA_STATE_UNKNOWN;

  // create execution client
  ara::exec::ExecutionClient client;
  std::vector<std::thread> threads;

public:
  static bool isRunning;
  otaServer();

  void run() ;

  void handler() ;
};

#endif