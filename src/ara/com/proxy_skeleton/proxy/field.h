/**
 * @file field.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Base class for all fields.
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_COM_PROXY_SKELETON_PROXY_FIELD_H_
#define ARA_COM_PROXY_SKELETON_PROXY_FIELD_H_

#include "ara/com/deserializer.h"
#include "ara/com/proxy_skeleton/proxy/event.h"

namespace ara::com::proxy_skeleton::proxy {
/**
 * @brief Field Base class for all fields.
 *
 * @tparam T
 */
template <typename T> class Field : public Event<T> {
public:
  /**
   * @brief Construct a new Field object
   *
   * @param service   pointer to service proxy
   * @param name      name of the field
   * @param field_id  id of the field
   */
  Field(ServiceProxy *service, std::string name, uint32_t field_id)
      : Event<T>(service, name, field_id), m_service{service}, m_name{name},
        m_field_id{field_id} {}

  /**
   * @brief Get the value of the field
   *
   * @return T
   */
  T Get() {
    T value;
    ara::com::Deserializer dser;
    std::vector<uint8_t> data;
    data.resize(sizeof(value));
    ara::com::proxy_skeleton::event_info e_get;
    e_get.event_id = m_field_id;
    e_get.service_id = m_service->m_proxy_handle.m_server_com.service_id;
    m_service->Field_get(e_get, data);
    value = dser.deserialize<T>(data, 0);
    return value;
  }
  /**
   * @brief Set the value of the field
   *
   * @param value     value to be set
   * @return T
   */
  T Set(T &value) {
    ara::com::Serializer ser;
    ser.serialize(value);
    std::vector<uint8_t> data = ser.Payload();
    ara::com::proxy_skeleton::event_info e_set;
    e_set.event_id = m_field_id;
    e_set.service_id = m_service->m_proxy_handle.m_server_com.service_id;
    m_service->Field_set(e_set, data);
    return value;
  }

private:
  ServiceProxy *m_service; /*!< pointer to service proxy */
  std::string m_name;      /*!< name of the field */
  uint32_t m_field_id;     /*!< id of the field */
};
/**
 * @brief FieldNoSetter Class for field with no setter.
 *
 * @tparam T
 */
template <typename T> class FieldNoSetter : public Field<T> {
public:
  void Set(T &value) = delete;
};
/**
 * @brief FieldNoGetter Class for field with no getter.
 *
 * @tparam T
 */
template <typename T> class FieldNoGetter : public Field<T> {
public:
  T Get() = delete;
};
/**
 * @brief FieldNoGetterAndSetter Class for field with no getter and no setter.
 *
 * @tparam T
 */
template <typename T> class FieldNoGetterAndSetter : public Field<T> {
public:
  T Get() = delete;
  void Set(T &value) = delete;
};
/**
 * @brief FieldNoNotifier Class for field with no getter and no setter.
 *
 * @tparam T
 */
template <typename T> class FieldNoNotifier : public Field<T> {};
/**
 * @brief FieldNoNotifierAndSetter Class for field with no getter and no setter.
 *
 * @tparam T
 */
template <typename T> class FieldNoNotifierAndSetter : public Field<T> {

public:
  void Set(T &value) = delete;
};
/**
 * @brief FieldNoNotifierAndGetter Class for field with no getter and no setter.
 *
 * @tparam T
 */
template <typename T> class FieldNoNotifierAndGetter : public Event<T> {
public:
  T Get() = delete;
};
/**
 * @brief FieldType Class for field.
 *
 * @tparam T            type of the field
 * @tparam hasGetter    true if field has getter
 * @tparam hasSetter    true if field has setter
 * @tparam hasNotifier  true if field has notifier
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

} // namespace ara::com::proxy_skeleton::proxy

#endif // ARA_COM_INTERNAL_PROXY_FIELD_H_
