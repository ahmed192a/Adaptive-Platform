/**
 * @file ucm.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2023-04-18
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef UCM_H
#define UCM_H
#include "ara/com/SOMEIP/Message.h"
#include "ara/com/proxy_skeleton/definitions.h"
#include "ara/exec/execution_client.h"
#include "ara/sm/triggerin/triggerin_proxy.h"
#include "ara/sm/triggerin/triggerin_types.h"
#include "ara/ucm/pkgmgr/packagemanagement_skeleton.h"
#include <iostream>
#include <string.h>
#include <thread>
#include <vector>


/************************************************************************
 *                           Defines                                    *
 ************************************************************************/
#define SERVER_PORT 5365           /*server port number*/
#define SD_PORT 1690               /*Service discovery port number*/
#define MAX_QUEUE_CLIENTS 5        /*Max number of clients*/
#define PKG_INSTANCE_ID 1          /*Instance Id*/
#define NUM_THREADS 3              /*number of threads*/
#define SM_TRIGGERIN_SERVICE_ID 55 /*SM_TRIGGERIN_SERVICE_ID in UCM skeleton*/

class UCM_Server {
private:
  ara::com::InstanceIdentifier instance{PKG_INSTANCE_ID};
  ara::com::proxy_skeleton::skeleton::ServiceSkeleton::SK_Handle
      skeleton_handle{SERVER_PORT, SD_PORT};
  /*             Shared Pointer                       */
  std::shared_ptr<ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton>
      server_skeleton_ptr = std::make_shared<
          ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton>(
          instance, skeleton_handle);
  /*             Socket for events                    */
  CServer server_main_socket_DG{SOCK_DGRAM};

  /*       main server TCP socket for methods handle  */
  CServer server_main_socket{SOCK_STREAM};

  ara::com::FindServiceHandle triggerin_proxy_findhandle{
      SM_TRIGGERIN_SERVICE_ID, 0, SD_PORT};
  ara::sm::triggerin::UCM_State ucm_state_g{
      ara::sm::triggerin::UCM_State::UCM_STATE_UNKNOWN};
  std::shared_ptr<
      ara::sm::triggerin::proxy::Trigger_In<ara::sm::triggerin::UCM_State>>
      triggerin_proxy_ptr{nullptr};
  ara::exec::ExecutionClient client;

  std::vector<std::thread> threads;

public:
  static bool isRunning;

  UCM_Server() ;
  void mainThread() ;

  void handle() ;
};

#endif