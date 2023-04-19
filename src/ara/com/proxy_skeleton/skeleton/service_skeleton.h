/**
 * @file service_skeleton.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date 2022-02-19
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef ARA_COM_proxy_skeleton_SKELETON_SERVICE_SKELETON_H_
#define ARA_COM_proxy_skeleton_SKELETON_SERVICE_SKELETON_H_

#include "ara/com/SOMEIP/SomeipSDMessage.h"
#include "ara/com/SOMEIP/option/ipv4_endpoint_option.h"
#include "ara/com/ipc/client/socket_Client.h"
#include "ara/com/ipc/server/socket_Server.h"
#include "ara/com/marshal.h"
#include "ara/com/proxy_skeleton/data_type.h"
#include "ara/com/serializer.h"
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <unistd.h>
#include <utility>

namespace ara::com::proxy_skeleton::skeleton {
/**
 * @brief class ServiceSkeleton
 *
 */
class ServiceSkeleton {
public:
  /**
   * @struct ServiceSkeleton::SK_Handle
   * @brief ServiceSkeleton handles the communication with the service
   *
   */
  struct SK_Handle {

    uint16_t service_portnum; //!< service port number
    uint16_t sd_portnum;      //!< Service Discovery port number
  };
  SK_Handle m_skeleton_handle;                       //!< skeleton handle
  ara::com::proxy_skeleton::ServiceId &m_service_id; //!< service id
  ara::com::InstanceIdentifier &m_instance_id;       //!< instance id

  /**
   * @brief Construct a new Service Skeleton object
   *
   * @param service_id            service id of the service
   * @param instance              instance id of the service
   * @param skeleton_handle       skeleton handle
   * @param mode                  mode of the service
   */
  ServiceSkeleton(
      ara::com::proxy_skeleton::ServiceId &service_id,
      InstanceIdentifier &instance, SK_Handle const &skeleton_handle,
      MethodCallProcessingMode mode = MethodCallProcessingMode::kEvent)
      : m_skeleton_handle{skeleton_handle}, m_service_id(service_id),
        m_instance_id(instance), m_mode{mode},
        cliaddr{AF_INET, htons(m_skeleton_handle.sd_portnum), INADDR_ANY} {}

  /**
   * @brief Get the Service Id object
   *
   * @return const ara::com::proxy_skeleton::ServiceId
   */
  ara::com::proxy_skeleton::ServiceId GetServiceId() const {
    return m_service_id;
  }
  /**
   * @brief Offer Service
   *
   */
  void OfferService() {
    const uint32_t minorV = 0;
    const uint8_t majorV = 1;

    SOMEIP_MESSAGE::sd::SomeIpSDMessage sd_msg;

    entry::ServiceEntry offer_service_e =
        entry::ServiceEntry::CreateOfferServiceEntry(
            m_service_id, m_instance_id.GetInstanceId(), majorV, minorV);

    auto optionA = std::make_shared<option::Ipv4EndpointOption>(
        option::Ipv4EndpointOption::CreateSdEndpoint(
            false, helper::Ipv4Address(127, 0, 0, 1),
            option::Layer4ProtocolType::Udp,
            m_skeleton_handle.service_portnum));

    offer_service_e.AddFirstOption(optionA);
    sd_msg.AddEntry(&offer_service_e);

    std::vector<uint8_t> _payload = sd_msg.Serializer();
    uint32_t _size = _payload.size();

    this->m_skeleton_udp.OpenSocket();
    this->m_skeleton_udp.UDPSendTo((void *)&_size, sizeof(_size),
                                   (struct sockaddr *)&this->cliaddr);
    this->m_skeleton_udp.UDPSendTo((void *)_payload.data(), _payload.size(),
                                   (struct sockaddr *)&this->cliaddr);
    this->m_skeleton_udp.CloseSocket();
  }

  /**
   * @brief StopOfferService
   *
   */
  void StopOfferService() {
    const uint32_t minorV = 0;
    const uint8_t majorV = 1;
    this->m_skeleton_udp.OpenSocket();

    SOMEIP_MESSAGE::sd::SomeIpSDMessage sd_msg;

    entry::ServiceEntry stop_offer_e =
        entry::ServiceEntry::CreateStopOfferEntry(
            m_service_id, m_instance_id.GetInstanceId(), majorV, minorV);
    sd_msg.AddEntry(&stop_offer_e);
    std::vector<uint8_t> _payload = sd_msg.Serializer();
    uint32_t _size = _payload.size();
    this->m_skeleton_udp.UDPSendTo((void *)&_size, sizeof(_size),
                                   (struct sockaddr *)&this->cliaddr);
    this->m_skeleton_udp.UDPSendTo((void *)_payload.data(), _payload.size(),
                                   (struct sockaddr *)&this->cliaddr);
    this->m_skeleton_udp.CloseSocket();
  }

