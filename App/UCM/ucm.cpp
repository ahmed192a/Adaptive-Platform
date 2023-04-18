/**
 * @file ucm.cpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2023-04-18
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "ucm.h"
#include <cstdint>

/// Namespaces
using namespace std;
using namespace ara::exec;

bool UCM_Server::isRunning = true;

UCM_Server::UCM_Server() {
  std::cout << "\t\t[UCM] Intialization ......" << std::endl;
  std::cout << "\t\t[UCM]Reporting running to EM" << std::endl << std::endl;
  client.ReportExecutionState(ExecutionState::kRunning);
  std::cout << "---------------------------------------------" << std::endl;

  /*     UDP Server sockets for events      */
  server_main_socket_DG.OpenSocket(SERVER_PORT);
  server_main_socket_DG.BindServer();
  ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle
      triggerin_proxy_handle =
          (ara::sm::triggerin::proxy::
               Trigger_In<ara::sm::triggerin::UCM_State>::FindService(
                   triggerin_proxy_findhandle)[0]);
  triggerin_proxy_handle.UDP_port = SERVER_PORT;
  triggerin_proxy_ptr = std::make_shared<
      ara::sm::triggerin::proxy::Trigger_In<ara::sm::triggerin::UCM_State>>(
      triggerin_proxy_handle);
}












void UCM_Server::mainThread() {
  threads.push_back(std::thread(&UCM_Server::handle, this));
  /* TCP with client */
  std::cout << "----------------------Thread 1--------------------"
            << std::endl;
  cout << "[UCM SERVER] OPEN MAIN SOCKET ON " << endl;

  server_main_socket.OpenSocket(SERVER_PORT);
  server_main_socket.BindServer();
  server_main_socket.ListenServer(MAX_QUEUE_CLIENTS);

  cout << "[UCM SERVER] OFFER Services " << endl;
  server_skeleton_ptr->OfferService();
  ucm_state_g =
      ara::sm::triggerin::UCM_State::UCM_STATE_INITIALIZED; // set ucm state
                                                            // to initialized
  triggerin_proxy_ptr->trigger.Set(
      ucm_state_g); // set triggerin proxy state to initialized to inform SM

  cout << "[SERVER]  Ready to Accept clients " << endl;

  while (isRunning) {
    cout << "---------------------------------------------" << endl;
    Socket Sclient = server_main_socket.AcceptServer();
    cout << "\t[SERVER]  Accepted to new Client request " << endl;

    uint32_t msg_size;        /* Size of message  */

    /*    Receive a payload from client containing the method name and
     * parameters  */
    Sclient.Receive((void *)&msg_size,
                    sizeof(msg_size)); /*  recieve message size   */
    std::vector<uint8_t> msg(msg_size); /* Payload message from client */

    Sclient.Receive((void *)&msg[0], msg_size); /*  recieve the message  */

    /*         SOMEIP_MESSAGE Deserialize    */
    ara::com::SOMEIP_MESSAGE::Message someip_msg =
        ara::com::SOMEIP_MESSAGE::Message::Deserialize(msg);
    if (someip_msg.MessageId().serivce_id ==
        server_skeleton_ptr->GetServiceId()) {
      if (someip_msg.check_Methode_ID() == true) {
        cout << "\t\t[UCM SERVER] This is method request " << endl;
        // hendling method request
        server_skeleton_ptr->method_dispatch(someip_msg, Sclient);
      } else {
        cout << "\t\t[UCM SERVER] This is SET OR GET request " << endl;
        // handling GET or SET field request
        server_skeleton_ptr->field_method_dispatch(someip_msg, Sclient);
      }
    } else {
      cout << "[UCM SERVER] received message from client but not from server"
           << endl;
      ara::com::proxy_skeleton::skeleton::ServiceSkeleton::NoServiceHandler(
          someip_msg, Sclient);
    }
  }
  threads[0].join();

  /// server_skeleton_ptr->StopOfferService();

  cout << "---------------------------------------------" << endl;
  cout << "[UCM] Reporting terminating to EM" << endl;
  cout << "---------------------------------------------" << endl;
  client.ReportExecutionState(
      ExecutionState::kTerminating); // report execution state to the
                                     // execution server

  server_main_socket.CloseSocket();    /* close server socket   */
  server_main_socket_DG.CloseSocket(); /* close server socket   */
}










void UCM_Server::handle() {
  std::cout << "----------------------Thread 2--------------------"
            << std::endl;

  while (isRunning) {
    sockaddr_in echoClntAddr; /* Address of datagram source  */
    unsigned int clntLen = sizeof(echoClntAddr); /* Address length */
    std::vector<uint8_t> msg; /* Payload message from client */
    uint32_t msg_size;        /* Size of message  */
    server_main_socket_DG.UDPRecFrom((void *)&msg_size, sizeof(msg_size),
                                     (struct sockaddr *)&echoClntAddr,
                                     &clntLen);

    msg.resize(msg_size);
    server_main_socket_DG.UDPRecFrom(
        (void *)&msg[0], msg_size, (struct sockaddr *)&echoClntAddr, &clntLen);
    cout << "---------------------------------------------" << endl;
    cout << "[UCM SERVER] UDP EVENT RECEIVED" << endl;
    cout << "[UCM SERVER] UDP recieved message size" << endl;
    cout << "[UCM SERVER] UDP recieved message Content" << endl;

    if (msg[14] == 0x02) /* make sure it's SOMEIP/SD message */
    {
      cout << "[UCM SERVER] SOMEIP/SD message received" << endl;
      ara::com::SOMEIP_MESSAGE::sd::SomeIpSDMessage sd_msg;
      ara::com::proxy_skeleton::Client_udp_Info cudp;

      sd_msg.Deserialize(msg);
      auto entry = (ara::com::entry::EventgroupEntry *)sd_msg.Entries()[0];

      cudp.addr = std::string(inet_ntoa(echoClntAddr.sin_addr));

      cout << "[UCM SERVER] Handling client "
           << inet_ntoa(echoClntAddr.sin_addr) << " with msg size " << msg_size
           << endl;

      if (entry->ServiceId() == server_skeleton_ptr->GetServiceId()) {
        if (entry->EventgroupId() ==
            server_skeleton_ptr->CurrentStatus.Get_event_id()) {
          std::cout << "\t[server] subscribe event handle \n";
          server_skeleton_ptr->CurrentStatus.subhandlecall(sd_msg, cudp);
        } else {
          ///@todo unknown eventgroup id
        }
      } else {
        cout << "[UCM SERVER] received message from client but not from server"
             << endl;
        ///@todo unknown service id
      }
    }
  }
}
