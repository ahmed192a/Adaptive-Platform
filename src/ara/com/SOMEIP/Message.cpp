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
             16,
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
  const uint8_t SessionIdMin = 1;
  constexpr uint8_t SessionIdMax = std::numeric_limits<uint8_t>::max();

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
  return header.protocol_version;
}

MessageType Message::Messagetype() const noexcept {
  return static_cast<MessageType>(header.message_type);
}

ReturnCode Message::Returncode() const noexcept {
  return static_cast<ReturnCode>(header.return_code);
}

uint32_t Message::Length() noexcept { return header.length; }

std::vector<uint8_t> Message::Serializer() {
  std::vector<uint8_t> result(HeaderSize, 0);
  memcpy(result.data(), &header, HeaderSize);

  // concatenate result and payload and put them in the result
  result.insert(result.end(), payload.begin(), payload.end());

  return result;
}
void Message::SetPayload(std::vector<uint8_t> const &data) {
  payload = data;
  header.length += payload.size();
}

std::vector<uint8_t> Message::GetPayload() const { return payload; }

Message Message::Deserialize(const std::vector<uint8_t> &msg) {
  header_t header;
  memcpy(&header, msg.data(), HeaderSize);
  std::vector<uint8_t> payload;
  if (header.length > HeaderSize) {
    payload = {msg.begin() + HeaderSize, msg.begin() + header.length};
  }
  return Message(header, payload);
}

} // namespace ara::com::SOMEIP_MESSAGE

