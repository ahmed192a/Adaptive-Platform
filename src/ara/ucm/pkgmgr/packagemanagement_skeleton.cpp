/**
 * @file package_management.cpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Definition of Provided Port(PackageManagement) methods
 * @version 0.1
 *
 * @copyright Copyright (c) 2021
 *
 */

#include "ara/ucm/pkgmgr/packagemanagement_skeleton.h"
#include "ara/ucm/pkgmgr/uart_linux.h"
#include "nlohmann/json.hpp"
#include "uuid_v4.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
using namespace std;

static uint16_t current_state; /*!< variable to store the state of the
                                  bootloader updating sequence */


std::vector<char> ReadAllBytes2(char const *filename) {
  ifstream ifs(filename, ios::binary | ios::ate);
  ifstream::pos_type pos = ifs.tellg();
  std::vector<char> result(pos);
  ifs.seekg(0, ios::beg);
  ifs.read(&result[0], pos);
  return result;
}
std::future<ara::ucm::pkgmgr::PackageManagement::ActivateOutput>
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::Activate() {
  return std::async(std::launch::async, [this]() {
    ara::ucm::pkgmgr::PackageManagement::ActivateOutput output;
    /* [SWS_UCM_00241] only allowed in kReady; enters kActivating */
    if (int e = core_.BeginActivate()) {
      output.error = e;
      return output;
    }
    /* The flashing-adapter hand-over (UART) is currently stubbed: activation
     * always succeeds. Rejected update sessions ([SWS_UCM_00258]) are not
     * modelled yet. */
    std::cout << "\t\t\t\tActivate Success" << std::endl;
    core_.EndActivate(true);
    output.error = 0;
    return output;
  });
}
ara::ucm::pkgmgr::PackageManagement::CancelOutput
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::Cancel(
    ara::ucm::pkgmgr::PackageManagement::TransferIdType id) {
  ara::ucm::pkgmgr::PackageManagement::CancelOutput output;
  output.error = core_.Cancel(id); /* 00234 / 00235 */
  return output;
}
ara::ucm::pkgmgr::PackageManagement::DeleteTransferOutput
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::DeleteTransfer(
    ara::ucm::pkgmgr::PackageManagement::TransferIdType id) {
  ara::ucm::pkgmgr::PackageManagement::DeleteTransferOutput output;
  output.error = core_.DeleteTransfer(id); /* 00214 / 00215 */
  if (output.error == 0) {
    std::error_code ec;
    std::string const base = "./SWP/" + core::IdToString(id);
    std::filesystem::remove(base + ".zip", ec);
    std::filesystem::remove_all(base, ec);
  }
  return output;
}
std::future<ara::ucm::pkgmgr::PackageManagement::FinishOutput>
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::Finish() {
  return std::async(std::launch::async, [this]() {
    ara::ucm::pkgmgr::PackageManagement::FinishOutput output;
    if (int e = core_.BeginFinish()) { /* only after Activate / Rollback */
      output.error = e;
      return output;
    }
    /* remove the package files of every processed transfer */
    std::error_code ec;
    for (auto const &entry : std::filesystem::directory_iterator("./SWP", ec)) {
      std::filesystem::remove_all(entry.path(), ec);
    }
    core_.EndFinish();
    output.error = 0;
    return output;
  });
}


ara::ucm::pkgmgr::PackageManagement::GetHistoryOutput
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::GetHistory(
    uint64_t timestampGE, uint64_t timestampLT) {
  ara::ucm::pkgmgr::PackageManagement::GetHistoryOutput output;
  return output;
}

ara::ucm::pkgmgr::PackageManagement::GetIdOutput
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::GetId() {
  ara::ucm::pkgmgr::PackageManagement::GetIdOutput output;
  return output;
}

ara::ucm::pkgmgr::PackageManagement::GetSwClusterChangeInfoOutput ara::ucm::
    pkgmgr::skeleton::PackageManagementSkeleton::GetSwClusterChangeInfo() {
  ara::ucm::pkgmgr::PackageManagement::GetSwClusterChangeInfoOutput output;
  return output;
}

