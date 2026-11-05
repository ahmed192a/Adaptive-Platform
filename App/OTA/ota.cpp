/**
 * @file ota.cpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2023-04-20
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "ota.h"

//// Namespaces
using namespace std;
using namespace OTA;
using namespace ara::exec;



bool otaServer::isRunning = true;

otaServer::otaServer() {
  cout << endl;
  cout << "\t\t[OTA] Intialization ......" << endl;

  cout << "\t\t[OTA] send  execution state running to EM" << endl << endl;
  client.ReportExecutionState(
      ExecutionState::kRunning); // report execution state to the execution
                                 // server

  server_udp_socket.OpenSocket(UDP_PORT_EVENTS);
  server_udp_socket.BindServer();

  /** create package management proxy **/
  cout << "-----------------------------------------------------------------"
       << endl;
  ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle handle_pkg =
      (ara::ucm::pkgmgr::proxy::PackageManagementProxy::FindService(
          findhandle_pkg)[0]);
  handle_pkg.UDP_port = UDP_PORT_EVENTS;
  pkg_proxy_ptr =
      std::make_shared<ara::ucm::pkgmgr::proxy::PackageManagementProxy>(
          handle_pkg);

  /** create OTA Trigger in proxy **/
  ara::com::proxy_skeleton::proxy::ServiceProxy::SP_Handle handle_ota =
      ara::sm::triggerin::proxy::Trigger_In<ara::sm::triggerin::OTA_State>::
          FindService(findhandle_ota_triggerin)[0];
  cout << "-----------------------------------------------------------------"
       << endl;
  handle_ota.UDP_port = UDP_PORT_EVENTS;
  ota_triggerin_proxy_ptr = std::make_shared<
      ara::sm::triggerin::proxy::Trigger_In<ara::sm::triggerin::OTA_State>>(
      handle_ota);

  cout << "[OTA] Starting OTA process" << endl;
  cout << "handle pkg : " << handle_pkg.m_server_com.port_number << " "
       << handle_pkg.m_server_com.service_id << endl;
  cout << "handle ota : " << handle_ota.m_server_com.port_number << " "
       << handle_ota.m_server_com.service_id << endl;
  cout << "-----------------------------------------------------------------"
       << endl;
  ota_state_g = ara::sm::triggerin::OTA_State::OTA_STATE_INITIALIZED;
  ota_triggerin_proxy_ptr->trigger.Set(ota_state_g);
}

