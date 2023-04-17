/**
 * @file service_proxy.cpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Base class for all services.
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/com/proxy_skeleton/proxy/service_proxy.h"
#include "ara/com/SOMEIP/SomeipSDMessage.h"
#include "ara/com/SOMEIP/entry/entry.h"
#include "ara/com/SOMEIP/helper/ipv4_address.h"
#include "ara/com/SOMEIP/option/ipv4_endpoint_option.h"
#include "ara/com/SOMEIP/option/option.h"
#include "ara/com/deserializer.h"
#include "ara/com/proxy_skeleton/data_type.h"
#include "ara/com/proxy_skeleton/definitions.h"
#include "ara/com/serializer.h"
#include <iostream>
#include <thread>
#include <vector>

namespace ara::com::proxy_skeleton::proxy {
ServiceHandleContainer<ServiceProxy::SP_Handle>
ServiceProxy::FindService(FindServiceHandle FSH) {
  ServiceHandleContainer<ServiceProxy::SP_Handle> vhandler;
  // try to open socket with SD periodically until it is successful
  do {
    CClient Client_SD(SOCK_STREAM);

    while (Client_SD.OpenSocket() != SUCCEEDED) {
      std::cout << "Openning socket with SD failed, retrying in 1 second"
                << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    // vhandler.resize(1);
    // Send a service id to the service discovery
    // receive a struct
    // m_service_id = service_id;

    SOMEIP_MESSAGE::sd::SomeIpSDMessage m_info;
    SOMEIP_MESSAGE::sd::SomeIpSDMessage msg_info;
    entry::ServiceEntry event_gr_entry =
        entry::ServiceEntry::CreateFindServiceEntry(FSH.service_id);

    m_info.AddEntry(&event_gr_entry);
    std::vector<uint8_t> _payload = m_info.Serializer();
    auto _payload_size = (uint32_t)_payload.size();

    Client_SD.GetHost("127.0.0.1", FSH.portnum);
    Client_SD.ClientConnect();

    // send the service id
    Client_SD.ClientWrite(&_payload_size, sizeof(_payload_size));
    Client_SD.ClientWrite(_payload.data(), _payload_size);
    _payload.clear();
    // receive a struct
    Client_SD.ClientRead(&_payload_size, sizeof(_payload_size));
    _payload.resize(_payload_size);
    Client_SD.ClientRead(_payload.data(), _payload_size);
    msg_info.Deserialize(_payload);

    Client_SD.CloseSocket();

    std::vector<entry::Entry *> entries = msg_info.Entries();

    auto const &options = entries[0]->FirstOptions();

    for (auto option : options) {
      ServiceProxy::SP_Handle handle;
      // service_id in  haandle
      // sp_handle -> service_id, instance_id<->process_id, port_id,
      std::shared_ptr<option::Ipv4EndpointOption> ipv4_endpoint =
          std::dynamic_pointer_cast<option::Ipv4EndpointOption>(option);

      handle.m_server_com.port_number = ipv4_endpoint->Port();
      handle.m_server_com.service_id = FSH.service_id;
      handle.m_server_com.instance_id = FSH.instance_id;
      vhandler.push_back(handle);
    }
  } while (vhandler.size() == 0);
  return vhandler;
}

} // namespace ara::com::proxy_skeleton::proxy
