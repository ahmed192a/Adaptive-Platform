/**
 * @file field.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date 2022-02-19
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef ARA_COM_proxy_skeleton_SKELETON_FIELD_H_
#define ARA_COM_proxy_skeleton_SKELETON_FIELD_H_

#include <bits/stdc++.h>
#include <cstdint>

#include "ara/com/proxy_skeleton/skeleton/event.h"
namespace ara::com::proxy_skeleton::skeleton {
/**
    template <typename T>
    using FieldSetHandler = std::function<T(const T &data)>;

    template <typename T>
    using FieldGetHandler = std::function<T()>;
*/

/**
 * @brief class field -->  has getter ,setter , notifier
 *
 * @tparam T
 *
 */
template <typename T> class Field : public Event<T> {

  public:
  /**
   * @brief Construct a new Field object
   * @todo edit all other fields
   * @param service
   * @param name
   * @param field_id
   */
  Field(ServiceSkeleton *service, std::string const& name, uint16_t field_id)
      : Event<T>(service, name, field_id){}
  /**
   * @brief Handle set method
   *
   * @param sd_msg
   * @param binding
   */
  void HandleSet(ara::com::SOMEIP_MESSAGE::Message const& sd_msg, Socket &binding) {
    std::vector<uint8_t> _data = sd_msg.GetPayload();
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            (uint16_t)this->m_service->m_service_id,
            (uint16_t)(sd_msg.MessageId().method_id | 0x8000)},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::RESPONSE);

    ara::com::Deserializer dser;
    int offset = 0;
    this->event_data = dser.deserialize<T>(_data, offset);
    // Setpayload in message
    R_msg.SetPayload(_data);
    _data = R_msg.Serializer();
    auto msg_size = (uint32_t)_data.size();
    // send message
    binding.Send(&msg_size, sizeof(msg_size));
    binding.Send(_data.data(), msg_size);
    binding.CloseSocket();
    this->notify();
  }
  /**
   * @brief Handle Get method
   *
   * @param sd_msg
   * @param binding
   */
  void HandleGet(ara::com::SOMEIP_MESSAGE::Message const& sd_msg, Socket &binding) {
    std::vector<uint8_t> _data;
    SOMEIP_MESSAGE::Message R_msg(
        SOMEIP_MESSAGE::Message_ID{
            (uint16_t)this->m_service->m_service_id,
            (uint16_t)(sd_msg.MessageId().method_id | 0x8000)},
        SOMEIP_MESSAGE::Request_ID{5, 6},
        2, // protocol version
        7, // Interface Version
        SOMEIP_MESSAGE::MessageType::RESPONSE);

    ara::com::Serializer ser;
    ser.serialize(this->event_data);
    _data = ser.Payload();

    // Setpayload in message
    R_msg.SetPayload(_data);
    _data = R_msg.Serializer();
    auto msg_size = (uint32_t)_data.size();
    // send message
    binding.Send(&msg_size, sizeof(msg_size));
    binding.Send(_data.data(), msg_size);
    binding.CloseSocket();
  }

};

/**
 * @brief class FieldNoSetter has get method
 *
 * @tparam T
 */
template <typename T> class FieldNoSetter : public Field<T> {
public:
  using  Field<T>::Field;
   void HandleSet(ara::com::SOMEIP_MESSAGE::Message const& sd_msg, Socket &binding) = delete;
};

/**
 * @brief class FieldNoGetter to set method
 *
 * @tparam T
 */
template <typename T> class FieldNoGetter : public Field<T> {
public:
using  Field<T>::Field;
  void HandleGet(ara::com::SOMEIP_MESSAGE::Message const& sd_msg, Socket &binding)= delete;
};

/**
 * @brief class Field No Getter And Setter --> update event
 *
 * @tparam T
 */
template <typename T> class FieldNoGetterAndSetter : public Field<T> {
public:
using  Field<T>::Field;
  void HandleGet(ara::com::SOMEIP_MESSAGE::Message const& sd_msg, Socket &binding) = delete;
  void HandleSet(ara::com::SOMEIP_MESSAGE::Message const& sd_msg, Socket &binding) = delete;
  // /**
  //  * @brief update event
  //  *
  //  * @param data
  //  */
  // void Update(const T &data) {
  //   this->m_service->SendEvent(this->m_name, data, true);
  // }
};

/**
 * @brief class Field No Notifier --> update event
 *
 * @tparam T
 */
template <typename T> class FieldNoNotifier : public Field<T> {
public:
  using  Field<T>::Field;
  /**
   * @brief update event
   *
   * @param value
   */
  void update(T value) override {
    this->event_data = value;
    std::cout << this->m_name << " is Udpated " << std::endl;
  }
};

/**
 * @brief class Field No Notifier And Setter --> to handle get then update
 *
 * @tparam T
 */
template <typename T> class FieldNoNotifierAndSetter : public Field<T> {
public:
using  Field<T>::Field;
  void HandleSet(ara::com::SOMEIP_MESSAGE::Message const& sd_msg, Socket &binding) = delete;
  /**
   * @brief update event
   *
   * @param value
   */
  void update(T value) override {
    this->event_data = value;
    std::cout << this->m_name << " is Udpated " << std::endl;
  }
};
/**
 * @brief class Field No Notifier And Getter
 *
 * @tparam T
 */
template <typename T> class FieldNoNotifierAndGetter : public Field<T> {
public:
using  Field<T>::Field;
  void HandleGet(ara::com::SOMEIP_MESSAGE::Message const& sd_msg, Socket &binding) = delete;
  
  /**
   * @brief update event
   *
   * @param value
   */
  void update(T value) override {
    this->event_data = value;
    std::cout << this->m_name << " is Udpated " << std::endl;
  }
  
};

/**
 * @brief Interface for creating field opject ddepending on the input
 * (getter/notifier/setter)
 *
 * @tparam T
 * @tparam hasGetter
 * @tparam hasNotifier
 * @tparam hasSetter
 */
template <typename T, bool hasGetter, bool hasSetter, bool hasNotifier>
struct FieldType {
  using type = std::conditional_t<
      hasGetter && hasSetter && hasNotifier, Field<T>,
      std::conditional_t<
          hasGetter && hasSetter && !hasNotifier, FieldNoNotifier<T>,
          std::conditional_t<
              hasGetter && !hasSetter && hasNotifier, FieldNoSetter<T>,
              std::conditional_t<
                  hasGetter && !hasSetter && !hasNotifier,
                  FieldNoNotifierAndSetter<T>,
                  std::conditional_t<
                      !hasGetter && hasSetter && hasNotifier, FieldNoGetter<T>,
                      std::conditional_t<!hasGetter && hasSetter &&
                                             !hasNotifier,
                                         FieldNoNotifierAndGetter<T>,
                                         FieldNoGetterAndSetter<T>>>>>>>;
};


} // namespace ara::com::proxy_skeleton::skeleton

#endif // ARA_COM_proxy_skeleton_SKELETON_FIELD_H_
