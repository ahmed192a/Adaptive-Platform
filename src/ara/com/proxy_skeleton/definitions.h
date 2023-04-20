/**
 * @file definitions.hpp
 * @author
 * @brief
 * @version 0.1
 * @date 2022-02-18
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_COM_PROXY_SKELETON_DEFINITIONS_H_
#define ARA_COM_PROXY_SKELETON_DEFINITIONS_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ara::com::proxy_skeleton {

using ServiceVersion = std::uint32_t; //!< Definition of service version type

using InstanceId =
    std::uint16_t; //!< Definition of proxy_skeleton instance ID type.

using ServiceId =
    std::uint16_t; //!< Definition of proxy_skeleton service ID type.

static const InstanceId INSTANCE_ID_UNKNOWN =
    0xFFFF; //!< Definition of Unkown (NULL) instance ID type.

struct SD_data {
  uint16_t service_id;
  uint16_t instance_id;
  uint16_t port_number;
  bool operator==(SD_data const &other) const {
    if (service_id == other.service_id && instance_id == other.instance_id &&
        port_number == other.port_number) {
      return true;
    }
    return false;
  }
};

struct event_info {
  uint16_t service_id;
  uint16_t event_id;
};
template <typename T> struct event_notify {
  uint16_t event_id;
  uint16_t service_id;
  T newdata;
};
struct Client_udp_Info {
  uint16_t port;
  std::string addr;
  bool operator==(const Client_udp_Info &other) const {
    if (port == other.port && addr == other.addr)
      return true;
    return false;
  }
};

} // namespace ara::com::proxy_skeleton

#endif // ARA_COM_PROXY_SKELETON_DEFINITIONS_H_
