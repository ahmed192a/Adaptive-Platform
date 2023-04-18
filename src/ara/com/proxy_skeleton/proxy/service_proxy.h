/**
 * @file service_proxy.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_COM_INTERNAL_SERVICE_PROXY_H_
#define ARA_COM_INTERNAL_SERVICE_PROXY_H_

#include "ara/com/SOMEIP/SomeipSDMessage.h"
#include "ara/com/SOMEIP/entry/eventgroup_entry.h"
#include "ara/com/deserializer.h"
#include "ara/com/ipc/client/socket_Client.h"
#include "ara/com/ipc/server/socket_Server.h"
#include "ara/com/proxy_skeleton/data_type.h"
#include "ara/com/proxy_skeleton/definitions.h"
#include "ara/com/serializer.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <queue>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using SD_data = ara::com::proxy_skeleton::SD_data;

namespace ara::com::proxy_skeleton::proxy {
/**
 * @brief ServiceProxy is the main class for the proxy skeleton.
 *
 */
class ServiceProxy {
private:
  CClient service_proxy_tcp{SOCK_STREAM}; /*!< TCP client */
  CClient service_proxy_udp{SOCK_DGRAM}; /*!< UDP client */

public:
  /**
   * @brief SP_Handle struct for SP_Handle (ServiceProxyHandle)
   *
   */
  struct SP_Handle {
    uint16_t UDP_port;
    SD_data m_server_com;
  };

  SP_Handle m_proxy_handle; /*!< struct to receive in it the process id & port
                               number of the server from service discovery */

  /**
   * @brief Construct a new Service Proxy object
   *
   * @param proxy_handle a struct to receive in it the process id & port number
   *                      of the server from service discovery
   */
  explicit ServiceProxy(SP_Handle const& proxy_handle): m_proxy_handle(proxy_handle) {}
  
  /**
   * @brief Destroy the Service Proxy object
   *
   */
  virtual ~ServiceProxy() = default;

  /**
   * @brief Get the Service Id object
   *
   * @return const ara::com::proxy_skeleton::ServiceId
   */
  ara::com::proxy_skeleton::ServiceId GetServiceId() const{
    return m_proxy_handle.m_server_com.service_id;
  }
  /**
   * @brief FindService
   *
   * @param FSH
   * @return ServiceHandleContainer<SP_Handle>
   */
  static ServiceHandleContainer<SP_Handle>
  FindService(FindServiceHandle FSH); // we send to the service discovery a
                                      // request for a specific service
  /**
   * @brief SendRequest
   *
   * @tparam R
   * @tparam Args
   * @param method_id
   * @param args
   * @return R
   */
  template <typename R, typename... Args>
  // if the pass argument is not vector or string
  
