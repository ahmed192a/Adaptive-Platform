/**
 * @file SomeipSDMessage.cpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/com/SOMEIP/SomeipSDMessage.h"
#include "ara/com/SOMEIP/entry/entry_desrializer.h"
#include "ara/com/SOMEIP/option/option_desrializer.h"
#include <cstdint>
#include <iostream>
using namespace std;
namespace ara::com::SOMEIP_MESSAGE::sd {
SomeIpSDMessage::SomeIpSDMessage()
    : Message(MID, RID, ProtocolVersion, InterfaceVersion, Messagetype),
      Rebooted{true} {}

uint32_t SomeIpSDMessage::getEntriesLength() const noexcept {
  const uint32_t EntrySize = 16;

  return static_cast<uint32_t>(entries.size()) * EntrySize;
}

uint32_t SomeIpSDMessage::getOptionsLength() const noexcept {
  const uint32_t OptionLengthFieldSize = 3;
  uint32_t result = 0;

  for (auto const entry : entries) {
    for (auto const firstOption : entry->FirstOptions()) {
      result += OptionLengthFieldSize + firstOption->Length();
    }

    for (auto const secondOption : entry->SecondOptions()) {
      result += OptionLengthFieldSize + secondOption->Length();
    }
  }

  return result;
}

const std::vector<entry::Entry *> &SomeIpSDMessage::Entries() const noexcept {
  return entries;
}

void SomeIpSDMessage::AddEntry(entry::Entry *entry) {
  entries.push_back(entry);
}

uint32_t SomeIpSDMessage::Length() const noexcept {
  // [PRS_SOMEIPSD_00153]: bytes after the Length field. That is the rest of
  // the SOME/IP header (8), Flags + Reserved (4), the entries array length
  // field (4) + entries, and the options array length field (4) + options.
  const uint32_t LengthFieldSize = 4;
  const uint32_t SdHeaderSize = 4;

  return EmptyLength + SdHeaderSize + LengthFieldSize + getEntriesLength() +
         LengthFieldSize + getOptionsLength();
}

void SomeIpSDMessage::SetSessionId(uint16_t sessionId) {
  if (sessionId == 0) {
    throw std::invalid_argument(
        "Session ID cannot be zero for the SD message.");
  }

  Message::SetSessionId(sessionId);
}

bool SomeIpSDMessage::IncrementSessionId() noexcept {
  bool wrapped = Message::IncrementSessionId();
  // If the message is rebooted, check for wrapping
  Rebooted &= !wrapped;

  return wrapped;
}

std::vector<uint8_t> SomeIpSDMessage::Serializer() {

  // General SOME/IP header payload insertion
  std::vector<uint8_t> _result = Message::Serializer();

  // Flags byte: Reboot (0x80) and Unicast (0x40, always 1)
  // [PRS_SOMEIPSD_00254, PRS_SOMEIPSD_00259]; all other bits (incl. the
  // Explicit Initial Data Control bit removed in R21-11) and the 24 reserved
  // bits are zero [PRS_SOMEIPSD_00702, PRS_SOMEIPSD_00261].
  const uint32_t RebootedFlag = 0xc0000000;
  const uint32_t NotRebootedFlag = 0x40000000;
  helper::Inject(_result, Rebooted ? RebootedFlag : NotRebootedFlag);
  uint8_t lastOptionIndex = 0;
  std::vector<uint8_t> entriesPayload;
  std::vector<uint8_t> optionsPayload;
  for (auto const entry : entries) {

    auto entryPayload = entry->Payload(lastOptionIndex);
    helper::Concat(entriesPayload, std::move(entryPayload));

    for (auto firstOption : entry->FirstOptions()) {

      auto firstOptionPayload = firstOption->Payload();
      helper::Concat(optionsPayload, std::move(firstOptionPayload));
    }

    for (auto secondOption : entry->SecondOptions()) {

      auto secondOptionPayload = secondOption->Payload();
      helper::Concat(optionsPayload, std::move(secondOptionPayload));
    }
  }

  // Entries length and payloads insertion
  uint32_t entriesLength = getEntriesLength();
  helper::Inject(_result, entriesLength);
  helper::Concat(_result, std::move(entriesPayload));

  // Options length and payloads insertion
  uint32_t optionsLength = getOptionsLength();
  helper::Inject(_result, optionsLength);
  helper::Concat(_result, std::move(optionsPayload));

  // The base class wrote the payload-less Length; patch in the real one.
  const uint32_t length = Length();
  _result[4] = static_cast<uint8_t>(length >> 24);
  _result[5] = static_cast<uint8_t>(length >> 16);
  _result[6] = static_cast<uint8_t>(length >> 8);
  _result[7] = static_cast<uint8_t>(length);

  return _result;
}

namespace {
uint32_t Be32(const std::vector<uint8_t> &v, size_t o) {
  return (static_cast<uint32_t>(v[o]) << 24) |
         (static_cast<uint32_t>(v[o + 1]) << 16) |
         (static_cast<uint32_t>(v[o + 2]) << 8) | static_cast<uint32_t>(v[o + 3]);
}
} // namespace

bool SomeIpSDMessage::Deserialize(const std::vector<uint8_t> &payload) {
  const size_t EntrySize = 16;
  const size_t OptionHeaderSize = 3; // Length(2) + Type(1)
  // 16 byte header + flags/reserved + entries length field
  if (payload.size() < 16 + 4 + 4) {
    return false;
  }
  size_t offset = 16;
  // Flags are the first byte, the highest bit is the Reboot flag
  // [PRS_SOMEIPSD_00254].
  const bool rebooted = (payload[offset] & 0x80) != 0;
  offset += 4;

  const uint64_t entriesLength = Be32(payload, offset);
  offset += 4;
  if (entriesLength % EntrySize != 0 ||
      offset + entriesLength + 4 > payload.size()) {
    return false;
  }
  const size_t entriesStart = offset;
  offset += static_cast<size_t>(entriesLength);

  const uint64_t optionsLength = Be32(payload, offset);
  offset += 4;
  if (offset + optionsLength > payload.size()) {
    return false;
  }
  const size_t optionsStart = offset;

  // Options array: walk by the on-wire Length field so that option types we
  // do not parse can still be skipped (their slot stays nullptr).
  std::vector<std::shared_ptr<ara::com::option::Option>> options;
  size_t optionsOffset = 0;
  while (optionsOffset < optionsLength) {
    if (optionsOffset + OptionHeaderSize > optionsLength) {
      return false;
    }
    const size_t o = optionsStart + optionsOffset;
    const size_t len = (static_cast<size_t>(payload[o]) << 8) | payload[o + 1];
    if (optionsOffset + OptionHeaderSize + len > optionsLength) {
      return false;
    }
    std::vector<uint8_t> optionBytes(payload.begin() + o,
                                     payload.begin() + o + OptionHeaderSize + len);
    std::shared_ptr<ara::com::option::Option> option;
    // The option parsers read fixed layouts; only hand them complete ones.
    if (len >= 9) {
      option = option::OptionDeserializer::Deserialize(optionBytes, 0);
    }
    options.push_back(option);
    optionsOffset += OptionHeaderSize + len;
  }

  this->Rebooted = rebooted;
  for (size_t e = 0; e < entriesLength; e += EntrySize) {
    const size_t o = entriesStart + e;
    // Index of first/second option run, then #opt1 (high) / #opt2 (low).
    const size_t firstIndex = payload[o + 1];
    const size_t secondIndex = payload[o + 2];
    const size_t firstCount = (payload[o + 3] >> 4) & 0x0f;
    const size_t secondCount = payload[o + 3] & 0x0f;

    std::vector<uint8_t> entryBytes(payload.begin() + o,
                                    payload.begin() + o + EntrySize);
    auto entry = entry::Entry_Deserializer::Deserialize(entryBytes, 0);
    if (entry == nullptr) {
      continue; // unknown entry type: ignore it
    }
    for (size_t i = 0; i < firstCount; ++i) {
      if (firstIndex + i < options.size() && options[firstIndex + i]) {
        entry->AddFirstOption(options[firstIndex + i]);
      }
    }
    for (size_t i = 0; i < secondCount; ++i) {
      if (secondIndex + i < options.size() && options[secondIndex + i]) {
        entry->AddSecondOption(options[secondIndex + i]);
      }
    }
    this->entries.push_back(entry);
  }
  return true;
}

} // namespace ara::com::SOMEIP_MESSAGE::sd
