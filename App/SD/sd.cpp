/**
 * @file sd.cpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2023-04-20
 *
 * @copyright Copyright (c) 2023
 *
 */

#include "sd.h"
using namespace std;
using namespace ara::exec;

bool serviceRegistry::isRunning = true;

serviceRegistry::serviceRegistry() {

  std::cout << std::endl << "   [SD] Initilization..." << std::endl;
  //     cout<<endl<<"[SD]"<<std::string(get_current_dir_name())<<endl;
  client.ReportExecutionState(ExecutionState::kRunning);

  cout << "   [SD] Reported Runnig to EM..." << endl << endl;
  cout << "   [SD] Waiting for incoming connections..." << endl;
  cout << "--------------------------" << endl;

  pthread_t threads[2]; // create two threads
  int i = 0;            // thread number

  csv.clear(CSV_FILE); // clear the csv file before starting the program
}





namespace {
constexpr uint32_t MaxSdMessageSize = 64 * 1024; //!< upper bound for one SD message
}

void serviceRegistry::run() {
  threads.push_back(std::thread(&serviceRegistry::handler, this));
  CServer s1(SOCK_STREAM);   // create a server object
  s1.OpenSocket(portNumber); // open a socket on port 1690
  s1.BindServer();           // bind the server to the port
  s1.ListenServer(3);        // listen to the port
  while (isRunning) {
    vector<SD_data> data; // vector to store the data from the csv file
    uint32_t _size = 0;
    std::vector<uint8_t> _msg; // vector to store the serialized message
    ara::com::SOMEIP_MESSAGE::sd::SomeIpSDMessage
        sd_msg; // create a SomeIP SD message to store the deserialized
                // message received from the client
    ara::com::SOMEIP_MESSAGE::sd::SomeIpSDMessage
        R_msg; // create a SomeIP SD message to send the message to the client

    Socket soc = s1.AcceptServer(); // accept the client connection

    // read the message from the client
    soc.Receive(&_size, sizeof(_size)); // receive the size of the message
    if (_size == 0 || _size > MaxSdMessageSize) { // refuse absurd sizes
      soc.CloseSocket();
      continue;
    }
    _msg.resize(_size);                 // size (not just reserve) the buffer
    soc.Receive(&_msg[0], _size);       // receive the message
    // deserialize the message; ignore malformed or empty requests
    if (!sd_msg.Deserialize(_msg) || sd_msg.Entries().empty()) {
      soc.CloseSocket();
      continue;
    }

    // Loop on entries
    auto entry = sd_msg.Entries()[0];        // get the first entry
    uint32_t serviceid = entry->ServiceId(); // get the service id
    ara::com::entry::ServiceEntry eventgroup_entry =
        ara::com::entry::ServiceEntry::CreateFindServiceEntry(
            serviceid); // get the service entry

    std::cout << "      [SD(Thread 1)]: Client requested service id  : "
              << serviceid << std::endl; // print the service id
    cout << "--------------------------" << endl;

    csv.FindRow(CSV_FILE, serviceid,
                data); // search the file for the service id

    // create dynamic Ipv4EndpointOption object from static function
    auto ipv4_option = std::make_shared<ara::com::option::Ipv4EndpointOption>(
        ara::com::option::Ipv4EndpointOption::CreateSdEndpoint(
            false, ara::com::helper::Ipv4Address(127, 0, 0, 1),
            ara::com::option::Layer4ProtocolType::Udp, data[0].port_number));

    eventgroup_entry.AddFirstOption(
        ipv4_option); // add the ipv4 option to the eventgroup entry

    R_msg.AddEntry(&eventgroup_entry); // add the eventgroup entry to the
                                       // SomeIP SD message
    std::vector<uint8_t> R_msg_serialized =
        R_msg.Serializer(); // serialize the SomeIP message
    uint32_t R_msg_size =
        R_msg_serialized.size(); // get the size of the serialized message

    soc.Send(&R_msg_size, sizeof(R_msg_size));  // send the size of the message
    soc.Send(&R_msg_serialized[0], R_msg_size); // send the message

    soc.CloseSocket(); // close the socket
  }

  s1.CloseSocket(); // close the socket

  for (auto &t : threads) {
    t.join();
  }
  cout << "   [SD] Reported Terminating to EM..." << endl;
  client.ReportExecutionState(ExecutionState::kTerminating);
}