  R SendRequest(uint32_t method_id, Args &&...args) {

    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            this->m_proxy_handle.m_server_com.service_id,
            (uint16_t)(method_id & 0x7F)},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::REQUEST);

    R result; // to save the result of the method
    ara::com::Serializer ser;
    ara::com::Deserializer deser;

    (ser.serialize(std::forward<Args>(args)), ...);
    // get payload of argumnets
    std::vector<uint8_t> payload_buffer = ser.Payload();
    // push the payload to the message
    R_msg.SetPayload(payload_buffer);

      // Serialize message and send over network
    std::vector<uint8_t> message_buffer = R_msg.Serializer();
    auto msg_size = (uint32_t)message_buffer.size();

    service_proxy_tcp.OpenSocket();
    service_proxy_tcp.GetHost("127.0.0.1",
                              this->m_proxy_handle.m_server_com.port_number);
    service_proxy_tcp.ClientConnect();


    service_proxy_tcp.ClientWrite((void *)&msg_size, sizeof(msg_size));
    service_proxy_tcp.ClientWrite(&message_buffer[0], msg_size);

    // receive the methods result
    uint32_t msg_size_r = 0;

    service_proxy_tcp.ClientRead((void *)&msg_size_r, sizeof(msg_size_r));
    std::vector<uint8_t> respone_buffer(msg_size_r);
    service_proxy_tcp.ClientRead((void *)&respone_buffer[0], msg_size_r);
    service_proxy_tcp.CloseSocket();

    // deserialize the result
    SOMEIP_MESSAGE::Message Res_msg =
        SOMEIP_MESSAGE::Message::Deserialize(respone_buffer);
    // get the payload
    std::vector<uint8_t> result_payload = Res_msg.GetPayload();

    // deserialize the payload
    result = deser.deserialize<R>(result_payload, 0);
    return result;
  }
  /**
   * @brief SendRequest
   *
   * @tparam R
   * @param method_id
   * @return R
   */
  template <typename R> R SendRequest(uint32_t method_id) {
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            this->m_proxy_handle.m_server_com.service_id,
            (uint16_t)method_id},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::REQUEST);

    R result; // to save the result of the method
    ara::com::Serializer ser;
    ara::com::Deserializer deser;

    ser.serialize(method_id);

    service_proxy_tcp.OpenSocket();
    service_proxy_tcp.GetHost("127.0.0.1",
                              this->m_proxy_handle.m_server_com.port_number);
    service_proxy_tcp.ClientConnect();

    // get payload of argumnets
    std::vector<uint8_t> msgser = ser.Payload();
    // push the payload to the message
    R_msg.SetPayload(msgser);
    // get the serialized message
    msgser = R_msg.Serializer();

    auto msg_size = (uint32_t) msgser.size();
    service_proxy_tcp.ClientWrite((void *)&msg_size, sizeof(msg_size));
    service_proxy_tcp.ClientWrite(&msgser[0], msg_size);
    msgser.clear();
    service_proxy_tcp.ClientRead((void *)&msg_size, sizeof(msg_size));
    msgser.resize(msg_size);
    service_proxy_tcp.ClientRead((void *)&msgser[0], msg_size);
    service_proxy_tcp.CloseSocket();

    // deserialize the result
    SOMEIP_MESSAGE::Message Res_msg =
        SOMEIP_MESSAGE::Message::Deserialize(msgser);
    // get the payload
    std::vector<uint8_t> _data_payload = Res_msg.GetPayload();
    // deserialize the payload
    result = deser.deserialize<R>(_data_payload, 0);

    return result;
  }
  /**
   * @brief SendRequest
   *
   * @param method_id
   * @return std::vector<uint8_t>
   */
  std::vector<uint8_t> SendRequest(uint32_t method_id) {
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            this->m_proxy_handle.m_server_com.service_id,
            (uint16_t)method_id},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::REQUEST);

    ara::com::Serializer ser;

    ser.serialize(method_id);

    service_proxy_tcp.OpenSocket();
    service_proxy_tcp.GetHost("127.0.0.1",
                              this->m_proxy_handle.m_server_com.port_number);
    service_proxy_tcp.ClientConnect();

    // get payload of argumnets
    std::vector<uint8_t> msgser = ser.Payload();
    // push the payload to the message
    R_msg.SetPayload(msgser);
    // get the serialized message
    msgser = R_msg.Serializer();

    auto msg_size = (uint32_t) msgser.size();
    service_proxy_tcp.ClientWrite((void *)&msg_size, sizeof(msg_size));
    service_proxy_tcp.ClientWrite(&msgser[0], msg_size);
    msgser.clear();
    service_proxy_tcp.ClientRead((void *)&msg_size, sizeof(msg_size));
    msgser.resize(msg_size);
    service_proxy_tcp.ClientRead((void *)&msgser[0], msg_size);
    service_proxy_tcp.CloseSocket();

    // deserialize the result
    SOMEIP_MESSAGE::Message Res_msg =
        SOMEIP_MESSAGE::Message::Deserialize(msgser);
    // get the payload
    std::vector<uint8_t> _data_payload = Res_msg.GetPayload();

    return _data_payload;
  }
  /**
   * @brief SendRequest
   *
   * @tparam R
   * @param method_id
   * @param data
   * @return R
   */
  template <typename R>
  R SendRequest(uint32_t method_id, std::vector<uint8_t> const& data) {
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            this->m_proxy_handle.m_server_com.service_id,
            (uint16_t)method_id},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::REQUEST);
    R_msg.SetPayload(data);

    std::vector<uint8_t> _payload = R_msg.Serializer();

    auto _payload_size = (uint32_t)_payload.size();
    ara::com::Deserializer deser;
    R result;

    service_proxy_tcp.OpenSocket();
    service_proxy_tcp.GetHost("127.0.0.1",
                              this->m_proxy_handle.m_server_com.port_number);
    service_proxy_tcp.ClientConnect();

    service_proxy_tcp.ClientWrite((void *)&_payload_size,
                                  sizeof(_payload_size));
    service_proxy_tcp.ClientWrite(&_payload[0], _payload_size);
    _payload.clear();
    _payload_size = 0;
    service_proxy_tcp.ClientRead((void *)&_payload_size, sizeof(_payload_size));
    _payload.resize(_payload_size);
    service_proxy_tcp.ClientRead((void *)&_payload[0], _payload_size);
    service_proxy_tcp.CloseSocket();

    // deserialize the result
    SOMEIP_MESSAGE::Message Res_msg =
        SOMEIP_MESSAGE::Message::Deserialize(_payload);
    // get the payload
    std::vector<uint8_t> _data_payload = Res_msg.GetPayload();
    // deserialize the payload
    result = deser.deserialize<R>(_data_payload, 0);

    return result;
  }
  /**
   * @brief SendRequest
   *
   */
  /**
  template <typename R>
   R SendRequest(uint32_t method_id, std::vector<uint8_t> data)
  {
      ara::com::Deserializer deser;
      R result;

      service_proxy_tcp.OpenSocket();
      service_proxy_tcp.GetHost("127.0.0.1",
      this->m_proxy_handle.m_server_com.port_number);
      service_proxy_tcp.ClientConnect();

      int msg_size = data.size();
      service_proxy_tcp.ClientWrite((void *)&msg_size, sizeof(msg_size));
      service_proxy_tcp.ClientWrite(&data[0], msg_size);
      std::vector<uint8_t> msgser;
      service_proxy_tcp.ClientRead((void *)&msg_size, sizeof(msg_size));
      msgser.reserve(msg_size);
      service_proxy_tcp.ClientRead((void *)&msgser[0], msg_size);
      service_proxy_tcp.CloseSocket();

      /// deserialize the result
      SOMEIP_MESSAGE::Message Res_msg =
      SOMEIP_MESSAGE::Message::Deserialize(msgser);
      /// get the payload
      std::vector<uint8_t> _data_payload = Res_msg.GetPayload();
      /// deserialize the payload
      result = deser.deserialize<R>(_data_payload,0);

      return result;
  } */
  /**
   * @brief SendFireAndForgetRequest
   *
   * @tparam Args
   * @param method_id
   * @param args
   */
  template <typename... Args>
  void SendFireAndForgetRequest(uint32_t method_id, Args &&...args) {
    ara::com::Serializer ser;
    ser.serialize(method_id);
    (ser.serialize(std::forward<Args>(args)), ...);
    service_proxy_tcp.OpenSocket();
    service_proxy_tcp.GetHost("127.0.0.1",
                              this->m_proxy_handle.m_server_com.port_number);
    service_proxy_tcp.ClientConnect();
    std::vector<uint8_t> msgser = ser.Payload();
    auto msg_size = (uint32_t) msgser.size();
    service_proxy_tcp.ClientWrite((void *)&msg_size, sizeof(msg_size));
    service_proxy_tcp.ClientWrite(&msgser[0], msg_size);
    service_proxy_tcp.CloseSocket();
  }
  /**
   * @brief SendFireAndForgetRequest
   *
   * @param method_id
   */
  void SendFireAndForgetRequest(uint32_t method_id) {
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            this->m_proxy_handle.m_server_com.service_id,
            (uint16_t)method_id},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::REQUEST);
    ara::com::Serializer ser;
    ser.serialize(method_id);

    service_proxy_tcp.OpenSocket();
    service_proxy_tcp.GetHost("127.0.0.1",
                              this->m_proxy_handle.m_server_com.port_number);
    service_proxy_tcp.ClientConnect();
    std::vector<uint8_t> msgser = ser.Payload();
    R_msg.SetPayload(msgser);
    auto msg_size = (uint32_t) msgser.size(); // sizeof = 4 bytes
    service_proxy_tcp.ClientWrite((void *)&msg_size, sizeof(msg_size));
    service_proxy_tcp.ClientWrite(&msgser[0], msg_size);
    service_proxy_tcp.CloseSocket();
  }
  /**
   * @brief EventSubscribe
   *
   * @param event_id
   */
  void EventSubscribe(uint16_t event_id) {
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(m_proxy_handle.m_server_com.port_number);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
      printf("\nInvalid address/ Address not supported \n");
    }
    uint16_t pport = htons(m_proxy_handle.UDP_port);

    ara::com::SOMEIP_MESSAGE::sd::SomeIpSDMessage m_info;
    ara::com::entry::EventgroupEntry event_gr_entry =
        ara::com::entry::EventgroupEntry::CreateSubscribeEventEntry(
            this->m_proxy_handle.m_server_com.service_id,
            this->m_proxy_handle.m_server_com.service_id,
            1, // major version
            event_id);

    auto sub_option =
        std::make_shared<ara::com::option::Ipv4EndpointOption>(
            ara::com::option::Ipv4EndpointOption::CreateSdEndpoint(
                false, ara::com::helper::Ipv4Address(127, 0, 0, 1),
                option::Layer4ProtocolType::Udp, pport));

    event_gr_entry.AddFirstOption(sub_option);
    m_info.AddEntry(&event_gr_entry);

    std::vector<uint8_t> _payload = m_info.Serializer();
    auto _payload_size = (uint32_t)_payload.size();

    service_proxy_udp.OpenSocket();
    service_proxy_udp.UDPSendTo((void *)&_payload_size, sizeof(_payload_size),
                                (sockaddr *)&serv_addr);
    service_proxy_udp.UDPSendTo((void *)&_payload[0], _payload_size,
                                (sockaddr *)&serv_addr);
    service_proxy_udp.CloseSocket();
  }
  /**
   * @brief EventUnsubscribe
   *
   * @param event_id
   */
  void EventUnsubscribe(uint16_t event_id) {
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(m_proxy_handle.m_server_com.port_number);
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
      printf("\nInvalid address/ Address not supported \n");
    }
    SOMEIP_MESSAGE::sd::SomeIpSDMessage m_info;
    entry::EventgroupEntry event_gr_entry =
        entry::EventgroupEntry::CreateUnsubscribeEventEntry(
            this->m_proxy_handle.m_server_com.service_id,
            this->m_proxy_handle.m_server_com.service_id,
            1, // major version
            event_id);

    uint16_t pport = htons(m_proxy_handle.UDP_port);
    auto sub_option =
        std::make_shared<option::Ipv4EndpointOption>(
            option::Ipv4EndpointOption::CreateSdEndpoint(
                false, helper::Ipv4Address(127, 0, 0, 1),
                option::Layer4ProtocolType::Udp, pport));

    event_gr_entry.AddFirstOption(sub_option);
    m_info.AddEntry(&event_gr_entry);

    std::vector<uint8_t> _payload = m_info.Serializer();
    auto _payload_size = (uint32_t)_payload.size();

    service_proxy_udp.OpenSocket();
    service_proxy_udp.UDPSendTo((void *)&_payload_size, sizeof(_payload_size),
                                (sockaddr *)&serv_addr);
    service_proxy_udp.UDPSendTo((void *)_payload.data(), _payload_size,
                                (sockaddr *)&serv_addr);
    service_proxy_udp.CloseSocket();
  }
  /**
   * @brief Field_get
   *
   * @param f_get
   * @param data
   */
  void Field_get(ara::com::proxy_skeleton::event_info const&f_get,
                 std::vector<uint8_t> &data) {
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(m_proxy_handle.m_server_com.port_number);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
      printf("\nInvalid address/ Address not supported \n");
    }
    ara::com::SOMEIP_MESSAGE::Message get_msg(
        {(f_get.service_id), (uint16_t)(f_get.event_id | 0x8000)},
        {5, 6},
        5, // protocol verion
        6, // interface version
        ara::com::SOMEIP_MESSAGE::MessageType::REQUEST);
    std::vector<uint8_t> _payload = get_msg.Serializer();
    auto _payload_size = (uint32_t) _payload.size();

    service_proxy_tcp.OpenSocket();
    service_proxy_tcp.GetHost("127.0.0.1",
                              this->m_proxy_handle.m_server_com.port_number);
    service_proxy_tcp.ClientConnect();
    service_proxy_tcp.ClientWrite((void *)&_payload_size,
                                  sizeof(_payload_size));
    service_proxy_tcp.ClientWrite((void *)_payload.data(), _payload_size);

    _payload.clear();
    service_proxy_tcp.ClientRead((void *)&_payload_size, sizeof(_payload_size));
    _payload.resize(_payload_size);
    service_proxy_tcp.ClientRead((void *)_payload.data(), _payload_size);
    service_proxy_tcp.CloseSocket();

    ara::com::SOMEIP_MESSAGE::Message R_get_msg =
        ara::com::SOMEIP_MESSAGE::Message::Deserialize(_payload);
    data = R_get_msg.GetPayload();
  }
  /**
   * @brief Field_set
   *
   * @param f_set
   * @param data
   */
  void Field_set(ara::com::proxy_skeleton::event_info const&f_set,
                 std::vector<uint8_t> &data) {
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(m_proxy_handle.m_server_com.port_number);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
      printf("\nInvalid address/ Address not supported \n");
    }
    ara::com::SOMEIP_MESSAGE::Message set_msg(
        {(uint16_t)f_set.service_id, (uint16_t)(f_set.event_id | 0x8000)},
        {5, 6},
        5, // protocol verion
        6, // interface version
        ara::com::SOMEIP_MESSAGE::MessageType::REQUEST);
    set_msg.SetPayload(data);
    std::vector<uint8_t> _payload = set_msg.Serializer();
    auto _payload_size = (uint32_t)_payload.size();

    service_proxy_tcp.OpenSocket();
    service_proxy_tcp.GetHost("127.0.0.1",
                              this->m_proxy_handle.m_server_com.port_number);
    service_proxy_tcp.ClientConnect();
    service_proxy_tcp.ClientWrite((void *)&_payload_size,
                                  sizeof(_payload_size));
    service_proxy_tcp.ClientWrite((void *)_payload.data(), _payload_size);

    _payload.clear();
    service_proxy_tcp.ClientRead((void *)&_payload_size, sizeof(_payload_size));
    _payload.resize(_payload_size);
    service_proxy_tcp.ClientRead((void *)_payload.data(), _payload_size);
    service_proxy_tcp.CloseSocket();

    ara::com::SOMEIP_MESSAGE::Message R_get_msg =
        ara::com::SOMEIP_MESSAGE::Message::Deserialize(_payload);
    data = R_get_msg.GetPayload();
  }
};

} // namespace ara::com::proxy_skeleton::proxy

#endif // ARA_COM_INTERNAL_SERVICE_PROXY_H_