ara::ucm::pkgmgr::PackageManagement::GetSwClusterDescriptionOutput ara::ucm::
    pkgmgr::skeleton::PackageManagementSkeleton::GetSwClusterDescription() {
  ara::ucm::pkgmgr::PackageManagement::GetSwClusterDescriptionOutput output;
  return output;
}

ara::ucm::pkgmgr::PackageManagement::GetSwClusterInfoOutput
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::GetSwClusterInfo() {
  // open sw_packages.json and read all the packages
  std::ifstream sw_packages("sw_packages.json", std::ifstream::binary);
  if (!sw_packages.is_open()) {
    std::cout << "Error opening sw_packages.json" << std::endl;
    return ara::ucm::pkgmgr::PackageManagement::GetSwClusterInfoOutput();
  }
  ara::ucm::pkgmgr::PackageManagement::GetSwClusterInfoOutput output;

  nlohmann::json sw_packages_json;
  sw_packages >> sw_packages_json;
  sw_packages.close();

  sw_packages_json = sw_packages_json["sw_packages"];
  // loop through the json and fill
  // ara::ucm::pkgmgr::PackageManagement::SwPackageInfoVectorType in the output
  for (auto &sw_package : sw_packages_json) {
    if (sw_package["Type"] == "cluster") {
      ara::ucm::pkgmgr::PackageManagement::SwClusterInfoType sw_cluster_info = {
          .Name = sw_package["Name"],
          .Version = sw_package["Version"],
          .State = core::ParseByte(sw_package["State"])};
      output.SwInfo.push_back(sw_cluster_info);
    }
  }
  return output;
}

ara::ucm::pkgmgr::PackageManagement::GetSwPackagesOutput
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::GetSwPackages(
    ara::ucm::pkgmgr::PackageManagement::SwPackageInfoVectorType &Packages) {
  // open sw_packages.json and read all the packages
  std::ifstream sw_packages("sw_packages.json", std::ifstream::binary);
  if (!sw_packages.is_open()) {
    std::cout << "Error opening sw_packages.json" << std::endl;
    return ara::ucm::pkgmgr::PackageManagement::GetSwPackagesOutput();
  }
  nlohmann::json sw_packages_json;
  sw_packages >> sw_packages_json;
  sw_packages.close();

  sw_packages_json = sw_packages_json["sw_packages"];
  // loop through the json and fill
  // ara::ucm::pkgmgr::PackageManagement::SwPackageInfoVectorType in the output
  for (auto &sw_package : sw_packages_json) {
    ara::ucm::pkgmgr::PackageManagement::SwPackageInfoType sw_package_info = {
        .Name = sw_package["Name"],
        .Version = sw_package["Version"],
        .TransferID = sw_package["TransferID"],
        .ConsecutiveBytesReceived =
            core::ParseU64(sw_package["ConsecutiveBytesReceived"]),
        .ConsecutiveBlocksReceived =
            core::ParseU64(sw_package["ConsecutiveBlocksReceived"]),
        .State = core::ParseByte(sw_package["State"])};

    Packages.push_back(sw_package_info);
  }
  return ara::ucm::pkgmgr::PackageManagement::GetSwPackagesOutput();
}

std::future<ara::ucm::pkgmgr::PackageManagement::GetSwProcessProgressOutput>
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::GetSwProcessProgress(
    ara::ucm::pkgmgr::PackageManagement::TransferIdType id) {
  std::future<ara::ucm::pkgmgr::PackageManagement::GetSwProcessProgressOutput>
      future_output = std::async(std::launch::async, [&, id]() {
        ara::ucm::pkgmgr::PackageManagement::GetSwProcessProgressOutput output;
        output = currentSwProcessProgress;
        return output;
      });

  return future_output;
}

