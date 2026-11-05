/**
 * @file entry.cpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/com/SOMEIP/entry/entry.h"
#include <algorithm>
#include <cstdint>

namespace ara::com::entry {
Entry::Entry(EntryType type, uint16_t serviceId, uint16_t instanceId,
             uint32_t ttl, uint8_t majorVersion) noexcept
    : GBType{type}, GBServiceId{serviceId}, GBInstanceId{instanceId},
      GBMajorVersion{majorVersion}, GBTTL{ttl} {}

bool Entry::ValidateOption(const option::Option *option) const noexcept {
  bool _result;

  switch (option->Type()) {
  case option::OptionType::Configuration: {
    bool _containsConfiguration =
        ContainsOption(option::OptionType::Configuration);

    // Each entry can only have at maximum one configuration option.
    _result = !_containsConfiguration;

    break;
  }
  case option::OptionType::LoadBalancing: {
    bool _containsLoadBalancing =
        ContainsOption(option::OptionType::LoadBalancing);

    // Each entry can only have at maximum one load balancing option.
    _result = !_containsLoadBalancing;

    break;
  }
  case option::OptionType::IPv4SdEndpoint:
  case option::OptionType::IPv6SdEndpoint: {

    _result = true;

    break;
  }
  default: {
    // Other options cannot be validated in the base entry class.
    _result = true;

    break;
  }
  }

  return _result;
}

bool Entry::ContainsOption(option::OptionType optionType) const noexcept {
  bool found = std::any_of(GBFirstOptions.cbegin(), GBFirstOptions.cend(),
                           [optionType](const auto &option) {
                             return option->Type() == optionType;
                           });
  if (found) {
    return true;
  }
  return std::any_of(GBSecondOptions.cbegin(), GBSecondOptions.cend(),
                     [optionType](const auto &option) {
                       return option->Type() == optionType;
                     });
}

EntryType Entry::Type() const noexcept { return GBType; }

uint16_t Entry::ServiceId() const noexcept { return GBServiceId; }

uint16_t Entry::InstanceId() const noexcept { return GBInstanceId; }

uint8_t Entry::MajorVersion() const noexcept { return GBMajorVersion; }

uint32_t Entry::TTL() const noexcept { return GBTTL; }

const std::vector<std::shared_ptr<option::Option>> &
Entry::FirstOptions() const noexcept {
  return GBFirstOptions;
}

void Entry::AddFirstOption(std::shared_ptr<option::Option> firstOption) {

  GBFirstOptions.push_back(firstOption);
}

const std::vector<std::shared_ptr<option::Option>> &
Entry::SecondOptions() const noexcept {
  return GBSecondOptions;
}

void Entry::AddSecondOption(std::shared_ptr<option::Option> secondOption) {

  GBSecondOptions.push_back(secondOption);
}

std::vector<uint8_t> Entry::BasePayload(uint8_t &optionIndex) const {
  std::vector<uint8_t> _result;

  auto _type = static_cast<uint8_t>(Type());
  _result.push_back(_type);

  // Each run holds at most 15 options (4 bit counters).
  if (FirstOptions().size() > 15 || SecondOptions().size() > 15) {
    throw std::length_error("an SD entry can reference at most 15 options per run");
  }
  auto _firstOptionsSize = (uint8_t)FirstOptions().size();
  auto _secondOptionsSize = (uint8_t)SecondOptions().size();

  // An empty run must carry index 0 (option referencing rules,
  // PRS_SOMEIPSD_00341-00343, 00833, 00834).
  _result.push_back(_firstOptionsSize == 0 ? 0 : optionIndex);
  optionIndex += _firstOptionsSize;

  _result.push_back(_secondOptionsSize == 0 ? 0 : optionIndex);
  optionIndex += _secondOptionsSize;

  const uint8_t OptionSizeBitLength = 4;
  _firstOptionsSize <<= OptionSizeBitLength;
  _firstOptionsSize |= _secondOptionsSize;
  _result.push_back(_firstOptionsSize);

  helper::Inject(_result, ServiceId());
  helper::Inject(_result, InstanceId());

  const uint8_t TTLSizeBitLength = 24;
  uint32_t _majorVersion = MajorVersion();
  _majorVersion <<= TTLSizeBitLength;
  _majorVersion |= TTL();
  helper::Inject(_result, _majorVersion);

  return _result;
}
} // namespace ara::com::entry