void otaServer::run() {
  threads.push_back(std::thread(&otaServer::handler, this));
  std::cout << std::endl;

  ara::ucm::pkgmgr::PackageManagement::GetSwClusterInfoOutput sw_cluster_info =
      pkg_proxy_ptr->GetSwClusterInfo();
  cout << "------------------- Software Clusters information "
          "-------------------------"
       << endl;
  // loop on sw_cluster_info and print them in formated way
  for (auto const &sw_cluster : sw_cluster_info.SwInfo) {
    cout << "\tSw Cluster Name    : " << sw_cluster.Name << endl;
    cout << "\tSw Cluster Version : " << sw_cluster.Version << endl;
    cout << "\tSw Cluster State   : " << sw_cluster.State << endl;
    cout << "-----------------------------------------------------------------"
         << endl;
  }

  std::cout << std::endl;

  while (isRunning) {
    // creating the cloud client and connecting to the server
    cout << "-----------------------------------------------------------------"
         << endl;
    std::cout << "[OTA] connecting to the cloud server ..." << std::endl;
    Client cloud;

    if (cloud.cloudConnect(OTA_IP_CLOUD, OTA_PORT_CLOUD)) {

      std::cout << "\t[OTA] connection succeeded ..." << std::endl;

      // requesting the new metaData from the cloud server
      std::string json;
      std::vector<MetaData> metaData_v;
      cloud.requestMetadata(metaData_v);
      cout << "--------------------------------------------------------------"
              "---"
           << endl;
      cout << "[OTA] received metaData from cloud server : " << endl;
      for (auto &metaData : metaData_v) {

        cout << metaData.serializeToJson() << endl;
      }
      cout << "--------------------------------------------------------------"
              "---"
           << endl;


      // retrieve the old meta-data stored in the file
      MetaDataStorage fileSystem(META_DATA_FILE_PATH);
      std::vector<MetaData> metaDataList = fileSystem.load_MetaData();
      vector<MetaData> new_metaData_v;

      // check if the new meta-data is different from the old one or if there
      // are new meta-data
      if (metaDataList.empty()) {
        std::cout << "[OTA] no meta-data found in the file system" << std::endl;
        std::cout << "[OTA] creating new meta-data" << std::endl;
        new_metaData_v = metaData_v;
      } else {
        std::cout << "[OTA] meta-data found in the file system" << std::endl;
        std::cout
            << "[OTA] checking if the meta-data is different from the old one"
            << std::endl;
        // loop on the new meta-data and check if it is different from the old
        // one
        for (auto &metaData : metaData_v) {
          bool isDifferent = false;
          bool notFound = true;
          for (auto const &oldMetaData : metaDataList) {
            if (metaData == oldMetaData) {
              notFound = false;
              if (metaData > oldMetaData)
                isDifferent = true;
            }
          }
          if (isDifferent || notFound) {
            std::cout << "[OTA] new meta-data found" << std::endl;
            new_metaData_v.push_back(metaData);
          }
        }
      }
      std::cout << "[OTA] new meta-data found : " << new_metaData_v.size()
                << std::endl;
      if (!new_metaData_v.empty()) {
        // loop on the new meta-data and download the packages
        for (auto &metaData : new_metaData_v) {
          std::cout << "[OTA] downloading package " << metaData.get_appName()
                    << std::endl;
          std::vector<uint8_t> package;
          cloud.requestPackage(metaData, package);

          // if the package is directed for the classic platform use flashing
          // adapter proxy
          if (metaData.get_platformName() == "classic") {
            std::cout
                << "\t[OTA] sending the package to the flashing adapter ..."
                << std::endl;

            /** start sending the package to the ucm **/
            std::future<
                ara::ucm::pkgmgr::PackageManagement::TransferStartOutput>
                result; /* Saves TransferStartOutput */
            std::vector<uint8_t> small_data;

            uint64_t package_size = package.size();
            std::cout << "\t[OTA] Package size " << package_size << std::endl;
            result = pkg_proxy_ptr->TransferStart(package_size);
            std::cout << "\t[OTA] transfer start ..." << std::endl;
            ara::ucm::pkgmgr::PackageManagement::TransferStartOutput
                transfer_start_output = result.get();
            std::cout << "\t[OTA] transfer id : " << std::endl;

            int block_counter = 0;
            for (int i = 0; i < package.size(); i++) {
              for (int j = 0; j < transfer_start_output.BlockSize; j++) {
                if (i * transfer_start_output.BlockSize + j >= package.size()) {
                  small_data.resize(transfer_start_output.BlockSize);
                  break;
                } else
                  small_data.push_back(
                      package[i * transfer_start_output.BlockSize + j]);
              }

              // don't change 64 (packagemanagementskeleton.cpp)
              std::future<
                  ara::ucm::pkgmgr::PackageManagement::TransferDataOutput>
                  result2 = pkg_proxy_ptr->TransferData(
                      transfer_start_output.id, small_data,
                      block_counter); /* Saves TransferDataOutput */

              ara::ucm::pkgmgr::PackageManagement::TransferDataOutput result3;
              result3 = result2.get();
              std::cout << "\t[OTA] block " << block_counter << " sent"
                        << std::endl;
              block_counter++;
              small_data.clear();
              if (block_counter * transfer_start_output.BlockSize >
                  package.size())
                break;
            }

            pkg_proxy_ptr->TransferExit(transfer_start_output.id);

            /** end of sending the package to the ucm **/

            /* process sw package */
            ara::ucm::pkgmgr::PackageManagement::ProcessSwPackageOutput
                processSw_result =
                    pkg_proxy_ptr->ProcessSwPackage(transfer_start_output.id);
            if (processSw_result.error == 0) {
              std::cout << "[OTA] package successfully processed" << std::endl;
              // Activate the new package
              ara::ucm::pkgmgr::PackageManagement::ActivateOutput
                  activate_result = pkg_proxy_ptr->Activate();

              // // update the meta-data in the file system
              if (activate_result.error == 0) {
                // updating the stored meta-data
                std::cout << "\t[OTA] package activated ..." << std::endl;
                std::cout << "\t[OTA] updating the stored meta-data ..."
                          << std::endl;
                fileSystem.save_MetaData(metaData);
              } else {
                // the package failed to be fetched
                std::cout
                    << "\t[OTA] (rollback) package failed to be flashed ..."
                    << std::endl;
              }
            } else {
              std::cout << "[OTA] package processing failed" << std::endl;
            }

          } else if (metaData.get_platformName() == "adaptive") {
            // TO DO: use the adaptive ucm proxy
            cout << "\t[OTA] Adaptive platform not yet implemented" << endl;
          } else {
            std::cout << "\t[OTA] unsupported platform ..." << std::endl;
          }
        }
      } else {
        std::cout << "[OTA] no new versions in the cloud ..." << std::endl;
      }

      // disconnecting from the cloud connection
      cloud.cloudDisconnect();
    } else {
      std::cout << "\t[OTA] connection failed..." << std::endl;
    }

    sleep(FREQUENCY);

  } // end of while(true)

  threads[0].join();

  cout << "[OTA] terminating" << endl;
  client.ReportExecutionState(ExecutionState::kTerminating);
}