std::future<ara::ucm::pkgmgr::PackageManagement::ProcessSwPackageOutput>
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::ProcessSwPackage(
    ara::ucm::pkgmgr::PackageManagement::TransferIdType id) {
  std::future<ara::ucm::pkgmgr::PackageManagement::ProcessSwPackageOutput> f =
      std::async(std::launch::async, [this, id]() {
        ara::ucm::pkgmgr::PackageManagement::ProcessSwPackageOutput result;
        /* [SWS_UCM_00218/00017/00219] InvalidTransferId / ServiceBusy /
         * OperationNotPermitted; enters kProcessing */
        if (int e = core_.BeginProcess(id)) {
          result.error = e;
          return result;
        }
        // uart_linux u_linux;
        current_state = trigger_seq;
        uint16_t packet_num, const_packet_num;
        uint16_t extra_bytes;
        int block_counter = 0;
        // convert std::array<int,16> id to string str_id
        std::string const str_id = core::IdToString(id);

        // unzip the SWP/id.zip
        std::string zip_file_name = "SWP/" + str_id + ".zip";

        std::string unzip_file_name = "SWP/" + str_id + "/";
        std::string unzip_command =
            "unzip " + zip_file_name + " -d " + unzip_file_name;
        // system command to unzip the SWP/id.zip
        int unzip_result = system(unzip_command.c_str());
        // open sw.conf in the unziped folder to get the app name and version
        std::ifstream sw_conf(unzip_file_name + "sw.conf");
        if (!sw_conf.is_open()) {
          std::cout << "Error opening sw.conf" << std::endl;
          core_.EndProcess(id, false);
          result.error = ara::ucm::pkgmgr::PackageManagement::InvalidPackageManifest;
          return result;
        }
        nlohmann::json sw_conf_json = nlohmann::json::parse(sw_conf, nullptr, false);
        sw_conf.close();
        if (!sw_conf_json.is_object() || !sw_conf_json.contains("app_name") ||
            !sw_conf_json.contains("app_version") ||
            !sw_conf_json["app_name"].is_string() ||
            !sw_conf_json["app_version"].is_string()) {
          core_.EndProcess(id, false);
          result.error = ara::ucm::pkgmgr::PackageManagement::InvalidPackageManifest;
          return result;
        }
        if (core_.CancelRequested(id)) { /* [SWS_UCM_00150] */
          core_.EndProcess(id, false);
          result.error = ara::ucm::pkgmgr::PackageManagement::ProcessSwPackageCancelled;
          return result;
        }
        // get the app name and version from sw.conf
        std::string app_name = sw_conf_json["app_name"];
        std::string app_version = sw_conf_json["app_version"];

        // read the binary file from the unziped folder
        // std::ifstream binary_file(unzip_file_name + app_name,
        // std::ifstream::binary); if (!binary_file.is_open())
        // {
        //     std::cout << "Error opening binary file" << std::endl;
        //     result.error = 1;
        //     return result;
        // }

        // get the size of the binary file
        // binary_file.seekg(0, binary_file.end);
        // uint64_t binary_file_size = binary_file.tellg();
        // binary_file.seekg(0, binary_file.beg);
        // // create a buffer to read the binary file
        // std::vector<uint8_t> binary_file_buffer(binary_file_size);
        // binary_file.read((char *)binary_file_buffer.data(),
        // binary_file_size); binary_file.close();

        ifstream ifs(unzip_file_name + app_name, ios::binary | ios::ate);
        if (!ifs.is_open()) {
          core_.EndProcess(id, false);
          result.error = ara::ucm::pkgmgr::PackageManagement::ProcessedSoftwarePackageInconsistent;
          return result;
        }
        ifstream::pos_type pos = ifs.tellg();
        std::vector<uint8_t> binary_file_buffer(pos);
        ifs.seekg(0, ios::beg);
        ifs.read((char *)&binary_file_buffer[0], pos);

        if (binary_file_buffer.size() < 52) { /* header would be out of range */
          core_.EndProcess(id, false);
          result.error = ara::ucm::pkgmgr::PackageManagement::ProcessedSoftwarePackageInconsistent;
          return result;
        }
        std::vector<uint8_t> data_block(binary_file_buffer.begin() + 52,
                                        binary_file_buffer.end());
        cout << "*************************" << endl;
        printf("\t\t\t\tfirst address %x\n", *((uint32_t *)data_block.data()));
        packet_num = (uint32_t)(data_block.size() / 0x400);
        const_packet_num = packet_num;
        extra_bytes = (data_block.size() % 0x400);

        std::cout << "\t\t\t\tbuffer size " << binary_file_buffer.size()
                  << std::endl;
        std::cout << "\t\t\t\tbuffer size " << data_block.size() << std::endl;
        std::cout << "\t\t\t\tpacket_num " << packet_num << std::endl;
        std::cout << "\t\t\t\textra_bytes " << extra_bytes << std::endl;
        cout << "*************************" << endl;
        cout << "-------------- ProcessSwPackage Start --------------" << endl;

        // while (1) /* stay in this loop until the update is finished or
        // canceled */
        // {
        //     switch (current_state)
        //     {

        //         case trigger_seq:
        //         {
        //             cout<<"\t\t\t\tSend Trigger message to Classic ECU
        //             "<<endl; std::vector<uint8_t> start ;
        //             start.push_back('s');
        //             int st = start.size();
        //             u_linux.UART_sendBlock(start.data(),st);
        //             u_linux.UART_receiveBlock((uint8_t *)&current_state,
        //             CMD_SIZE); cout<<endl;
        //             // if(current_state == 1) current_state = trigger_seq;
        //             break;
        //         }
        //         case 2:
        //         {
        //             // sleep(1);
        //             u_linux.UART_receiveBlock((uint8_t *)&current_state,
        //             CMD_SIZE); printf("\t\t\t\tReceived command : %x\n",
        //             current_state); break;
        //         }
        //         case RECEIVE_REQUEST_FREAME_INFO:
        //         {
        //             int FRAME_SIZE = 4;
        //             uint8_t Frame [4];

        //             Frame[0] = uint8_t (packet_num&0x00FF);
        //             Frame[1] = uint8_t ((packet_num&0xFF00)>>8);
        //             Frame[2] = uint8_t (extra_bytes&0x00FF);
        //             Frame[3] = uint8_t ((extra_bytes&0xFF00)>>8);

        //             u_linux.UART_sendBlock(Frame, FRAME_SIZE);

        //             std::cout<<"\t\t\t\tSEND_FRAME_INFO  "<<endl;
        //             current_state = 2;
        //         break;
        //         }
        //         case SEND_NEW_PACKET:
        //         {

        //             std::vector<uint8_t> small_data;
        //             for(int j=0; j<0x400; j++)
        //             {
        //                 if((block_counter*0x400)+ j < data_block.size()){
        //                     small_data.push_back(data_block[block_counter*0x400
        //                     + j]);
        //                 }else
        //                     break;
        //             }
        //             u_linux.UART_sendBlock(small_data.data(),
        //             small_data.size());

        //             // print the percentage of the update process
        //             currentSwProcessProgress.progress =
        //             (uint8_t)((float)block_counter*100/(float)const_packet_num);
        //             // print the percentage of the update process with three
        //             digits after the decimal point
        //             printf("\t\t\t\t->Percentage of the update process :
        //             [%d%%]",currentSwProcessProgress.progress);
        //             std::cout<<"\tPacket num : "<< block_counter <<", size :
        //             "<< small_data.size()<<std::endl<<std::endl;

        //             packet_num -=1;
        //             block_counter++;

        //             small_data.clear();
        //             current_state = 2;
        //             break;
        //         }
        //         case END_OF_UPDATE:
        //         {
        //             std::cout<<"\t\t\t\tEND_OF_UPDATE ";
        //             printf("%x", current_state);
        //             cout<<endl;

        //             // current_state = Activate_s;
        //             // u_linux.UART_sendBlock((uint8_t *)&current_state,
        //             CMD_SIZE);
        //             // std::cout<<"\t\t\t\tActivate ";
        //             // printf("%x", current_state);
        //             // cout<<endl;
        //             break;
        //         }
        //         case Activate_s:
        //         {
        //             u_linux.UART_receiveBlock((uint8_t *)&current_state,
        //             CMD_SIZE); std::cout<<"\t\t\t\tRecieved UPDATE_SUCCESS :
        //             "; printf("%x", current_state); cout<<endl;

        //         break;
        //         }

        //         default:
        //         {
        //             current_state = 2; 			/* Initialize the current state
        //             */ break;
        //         }
        //     }
        //     // if(current_state == UPDATE_SUCCESS) break;
        //     if(current_state == END_OF_UPDATE) break;
        // }
        cout << "-------------- ProcessSwPackage End --------------" << endl;
        core_.EndProcess(id, true);
        result.error = 0;
        return result;
      });
  return f;
}
ara::ucm::pkgmgr::PackageManagement::RevertProcessedSwPackagesOutput
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::RevertProcessedSwPackages() {
  ara::ucm::pkgmgr::PackageManagement::RevertProcessedSwPackagesOutput output;
  output.error = core_.RevertProcessed(); /* 00237 */
  return output;
}
ara::ucm::pkgmgr::PackageManagement::RollbackOutput
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::Rollback() {
  ara::ucm::pkgmgr::PackageManagement::RollbackOutput output;
  if (int e = core_.BeginRollback()) { /* 00239 */
    output.error = e;
    return output;
  }
  /* No hardware rollback exists yet: the previous image is simply kept. */
  core_.EndRollback(true);
  return output;
}
std::future<ara::ucm::pkgmgr::PackageManagement::TransferDataOutput>
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::TransferData(
    ara::ucm::pkgmgr::PackageManagement::TransferIdType id,
    ara::ucm::pkgmgr::PackageManagement::ByteVectorType data,
    uint64_t blockCounter) {
  return std::async(std::launch::async, [this, id, data = std::move(data), blockCounter]() {
    ara::ucm::pkgmgr::PackageManagement::TransferDataOutput result;
    auto r = core_.TransferData(id, data.size(), blockCounter);
    result.error = r.error;
    if (r.error == 0) {
      /* append only the bytes that belong to the package (a padded last block
       * is trimmed) */
      std::ofstream out("./SWP/" + core::IdToString(id) + ".zip",
                        std::ios::binary | std::ios::app);
      out.write(reinterpret_cast<const char *>(data.data()),
                static_cast<std::streamsize>(r.accepted));
      if (!out) result.error = ara::ucm::pkgmgr::PackageManagement::InsufficientMemory;
    }
    return result;
  });
}
std::future<ara::ucm::pkgmgr::PackageManagement::TransferExitOutput>
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::TransferExit(
    ara::ucm::pkgmgr::PackageManagement::TransferIdType id) {
  return std::async(std::launch::async, [this, id]() {
    ara::ucm::pkgmgr::PackageManagement::TransferExitOutput result;
    result.error = core_.TransferExit(id); /* 00148 / 00212 / 00087 */
    return result;
  });
}
std::future<ara::ucm::pkgmgr::PackageManagement::TransferStartOutput>
ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::TransferStart(
    uint64_t size) {
  return std::async(std::launch::async, [this, size]() {
    ara::ucm::pkgmgr::PackageManagement::TransferStartOutput result{};
    UUIDv4::UUIDGenerator<std::mt19937_64> uuidGenerator;
    UUIDv4::UUID uuid = uuidGenerator.getUUID();
    char bytes[16];
    uuid.bytes(bytes);
    ara::ucm::pkgmgr::PackageManagement::TransferIdType id;
    for (int i = 0; i < 16; i++) id[i] = static_cast<uint8_t>(bytes[i]);
    auto r = core_.TransferStart(size, id);
    if (r.error != 0) { /* 00140: reported as BlockSize 0 (no error field on the wire) */
      result.BlockSize = 0;
      return result;
    }
    std::error_code ec;
    std::filesystem::create_directories("./SWP", ec);
    std::ofstream("./SWP/" + core::IdToString(id) + ".zip", std::ios::binary | std::ios::trunc);
    result.id = r.id;
    result.BlockSize = r.blockSize;
    return result;
  });
}


void ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::
    field_method_dispatch(ara::com::SOMEIP_MESSAGE::Message &message,
                          Socket &cserver) {
  // extract event id
  uint16_t event_id = message.MessageId().method_id & 0x7FFF;
  switch (event_id) {
  case 0: /* Id of CurrentStatus Field   */
    if (message.Length() >
        ara::com::SOMEIP_MESSAGE::Message::EmptyLength) /* if Message has payload
                                                          So it's Set request
                                                          with the new data  */
    {
      /*    CurrentStatus does't have setter   */
      NoMethodHandler(event_id,
                      cserver); /* Send Error Message unknown event_id  */
    } else // if Message doesn't have payload So it's Get request with just the
           // header 16 Bytes
    {
      CurrentStatus.HandleGet(message, cserver);
    }
    break;
  default:
    NoMethodHandler(event_id, cserver); // Send Error Message unknown event_id
    break;
  }
}

void ara::ucm::pkgmgr::skeleton::PackageManagementSkeleton::method_dispatch(
    ara::com::SOMEIP_MESSAGE::Message &message, Socket &cserver) {
  ara::com::Deserializer dser;

  uint16_t methodID = message.MessageId().method_id;

  cout << "\t\t\t[SERVER] Dispatch " << methodID;

  switch (methodID) {
  case 7: // TransferStart
    cout << " TransferStart" << endl;
    HandleCall(*this, &PackageManagementSkeleton::TransferStart, message,
               cserver);

    break;
  case 8: // TransferData
  {
    cout << " TransferData" << endl;
    std::cout <<"parameters size " << message.GetPayload().size() << std::endl;
    HandleCall(*this, &PackageManagementSkeleton::TransferData, message,
               cserver);
    // std::vector<uint8_t> msg = message.GetPayload();


    // ara::ucm::pkgmgr::PackageManagement::ByteVectorType data;
    // uint64_t blockCounter = dser.deserialize<uint64_t>(msg, 16);
    // ara::ucm::pkgmgr::PackageManagement::TransferIdType id =
    //     dser.deserialize<ara::ucm::pkgmgr::PackageManagement::TransferIdType>(
    //         msg, 0);
    // std::future<ara::ucm::pkgmgr::PackageManagement::TransferDataOutput>
    //     transfer_data_output;
    // ara::ucm::pkgmgr::PackageManagement::TransferDataOutput rval;


    // data.clear();
    // data.insert(data.begin(), msg.begin() + 24, msg.end());

    // // cout << "msgsize" << msg.size() << endl;
    // transfer_data_output = TransferData(id, data, blockCounter);
    // rval = transfer_data_output.get();
    // // create message and send it
    // ara::com::SOMEIP_MESSAGE::Message response_m(
    //     ara::com::SOMEIP_MESSAGE::Message_ID{
    //         this->UCM_Service_id,
    //         (uint16_t)(message.MessageId().method_id & 0x7fff)},
    //     ara::com::SOMEIP_MESSAGE::Request_ID{5, 6},
    //     2, // protocol version
    //     7, // Interface Version
    //     ara::com::SOMEIP_MESSAGE::MessageType::RESPONSE);

    // // create serialize object
    // ara::com::Serializer s1;
    // // serialize the result
    // s1.serialize(rval);
    // std::vector<uint8_t> _payload = s1.Payload();
    // response_m.SetPayload(_payload);
    // _payload.clear();
    // _payload = response_m.Serializer();

    // uint32_t msg_size = _payload.size();
    // // send message
    // cserver.Send(&msg_size, sizeof(msg_size));
    // cserver.Send(_payload.data(), msg_size);
    // cserver.CloseSocket();
  }

  break;
  case 9:
    cout << " TransferExit" << endl;
    HandleCall(*this, &PackageManagementSkeleton::TransferExit, message,
               cserver);
    break;
  case 10:
    cout << " ProcessSwPackage" << endl;
    HandleCall(*this, &PackageManagementSkeleton::ProcessSwPackage, message,
               cserver);
    break;
  case 11:
    cout << " GetSwProcessProgress" << endl;
    HandleCall(*this, &PackageManagementSkeleton::GetSwProcessProgress, message,
               cserver);
    break;

  case 12: {
    cout << " GetSwPackage" << endl;
    ara::ucm::pkgmgr::PackageManagement::GetSwPackagesOutput result;
    GetSwPackages(result.Packages);
    // create message and send it
    ara::com::SOMEIP_MESSAGE::Message response_m(
        ara::com::SOMEIP_MESSAGE::Message_ID{
            (uint16_t)this->UCM_Service_id,
            (uint16_t)(message.MessageId().method_id & 0x7fff)},
        ara::com::SOMEIP_MESSAGE::Request_ID{5, 6},
        ara::com::SOMEIP_MESSAGE::kProtocolVersion, // protocol version
        7, // Interface Version
        ara::com::SOMEIP_MESSAGE::MessageType::RESPONSE);
    // create data vector
    vector<uint8_t> data;
    // loop over all packages and serialize them
    for (auto &package : result.Packages) {
      ara::com::Serializer s1;
      data.insert(data.end(), package.Name.begin(), package.Name.end());
      data.push_back(0);
      data.insert(data.end(), package.Version.begin(), package.Version.end());
      data.push_back(0);
    }
    // set payload
    response_m.SetPayload(data);
    // send message
    data.clear();
    data = response_m.Serializer();
    uint32_t msg_size = data.size();
    cserver.Send(&msg_size, sizeof(msg_size));
    cserver.Send(data.data(), msg_size);
    cserver.CloseSocket();
  } break;
  case 13: {
    cout << " GetSwClusterInfo" << endl;
    ara::ucm::pkgmgr::PackageManagement::GetSwClusterInfoOutput result;
    result = GetSwClusterInfo();
    // create message and send it
    ara::com::SOMEIP_MESSAGE::Message response_m(
        ara::com::SOMEIP_MESSAGE::Message_ID{
            (uint16_t)this->UCM_Service_id,
            (uint16_t)(message.MessageId().method_id & 0x7fff)},
        ara::com::SOMEIP_MESSAGE::Request_ID{5, 6},
        ara::com::SOMEIP_MESSAGE::kProtocolVersion, // protocol version
        7, // Interface Version
        ara::com::SOMEIP_MESSAGE::MessageType::RESPONSE);
    // create data vector
    vector<uint8_t> data;
    // loop over all packages and serialize them
    for (auto &package : result.SwInfo) {
      ara::com::Serializer s1;
      data.insert(data.end(), package.Name.begin(), package.Name.end());
      data.push_back(0);
      data.insert(data.end(), package.Version.begin(), package.Version.end());
      data.push_back(0);
      data.push_back(package.State);
      data.push_back(0);
    }
    // set payload
    response_m.SetPayload(data);
    // send message
    data.clear();
    data = response_m.Serializer();
    uint32_t msg_size = data.size();
    cserver.Send(&msg_size, sizeof(msg_size));
    cserver.Send(data.data(), msg_size);
    cserver.CloseSocket();
    break;
  }
  case 21:
    cout << " Activate" << endl;
    HandleCall(*this, &PackageManagementSkeleton::Activate, message, cserver);
    break;
  default:
    cout << " Unknown Method" << endl;
    NoMethodHandler(methodID, cserver); // Send Error Message unknown method
    // cserver.Send(&result, sizeof(int));
    // cserver.CloseSocket();
    break;
  }
}