void serviceRegistry::handler() {
  CServer s1(SOCK_DGRAM);    // create a server object
  s1.OpenSocket(portNumber); // open a socket on port 1690
  s1.BindServer();           // bind the server to the port

  struct sockaddr_in cliaddr; // create a struct to store the client address
  socklen_t len = sizeof(
      cliaddr); // create a variable to store the size of the client address
  while (isRunning) {
    uint32_t _size = 0; // create a variable to store the size of the message
    std::vector<uint8_t> _msg; // create a vector to store the received message
    SD_data
        receive; // global variable to store the data received from the server

    ara::com::SOMEIP_MESSAGE::sd::SomeIpSDMessage
        sd_msg; // create a SomeIP SD message to store the deserialized
                // message received from the server

    s1.UDPRecFrom(&_size, sizeof(_size), (struct sockaddr *)&cliaddr,
                  &len); // receive the size of the message
    if (_size == 0 || _size > MaxSdMessageSize) { // refuse absurd sizes
      continue;
    }
    _msg.resize(_size); // size (not just reserve) the buffer
    s1.UDPRecFrom(&_msg[0], _size, (struct sockaddr *)&cliaddr,
                  &len); // receive the message

    cout << "       [SD(Thread 0)]: Receiving Offers" << endl;
    if (!sd_msg.Deserialize(_msg)) { // ignore malformed messages
      continue;
    }

    // Loop on entries
    auto entrie = sd_msg.Entries(); // get the entries
    for (auto const &entry : entrie) {
      switch (entry->Type()) {

      case ara::com::entry::EntryType::Offering: {
        if (entry->FirstOptions().empty()) {
          break; // an offer without an endpoint carries nothing to register
        }
        auto first_option =
            entry->FirstOptions()[0]; // get the first entry option

        std::shared_ptr<ara::com::option::Ipv4EndpointOption> ipv4_option =
            std::dynamic_pointer_cast<ara::com::option::Ipv4EndpointOption>(
                first_option); // store first option as ipv4 option
        if (!ipv4_option) {
          break;
        }

        // print the ipv4 address
        cout << "           IPv4: " << int(ipv4_option->IpAddress().Octets[0])
             << "." << int(ipv4_option->IpAddress().Octets[1]) << "."
             << int(ipv4_option->IpAddress().Octets[2]) << "."
             << int(ipv4_option->IpAddress().Octets[3]) << endl;
        cout << "           Port: " << ipv4_option->Port()
             << endl; // print the port number
        cout << "           ServiceId: " << entry->ServiceId()
             << endl; // print the service id
        cout << "--------------------------" << endl;

        // place info into recvieve object
        receive.instance_id = entry->InstanceId(); // store the instance id
        receive.service_id = entry->ServiceId();   // store the service id
        receive.port_number = ipv4_option->Port(); // store the port number

        // check TTL
        if (entry->TTL() > 0) {
          csv.write(CSV_FILE,
                    receive); // write the service info in the csv file
        } else {
          csv.delete_record(CSV_FILE, receive.service_id,
                            receive.instance_id); // delete the service record
                                                  // from the csv file
        }
      } break;
      case ara::com::entry::EntryType::Subscribing: {
        //                 cout << "EntryType: Subscribing" << endl;
      } break;

      case ara::com::entry::EntryType::Acknowledging: {
      } break;
      default:
        break;
      }
    }
  }
  s1.CloseSocket(); // close the socket
}