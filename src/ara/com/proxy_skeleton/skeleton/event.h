/**
 * @file event.hpp
 * @author Flashing Adapter Graduation Project Team
 * @brief
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_COM_PROXY_SKELETON_SKELETON_EVENT_H_
#define ARA_COM_PROXY_SKELETON_SKELETON_EVENT_H_

#include "ara/com/SOMEIP/entry/eventgroup_entry.h"
#include "ara/com/deserializer.h"
#include "ara/com/proxy_skeleton/skeleton/service_skeleton.h"
#include "ara/com/proxy_skeleton/skeleton/shared_sub/file.h"
#include "ara/com/serializer.h"
#include <algorithm>
#include <arpa/inet.h>
#include <functional>
#include <iterator>
#include <set>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
/**
 * @brief
 * @todo  mutex on read and write file subcribers
 */
namespace ara::com::proxy_skeleton::skeleton {

template <typename T> class Event {


protected:
  /**
   * @brief
   *
   * @todo edit the sigval to carry any type not just int
   *
   */

  void notify() {
    auto subs = subscribers_data.getrows(subname_file.data());
    for (auto const &subsriber : subs) {
      sockaddr_in fg;
      fg.sin_family = AF_INET;
      fg.sin_port = subsriber.port;
      inet_pton(AF_INET, subsriber.addr.data(), &fg.sin_addr);
      m_service->SendEvent<T>(m_event_id, event_data, fg);
    }
  }

public:
  ServiceSkeleton *m_service;
  std::string m_name;
  uint16_t m_event_id;
  T event_data;
  Subscriber subscribers_data;
  std::string subname_file;

  /**
   * @brief Construct a new Event object
   *
   * @param service
   * @param name
   * @param event_id
   */
  Event(ServiceSkeleton *service, std::string const &name, uint16_t event_id)
      : m_service{service}, m_name{name}, m_event_id{event_id} {
    subname_file =
        m_name + std::to_string(m_service->m_service_id) + std::to_string(event_id);
    subscribers_data.clear(subname_file.data());
  }
  /**
   * @brief Destroy the Event object
   *
   */
  ~Event() = default;

  /**
   * @brief return the id of the event
   *
   * @return int
   */
  int Get_event_id() const { return m_event_id; }

  /**
   * @brief get the event variable
   */
  T get_event() { return event_data; }

  /**
   * @brief Set the subscriber object
   *
   * @param client_id
   */
  void
  set_subscriber(ara::com::proxy_skeleton::Client_udp_Info const &client_id) {

    subscribers_data.write(subname_file.data(), client_id);
  }
  /**
   * @brief Delete subscriber
   *
   * @param client_id
   */
  void
  Del_subscriber(ara::com::proxy_skeleton::Client_udp_Info const &client_id) {

    subscribers_data.delete_record(subname_file.data(), client_id);
  }
  /**
   * @brief print_subscribers
   *
   */
  void print_subscribers() {
    if (subscribers_data.getrows(subname_file.data()).empty()) {
      std::cout << "the subscribers list of " << m_name << " is empty\n";
      return;
    }
    std::cout << "the subscribers of " << m_name << " are : \n";
    std::vector<ara::com::proxy_skeleton::Client_udp_Info> subs =
        subscribers_data.getrows(subname_file.data());
    for (auto const &subscriber : subs) {
      std::cout << "\t\t=> "
                << ": " << subscriber.port << "\n";
    }
  }
  /**
   * @brief update event and notify
   *
   * @param value
   */
  virtual void update(T value) {
    event_data = value;
    std::cout << m_name << " is Udpated " << std::endl;

    notify();
  }
  /**
   * @brief subhandlecall to get entry and option of some ip
   *
   * @param sd_msg
   * @param client
   */
  void
  subhandlecall(ara::com::SOMEIP_MESSAGE::sd::SomeIpSDMessage const &sd_msg,
                ara::com::proxy_skeleton::Client_udp_Info &client) {

    auto entry = (ara::com::entry::EventgroupEntry *)sd_msg.Entries()[0];
    auto option =
        std::dynamic_pointer_cast<ara::com::option::Ipv4EndpointOption>(
            entry->FirstOptions()[0]);
    client.port = option->Port();
    uint32_t TTL = entry->TTL();

    if (TTL == 0) {
      std::cout << "\t\t Unsubscribe from " << m_name << " event" << std::endl;
      Del_subscriber(client);
      print_subscribers();
    } else {
      std::cout << "\t\t Subscribe to " << m_name << " event" << std::endl;
      set_subscriber(client);
      print_subscribers();
    }
  }

  /**
   * @brief get_subscribers
   *
   * @return std::vector<ara::com::proxy_skeleton::Client_udp_Info>
   */
  std::vector<ara::com::proxy_skeleton::Client_udp_Info> getsub() {
    return subscribers_data.getrows(subname_file.data());
  }

}; // Event
} // namespace ara::com::proxy_skeleton::skeleton

#endif // ARA_COM_INTERNAL_SKELETON_EVENT_H_
