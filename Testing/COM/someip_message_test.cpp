// SOME/IP header conformance tests. Expected byte sequences are written out by
// hand from AUTOSAR_FO_PRS_SOMEIPProtocol (R25-11) so that they do not depend
// on the implementation under test.
#include "ara/com/SOMEIP/Message.h"
#include <gtest/gtest.h>

using namespace ara::com::SOMEIP_MESSAGE;

namespace {
Message MakeRequest(std::vector<uint8_t> const &payload = {}) {
  Message m(Message_ID{0x1234, 0x0056}, Request_ID{0x00AB, 0x0102},
            kProtocolVersion, 0x03, MessageType::REQUEST);
  if (!payload.empty()) m.SetPayload(payload);
  return m;
}
} // namespace

TEST(SomeIpHeader, EncodesBigEndianWithLengthEight) {
  // [PRS_SOMEIP_00368] big endian, [PRS_SOMEIP_00042] Length counts from the
  // Request ID: 8 bytes for an empty payload.
  auto bytes = MakeRequest().Serializer();
  std::vector<uint8_t> expected = {0x12, 0x34, 0x00, 0x56,  // Message ID
                                   0x00, 0x00, 0x00, 0x08,  // Length
                                   0x00, 0xAB, 0x01, 0x02,  // Request ID
                                   0x01,                    // Protocol Version
                                   0x03,                    // Interface Version
                                   0x00,                    // REQUEST
                                   0x00};                   // E_OK
  EXPECT_EQ(bytes, expected);
}

TEST(SomeIpHeader, LengthCoversPayload) {
  auto m = MakeRequest({1, 2, 3, 4, 5});
  EXPECT_EQ(m.Length(), 13u);
  auto bytes = m.Serializer();
  ASSERT_EQ(bytes.size(), 21u);
  EXPECT_EQ(bytes[7], 13);
  // Setting the payload again must replace, not accumulate.
  m.SetPayload({9});
  EXPECT_EQ(m.Length(), 9u);
}

TEST(SomeIpHeader, RoundTrip) {
  auto m = MakeRequest({0xDE, 0xAD, 0xBE, 0xEF});
  auto back = Message::Deserialize(m.Serializer());
  EXPECT_EQ(back.MessageId().serivce_id, 0x1234);
  EXPECT_EQ(back.MessageId().method_id, 0x0056);
  EXPECT_EQ(back.ClientId(), 0x00AB);
  EXPECT_EQ(back.SessionId(), 0x0102);
  EXPECT_EQ(back.ProtocolVersion(), 1);
  EXPECT_EQ(back.InterfaceVersion(), 3);  // was returning the protocol version
  EXPECT_EQ(back.Messagetype(), MessageType::REQUEST);
  EXPECT_EQ(back.Returncode(), ReturnCode::E_OK);
  EXPECT_EQ(back.GetPayload(), (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
}

TEST(SomeIpHeader, MessageTypeValues) {
  // [PRS_SOMEIP_00055]
  EXPECT_EQ(static_cast<uint8_t>(MessageType::REQUEST), 0x00);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::REQUEST_NO_RETURN), 0x01);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::NOTIFICATION), 0x02);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::RESPONSE), 0x80);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::ERROR), 0x81);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::TP_REQUEST), 0x20);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::TP_REQUEST_NO_RETURN), 0x21);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::TP_NOTIFICATION), 0x22);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::TP_RESPONSE), 0xA0);
  EXPECT_EQ(static_cast<uint8_t>(MessageType::TP_ERROR), 0xA1);
}

TEST(SomeIpHeader, SessionIdWrapsFromFFFFToOne) {
  // [PRS_SOMEIP_00933] range 0x0001..0xFFFF, [PRS_SOMEIP_00521] wrap to 1.
  auto m = MakeRequest();
  m.SetSessionId(0x00FF);
  EXPECT_FALSE(m.IncrementSessionId());
  EXPECT_EQ(m.SessionId(), 0x0100);  // previously wrapped at 0xFF
  m.SetSessionId(0xFFFF);
  EXPECT_TRUE(m.IncrementSessionId());
  EXPECT_EQ(m.SessionId(), 1);
}

TEST(SomeIpHeader, MalformedInputIsRejected) {
  EXPECT_FALSE(Message::TryDeserialize({}).has_value());
  EXPECT_FALSE(Message::TryDeserialize(std::vector<uint8_t>(15, 0)).has_value());
  auto bytes = MakeRequest({1, 2, 3}).Serializer();
  auto shortened = bytes;
  shortened.pop_back();  // Length now exceeds the buffer
  EXPECT_FALSE(Message::TryDeserialize(shortened).has_value());
  auto extended = bytes;
  extended.push_back(0);  // trailing garbage
  EXPECT_FALSE(Message::TryDeserialize(extended).has_value());
  bytes[7] = 3;  // Length below the minimum of 8
  EXPECT_FALSE(Message::TryDeserialize(bytes).has_value());
  EXPECT_THROW(Message::Deserialize(shortened), std::invalid_argument);
}

TEST(SomeIpHeader, ValidateHeader) {
  EXPECT_EQ(MakeRequest().ValidateHeader(), ReturnCode::E_OK);
  Message wrongVersion(Message_ID{1, 1}, Request_ID{1, 1}, 2, 1,
                       MessageType::REQUEST);
  EXPECT_EQ(wrongVersion.ValidateHeader(), ReturnCode::E_WRONG_PROTOCOL_VERSION);
  Message wrongType(Message_ID{1, 1}, Request_ID{1, 1}, kProtocolVersion, 1,
                    static_cast<MessageType>(0x55));
  EXPECT_EQ(wrongType.ValidateHeader(), ReturnCode::E_WRONG_MESSAGE_TYPE);
}

TEST(SomeIpHeader, EventIdsHaveBit15Set) {
  // Recommendation in [PRS_SOMEIP_00245]: methods 0x0000-0x7FFF, events 0x8xxx.
  Message method(Message_ID{1, 0x0001}, Request_ID{1, 1}, kProtocolVersion, 1,
                 MessageType::REQUEST);
  Message event(Message_ID{1, 0x8001}, Request_ID{1, 1}, kProtocolVersion, 1,
                MessageType::NOTIFICATION);
  EXPECT_TRUE(method.check_Methode_ID());
  EXPECT_FALSE(event.check_Methode_ID());
}

TEST(SomeIpHeader, ReturnCodeValues) {
  // Values follow the sequential table of [PRS_SOMEIP_00191]; the table body
  // was not retrievable from the R25-11 PDF text, so this checks the layout
  // used by the previous revisions of the specification (0x00..0x0F).
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_OK), 0x00);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_NOT_OK), 0x01);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_UNKNOWN_SERVICE), 0x02);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_UNKNOWN_METHOD), 0x03);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_NOT_READY), 0x04);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_NOT_REACHABLE), 0x05);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_TIMEOUT), 0x06);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_WRONG_PROTOCOL_VERSION), 0x07);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_WRONG_INTERFACE_VERSION), 0x08);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_MALFORMED_MESSAGE), 0x09);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_WRONG_MESSAGE_TYPE), 0x0A);
  EXPECT_EQ(static_cast<uint8_t>(ReturnCode::E_E2E_NO_NEW_DATA), 0x0F);
}
