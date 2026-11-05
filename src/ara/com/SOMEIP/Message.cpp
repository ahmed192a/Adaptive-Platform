/**
 * @file Message.cpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/com/SOMEIP/Message.h"
#include <cstdint>
#include <cstring>
#include <unistd.h>

namespace ara::com::SOMEIP_MESSAGE {
Message::Message(struct Message_ID mID, struct Request_ID rID,
                 uint8_t protocol_version, uint8_t interface_version,
                 enum MessageType Mtype, enum ReturnCode Rcode) noexcept
    : header{mID.serivce_id,
             mID.method_id,
             EmptyLength,
             rID.client_id,
             rID.session_id,
             protocol_version,
             interface_version,
             static_cast<uint8_t>(Mtype),
             static_cast<uint8_t>(Rcode)}

{}
Message::Message(struct Message_ID mID, struct Request_ID rID,
                 uint8_t protocol_version, uint8_t interface_version,
                 enum MessageType Mtype) noexcept
    : Message(mID, rID, protocol_version, interface_version, Mtype,
              ReturnCode::E_OK) {}

Message::Message(Request_ID rID, Message_ID mID, uint8_t protocol_version,
                 uint8_t interface_version, ReturnCode Rcode,
                 MessageType Mtype) noexcept
    : Message(mID, rID, protocol_version, interface_version, Mtype, Rcode) {}
struct Message_ID Message::MessageId() const noexcept {
  return {(uint16_t)header.service_id, (uint16_t)header.method_id};
}

uint16_t Message::ClientId() const noexcept { return header.client_id; }

uint16_t Message::SessionId() const noexcept { return header.session_id; }

void Message::SetSessionId(uint16_t sessionId) {
  header.session_id = sessionId;
}

bool Message::IncrementSessionId() noexcept {
  // Session ID range is [0x0001, 0xFFFF] and wraps to 0x0001
  // [PRS_SOMEIP_00933, PRS_SOMEIP_00521].
  const uint16_t SessionIdMin = 1;
  constexpr uint16_t SessionIdMax = std::numeric_limits<uint16_t>::max();

  if (header.session_id == SessionIdMax) {
    header.session_id = SessionIdMin;
    return true;
  } else {
    header.session_id++;
    return false;
  }
}

uint8_t Message::ProtocolVersion() const noexcept {
  return header.protocol_version;
}

uint8_t Message::InterfaceVersion() const noexcept {
  return header.interface_version;
}

MessageType Message::Messagetype() const noexcept {
  return static_cast<MessageType>(header.message_type);
}

ReturnCode Message::Returncode() const noexcept {
  return static_cast<ReturnCode>(header.return_code);
}

uint32_t Message::Length() const noexcept { return header.length; }

std::vector<uint8_t> Message::Serializer() {
  // All header fields are encoded big endian [PRS_SOMEIP_00368].
  std::vector<uint8_t> result;
  result.reserve(HeaderSize + payload.size());
  helper::Inject(result, header.service_id);
  helper::Inject(result, header.method_id);
  helper::Inject(result, header.length);
  helper::Inject(result, header.client_id);
  helper::Inject(result, header.session_id);
  result.push_back(header.protocol_version);
  result.push_back(header.interface_version);
  result.push_back(header.message_type);
  result.push_back(header.return_code);

  result.insert(result.end(), payload.begin(), payload.end());
  return result;
}

void Message::SetPayload(std::vector<uint8_t> const &data) {
  payload = data;
  header.length = EmptyLength + static_cast<uint32_t>(payload.size());
}

std::vector<uint8_t> Message::GetPayload() const { return payload; }

namespace {
uint16_t Be16(const std::vector<uint8_t> &v, size_t o) {
  return static_cast<uint16_t>((v[o] << 8) | v[o + 1]);
}
uint32_t Be32(const std::vector<uint8_t> &v, size_t o) {
  return (static_cast<uint32_t>(v[o]) << 24) |
         (static_cast<uint32_t>(v[o + 1]) << 16) |
         (static_cast<uint32_t>(v[o + 2]) << 8) | static_cast<uint32_t>(v[o + 3]);
}
} // namespace

std::optional<Message>
Message::TryDeserialize(const std::vector<uint8_t> &msg) noexcept {
  if (msg.size() < HeaderSize) {
    return std::nullopt;
  }
  header_t h;
  h.service_id = Be16(msg, 0);
  h.method_id = Be16(msg, 2);
  h.length = Be32(msg, 4);
  h.client_id = Be16(msg, 8);
  h.session_id = Be16(msg, 10);
  h.protocol_version = msg[12];
  h.interface_version = msg[13];
  h.message_type = msg[14];
  h.return_code = msg[15];
  // Length counts everything after the Length field [PRS_SOMEIP_00042].
  if (h.length < EmptyLength ||
      static_cast<uint64_t>(h.length) + 8 != msg.size()) {
    return std::nullopt;
  }
  try {
    return Message(h, std::vector<uint8_t>(msg.begin() + HeaderSize, msg.end()));
  } catch (...) {
    return std::nullopt;
  }
}

Message Message::Deserialize(const std::vector<uint8_t> &msg) {
  auto result = TryDeserialize(msg);
  if (!result) {
    throw std::invalid_argument("malformed SOME/IP message");
  }
  return *result;
}

ReturnCode Message::ValidateHeader() const noexcept {
  if (header.protocol_version != kProtocolVersion) {
    return ReturnCode::E_WRONG_PROTOCOL_VERSION;
  }
  switch (static_cast<MessageType>(header.message_type)) {
  case MessageType::REQUEST:
  case MessageType::REQUEST_NO_RETURN:
  case MessageType::NOTIFICATION:
  case MessageType::RESPONSE:
  case MessageType::ERROR:
  case MessageType::TP_REQUEST:
  case MessageType::TP_REQUEST_NO_RETURN:
  case MessageType::TP_NOTIFICATION:
  case MessageType::TP_RESPONSE:
  case MessageType::TP_ERROR:
    return ReturnCode::E_OK;
  }
  return ReturnCode::E_WRONG_MESSAGE_TYPE;
}

} // namespace ara::com::SOMEIP_MESSAGE