void otaServer::handler() {
  while (isRunning) {
    sockaddr_in echoClntAddr; /* Address of datagram source */
    unsigned int clntLen = sizeof(echoClntAddr); /* Address length */

    std::vector<uint8_t> msg; /* Buffer for storing received datagram */
    uint32_t msg_size;        /* Size of received message */

    /* Receive size of the message */
    server_udp_socket.UDPRecFrom((void *)&msg_size, sizeof(msg_size),
                                 (struct sockaddr *)&echoClntAddr, &clntLen);
    msg.resize(msg_size); /* Size the buffer that is received into */
    /* Receive the message */
    server_udp_socket.UDPRecFrom((void *)&msg[0], msg_size,
                                 (struct sockaddr *)&echoClntAddr, &clntLen);
    cout << "\t[CLIENT]  ->> Handling client "
         << inet_ntoa(echoClntAddr.sin_addr) << " " << echoClntAddr.sin_port
         << endl;

    ara::com::proxy_skeleton::Client_udp_Info cudp; /* Client_udp_Info */
    cudp.port = echoClntAddr.sin_port;              /* Port of client */
    cudp.addr =
        std::string(inet_ntoa(echoClntAddr.sin_addr)); /* Address of client */

    // make sure that the message is SOMEIP (a header is at least 16 bytes)
    if (msg.size() < 16) {
      continue; /* too short to be a SOME/IP message: ignore */
    }
    if (msg[14] != 0x02) {

      auto parsed = ara::com::SOMEIP_MESSAGE::Message::TryDeserialize(msg);
      if (!parsed) {
        continue; /* malformed datagram: ignore */
      }
      ara::com::SOMEIP_MESSAGE::Message Nmsg = *parsed;
      msg = Nmsg.GetPayload(); /* Get payload */

      if (Nmsg.MessageId().serivce_id == pkg_proxy_ptr->GetServiceId()) {
        if ((Nmsg.MessageId().method_id & 0x7FFF) ==
            pkg_proxy_ptr->CurrentStatus.Get_event_id()) { //
          pkg_proxy_ptr->CurrentStatus.handlecall(msg);
          std::cout << "NEW EVENT1 : "
                    << pkg_proxy_ptr->CurrentStatus.get_value() << std::endl;
        } else {
          ///@todo Send Error Message unknown event_id
        }
      } else {
        ///@todo Send Error Message unknown service_id
      }
    }
  }
}