  /**
   * @brief SendEvent     send an event update to clients
   * @tparam T            type of the event
   * @param event_id      event id
   * @param data          data of the event
   * @param client_add    client address
   */
  template <typename T>
  void SendEvent(uint16_t event_id, const T &data, sockaddr_in client_add) {
    /* Serialize the data */
    ara::com::Serializer ser;
    ser.serialize(data);
    std::vector<uint8_t> sermsg = ser.Payload();
    /* Fill the event info object */
    ara::com::proxy_skeleton::event_info msg_;
    msg_.service_id = m_service_id;
    msg_.event_id = event_id;
    /* send the event info object then send the serialized data */
    std::cout << "send event to client " << client_add.sin_port << std::endl;
    // Create Mesaage {, event_id | 0x8000}, NOTIFICATION
    ara::com::SOMEIP_MESSAGE::Message M1(
        {this->m_service_id, (uint16_t)(event_id | 0x8000)}, {1, 2}, 3, 4,
        ara::com::SOMEIP_MESSAGE::MessageType::NOTIFICATION);

    M1.SetPayload(sermsg);

    std::vector<uint8_t> __payload = M1.Serializer();
    // size of vector
    uint32_t size = __payload.size();

    this->m_skeleton_udp.OpenSocket();
    this->m_skeleton_udp.UDPSendTo((void *)&size, sizeof(size),
                                   (sockaddr *)&client_add);
    this->m_skeleton_udp.UDPSendTo((void *)&__payload[0], __payload.size(),
                                   (sockaddr *)&client_add);
    this->m_skeleton_udp.CloseSocket();
  }
  /**
   * @brief NoServiceHandler --> handle error
   *
   * @param S_msg         --> message from client
   * @param binding       --> binding of the client
   */
  static void NoServiceHandler(SOMEIP_MESSAGE::Message const &S_msg,
                               Socket &binding) {
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Request_ID{5, 6},
        SOMEIP_MESSAGE::Message_ID{S_msg.MessageId().serivce_id,
                                   S_msg.MessageId().method_id},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::ReturnCode::E_UNKNOWN_SERVICE,
        SOMEIP_MESSAGE::MessageType::ERROR);

    std::vector<uint8_t> _Payload = R_msg.Serializer();
    uint32_t size = _Payload.size();
    binding.Send(&size, sizeof(size));
    binding.Send(_Payload.data(), size);
    binding.CloseSocket();
  }

