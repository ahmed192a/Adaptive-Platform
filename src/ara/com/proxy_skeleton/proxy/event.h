/**
 * @file event.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Base class for all events.
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_COM_PROXY_SKELETON_PROXY_EVENT_H_
#define ARA_COM_PROXY_SKELETON_PROXY_EVENT_H_

#include "ara/com/deserializer.h"
#include "ara/com/proxy_skeleton/proxy/service_proxy.h"
namespace ara::com::proxy_skeleton::proxy {
/**
 * @brief Event Base class for all events.
 *
 * @tparam T
 */
template <typename T> class Event {
private:
  ServiceProxy *m_service; /*!< pointer to service proxy */
  std::string m_name;      /*!< name of the event */
  T event_data;            /*!< event data */
  uint32_t m_event_id;     /*!< id of the event */

public:
  /**
   * @brief Construct a new Event object
   *
   * @param service   pointer to service proxy
   * @param name      name of the event
   * @param event_id  id of the event
   */
  Event(ServiceProxy *service, const std::string &name, uint32_t event_id)
      : m_service{service}, m_name{name}, m_event_id{event_id} {}

  /**
   * @brief get the id of the event (unique identifier)
   *
   * @return uint32_t
   */
  uint32_t Get_event_id() const { return m_event_id; }

  /**
   * @brief Subscribe to event
   *
   */
  void Subscribe() {
    m_service->EventSubscribe(m_event_id);
    std::cout << "subscribe -- \n";
  }

  /**
   * @brief Unsubscribe from event
   *
   */
  void UnSubscribe() { m_service->EventUnsubscribe(m_event_id); }

  /**
   * @brief Get the Event Data object
   *
   * @param msg   message to be deserialized
   */
  void handlecall(const std::vector<uint8_t> &msg) {
    ara::com::Deserializer dser;
    int offset = 0;
    event_data = dser.deserialize<T>(msg, offset);
  }

  /**
   * @brief Get the value object
   *
   * @return const T&
   */
  const T &get_value() const { return event_data; }
}; // Event

} // namespace ara::com::proxy_skeleton::proxy

#endif // ARA_COM_PROXY_SKELETON_PROXY_EVENT_H_
