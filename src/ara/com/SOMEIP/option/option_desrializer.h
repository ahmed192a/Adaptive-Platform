/**
 * @file option_desrializer.hpp
 * @author
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef OPTION_DESRIALIZER_H
#define OPTION_DESRIALIZER_H
#include "ara/com/SOMEIP/option/ipv4_endpoint_option.h"
#include "ara/com/SOMEIP/option/option.h"
#include <cstdint>
#include <memory>
namespace ara::com::option {
/**
 * @brief Class for Option deserialization
 *
 */
class OptionDeserializer {
public:
  /**
   * @brief Deserialize Option from byte array
   *
   * @param payload   byte array containing Option payload
   * @param offset    offset in byte array
   * @return Option*  pointer to deserialized Option
   */

  static std::shared_ptr<Option>
  Deserialize(const std::vector<uint8_t> &payload,
              std::size_t offset) noexcept {
    offset += 2;
    const auto optionType = static_cast<OptionType>(payload[offset]);
    offset += 1;
    const bool discardable = (payload[offset] & 0x80) != 0;
    offset += 1;

    switch (optionType) {
    case OptionType::IPv4Endpoint: {
      const helper::Ipv4Address ipAddress(payload[offset], payload[offset + 1],
                                          payload[offset + 2],
                                          payload[offset + 3]);
      offset += 4;
      offset += 1;
      const uint8_t protocolByte = payload[offset];
      offset += 1;
      auto port = static_cast<uint16_t>((payload[offset] << 8) | payload[offset + 1]);
      return std::make_shared<Ipv4EndpointOption>(
          Ipv4EndpointOption::CreateUnitcastEndpoint(
              discardable, ipAddress,
              static_cast<Layer4ProtocolType>(protocolByte), port));
    }
    case OptionType::IPv4Multicast: {
      const helper::Ipv4Address ipAddress(payload[offset], payload[offset + 1],
                                          payload[offset + 2],
                                          payload[offset + 3]);
      offset += 4;
      offset += 1;
      offset += 1;
      auto port = static_cast<uint16_t>((payload[offset] << 8) | payload[offset + 1]);
      return std::make_shared<Ipv4EndpointOption>(
          Ipv4EndpointOption::CreateMulticastEndpoint(discardable, ipAddress,
                                                      port));
    }
    case OptionType::IPv4SdEndpoint: {
      const helper::Ipv4Address ipAddress(payload[offset], payload[offset + 1],
                                          payload[offset + 2],
                                          payload[offset + 3]);
      offset += 4;
      offset += 1;
      const uint8_t protocolByte = payload[offset];
      offset += 1;
      auto port = static_cast<uint16_t>((payload[offset] << 8) | payload[offset + 1]);
      return std::make_shared<Ipv4EndpointOption>(
          Ipv4EndpointOption::CreateSdEndpoint(
              discardable, ipAddress,
              static_cast<Layer4ProtocolType>(protocolByte), port));
    }
    default: {
      return nullptr;
    }
    }
  }
};

} // namespace ara::com::option
#endif // OPTION_DESRIALIZER_H