protected:
  /**
   * @brief 1- HandleCall  R ARGS
   *
   * @tparam Class        Class to call the method on
   * @tparam R            Return type of the method
   * @tparam Args         Argument types of the method
   * @param c             object of type Class to call the method on
   * @param method        method to be called
   * @param msg           message from client
   * @param binding       binding of the client
   */
  template <typename Class, typename R, typename... Args>
  void HandleCall(Class &c, std::future<R> (Class::*method)(Args...),
                   const SOMEIP_MESSAGE::Message &msg, Socket &binding)               
  {
    Marshal marshal;
    auto arg_payload = msg.GetPayload();
    std::cout << "i am here 1"<<std::endl;
    
    auto args = marshal.get_args<Args...>(arg_payload);
    auto f_result =
        std::apply(method, std::tuple_cat(std::make_tuple(&c), args));
    R result = f_result.get();
    Serializer ser;
    ser.serialize(result);
    std::vector<uint8_t> payload = ser.Payload();


    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            this->m_service_id, (uint16_t)(msg.MessageId().method_id & 0x7fff)},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::RESPONSE);
    R_msg.SetPayload(payload);
    std::vector<uint8_t> _Payload = R_msg.Serializer();

    uint32_t size = _Payload.size();
    binding.Send(&size, sizeof(size));
    binding.Send(&_Payload[0], size);
    binding.CloseSocket();
  }
  /**/
  /**
   * @brief 2- HandleCall future<void> ARGS
   *
   * @tparam Class        Class to call the method on
   * @tparam Args         Argument types of the method
   * @param c             object of type Class to call the method on
   * @param method        method to be called
   * @param msg           message from client
   * @param binding       binding of the client
   */
  template <typename Class, typename... Args>
  void HandleCall(Class &c, std::future<void> (Class::*method)(Args...),
                  SOMEIP_MESSAGE::Message msg, Socket &binding) {
    // sHandleCall(c, method, msg, binding, std::index_sequence_for<Args...>());
    Marshal marshal;
    std::cout << "i am here 2"<<std::endl;

    auto arg_payload = msg.GetPayload();
    auto args = marshal.get_args<Args...>(arg_payload);
    auto f_result =
        std::apply(method, std::tuple_cat(std::make_tuple(&c), args));
    f_result.get();

    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            this->m_service_id, (uint16_t)(msg.MessageId().method_id & 0x7fff)},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::RESPONSE);
    std::vector<uint8_t> _Payload = R_msg.Serializer();
    uint32_t size = _Payload.size();
    binding.Send(&size, sizeof(size));
    binding.Send(_Payload.data(), size);
    binding.CloseSocket();
  }
  /**
   * @brief 3- HandleCall R
   *
   * @tparam Class        Class to call the method on
   * @tparam R            Return type of the method
   * @param c             object of type Class to call the method on
   * @param method        method to be called
   * @param msg           message from client
   * @param binding       binding of the client
   */
  template <typename Class, typename R>
  void HandleCall(Class &c, std::future<R> (Class::*method)(),
                  SOMEIP_MESSAGE::Message msg, Socket &binding) {
    std::cout << "i am here 3"<<std::endl;

    std::future<R> f = (c.*method)();
    R result = f.get();
    // create serialize object
    ara::com::Serializer s1;
    // serialize the result
    s1.serialize(result);
    // create SOME IP message
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
             this->m_service_id,
            (uint16_t)(msg.MessageId().method_id & 0x7fff)},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::RESPONSE);
    // Setpayload in message
    std::vector<uint8_t> _payload = s1.Payload();
    R_msg.SetPayload(_payload);
    _payload = R_msg.Serializer();
    uint32_t msg_size = _payload.size();
    // send message
    binding.Send(&msg_size, sizeof(msg_size));
    binding.Send(_payload.data(), msg_size);

    binding.CloseSocket();
  }
  /**
   * @brief 4- HandleCall future void
   *
   * @tparam Class        Class to call the method on
   * @param c             object of type Class to call the method on
   * @param method        method to be called
   * @param msg           message from client
   * @param binding       binding of the client
   */
  template <typename Class>
  void HandleCall(Class &c, std::future<void> (Class::*method)(),
                  SOMEIP_MESSAGE::Message msg, Socket &binding) {

    std::cout << "i am here 4"<<std::endl;
    
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{this->m_service_id,
                                   msg.MessageId().method_id},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::RESPONSE);
    std::future<void> f = (c.*method)();
    f.get();
    std::vector<uint8_t> _payload = R_msg.Serializer();
    uint32_t msg_size = _payload.size();
    binding.Send(&msg_size, sizeof(msg_size));
    binding.Send(_payload.data(), msg_size);
    binding.CloseSocket();
  }
  /**
   * @brief 5- HandleCall void ARGS
   *
   * @tparam Class    Class to call the method on
   * @tparam Args     Argument types of the method
   * @param c         object of type Class to call the method on
   * @param method    method to be called
   * @param msg       message from client
   * @param binding   binding of the client
   */
  template <typename Class, typename... Args>
  void HandleCall(Class &c, void (Class::*method)(Args...),
                  SOMEIP_MESSAGE::Message msg, Socket &binding) {
    std::cout << "i am here 5"<<std::endl;

    // sHandleCall(c, method, msg, binding, std::index_sequence_for<Args...>());
    Marshal marshal;
    auto arg_payload = msg.GetPayload();
    auto args = marshal.get_args<Args...>(arg_payload);
    std::apply(method, std::tuple_cat(std::make_tuple(&c), args));

    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            this->m_service_id, (uint16_t)(msg.MessageId().method_id & 0x7fff)},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::RESPONSE);
    std::vector<uint8_t> _Payload = R_msg.Serializer();
    uint32_t size = _Payload.size();
    binding.Send(&size, sizeof(size));
    binding.Send(_Payload.data(), size);
    binding.CloseSocket();
  }
  /**
   * @brief 6- HandleCall void
   *
   * @tparam Class    Class to call the method on
   * @param c         object of type Class to call the method on
   * @param method    method to be called
   * @param msg       message from client
   * @param binding   binding of the client
   */
  template <typename Class>
  void HandleCall(Class &c, void (Class::*method)(),
                  SOMEIP_MESSAGE::Message msg, Socket &binding) {
    binding.CloseSocket();
    (c.*method)();
  }
  /**
   * @brief No Handler --> handle error
   *
   * @param method_id     method id of the method that was called
   * @param binding       binding of the client
   */
  void NoMethodHandler(uint16_t method_id, Socket &binding) {
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Request_ID{5, 6},
        SOMEIP_MESSAGE::Message_ID{this->m_service_id, method_id},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::ReturnCode::E_UNKNOWN_METHOD,
        SOMEIP_MESSAGE::MessageType::ERROR);

    std::vector<uint8_t> _Payload = R_msg.Serializer();
    uint32_t size = _Payload.size();
    binding.Send(&size, sizeof(size));
    binding.Send(_Payload.data(), size);
    binding.CloseSocket();
  }

private:
  // Private variables
  ara::com::MethodCallProcessingMode m_mode;
  struct sockaddr_in cliaddr;
  CClient m_skeleton_udp{SOCK_DGRAM};
};

} // namespace ara::com::proxy_skeleton::skeleton

#endif // ARA_COM_proxy_skeleton_SKELETON_SERVICE_SKELETON_H_
