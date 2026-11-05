/**
 * @file Message.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#ifndef ARA_COM_SOMEIP_MESSAGE_HPP
#define ARA_COM_SOMEIP_MESSAGE_HPP
#include "ara/com/SOMEIP/helper/ipv4_address.h"
#include "ara/com/SOMEIP/helper/payload_helper.h"
#include "ara/com/SOMEIP/helper/ttl_timer.h"
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <stdint.h>
#include <vector>

namespace ara::com::SOMEIP_MESSAGE {
/**
 * @enum MessageType
 *
 * @brief MessageType enumaration is used to identify the type of the message in
 * SOME IP
 *
 */
/// SOME/IP protocol version carried in every header [PRS_SOMEIP_00051].
constexpr uint8_t kProtocolVersion = 0x01;

enum class MessageType : uint8_t {
  REQUEST = 0x00,           /**< A request expecting a response (even void)	*/
  REQUEST_NO_RETURN = 0x01, /**< A fire&forget request	*/
  NOTIFICATION = 0x02, /**< A request of a notification/event callback expecting
                          no response	*/
  RESPONSE = 0x80,     /**< The response message	*/
  ERROR = 0x81,        /**< The response containing an error	*/
  TP_REQUEST = 0x20,   /**< A TP request expecting a response (even void)	*/
  TP_REQUEST_NO_RETURN = 0x21, /**< A TP fire & forget request	*/
  TP_NOTIFICATION = 0x22, /**< A TP request of a notification/event callback
                             expecting no response	*/
  TP_RESPONSE = 0xA0,     /**< The TP response message [PRS_SOMEIP_00055]	*/
  TP_ERROR = 0xA1         /**< The TP response containing an error [PRS_SOMEIP_00055]	*/
};
/**
 * @enum ReturnCode
 * @brief ReturnCode enumaration is used to identify the return code of the
 * response message SOME IP
 *
 */
enum class ReturnCode : uint8_t {
  E_OK,              /**<No error occurred	*/
  E_NOT_OK,          /**<An unspecified error occurred	*/
  E_UNKNOWN_SERVICE, /**<The requested Service ID is unknown	*/
  E_UNKNOWN_METHOD, /**<The requested Method ID is unknown. Service ID is known.
                     */
  E_NOT_READY, /**<Service ID and Method ID are known. Application not running
                */
  E_NOT_REACHABLE, /**<System running the service is not reachable
                      (proxy_skeleton error code only).	*/
  E_TIMEOUT,       /**<A timeout occurred (proxy_skeleton error code only).	*/
  E_WRONG_PROTOCOL_VERSION,  /**<Version of SOME/IP protocol not supported
                              */
  E_WRONG_INTERFACE_VERSION, /**<Interface version mismatch	*/
  E_MALFORMED_MESSAGE,  /**<Deserialization error, so that payload cannot be
                           deserialized.	*/
  E_WRONG_MESSAGE_TYPE, /**< An unexpected message type was received (e.g.
                           REQUEST_NO_RETURN for a method defined as REQUEST).
                         */
  E_E2E_REPEATED,       /**<Repeated E2E calculation error	*/
  E_E2E_WRONG_SEQUENCE, /**<Wrong E2E sequence error	*/
  E_E2E,                /**<Not further specified E2E error	*/
  E_E2E_NOT_AVAILABLE,  /**<E2E not available	*/
  E_E2E_NO_NEW_DATA     /**<No new data for E2E calculation present */
};
/**
 * @struct Message_ID
 * @brief Message_ID struct is used to identify the service id and method id of
 * the message
 * @var Message_ID::service_id
 * Member 'service_id' is used to identify the service id of the message
 * @var Message_ID::method_id
 * Member 'method_id' is used to identify the method id of the message
 */
struct Message_ID {
  uint16_t serivce_id;
  uint16_t method_id = 0;
};
/**
 * @struct Request_ID
 * @brief Request_ID struct is used to identify the client id and session id of
 * the request message
 * @var Request_ID::client_id
 * Member 'client_id' is used to identify the client id of the request message
 * @var Request_ID::session_id
 * Member 'session_id' is used to identify the session id of the request message
 */
struct Request_ID {

  uint16_t client_id;
  uint16_t session_id = 1;
};


/**
 * @brief Class SOME IP Message
 *
 */
class Message  {
private:
  /// Host-order copy of the SOME/IP header. On the wire every field is big
  /// endian [PRS_SOMEIP_00368]; see Serializer()/TryDeserialize().
  struct header_t {
    uint16_t service_id;
    uint16_t method_id;
    uint32_t length;  //!< bytes from Request ID to the end of the payload
    uint16_t client_id;
    uint16_t session_id;
    uint8_t protocol_version;
    uint8_t interface_version;
    uint8_t message_type;
    uint8_t return_code;
  };
  header_t header;
  std::vector<uint8_t>
      payload; //!< Payload of the message used to store the serialized message

  /**
   * @brief Construct a new Message object
   *
   * @param mID 						message ID
   * @param rID 						request ID
   * @param protocol_version 			protocol version
   * @param interface_version 		interface version
   * @param Mtype 					message type
   * @param Rcode 					return code
   */
  Message(struct Message_ID mID, struct Request_ID rID,
          uint8_t protocol_version, uint8_t interface_version,
          MessageType Mtype, ReturnCode Rcode) noexcept;
  Message(header_t header, std::vector<uint8_t> const& payload)
      : header(header), payload(payload) {}

public:
  static const uint32_t HeaderSize = 16; //!< Header size in bytes
  /// Value of the Length field for an empty payload: Request ID, Protocol
  /// Version, Interface Version, Message Type and Return Code
  /// [PRS_SOMEIP_00042].
  static const uint32_t EmptyLength = 8;

  /**
   * @brief Construct a new Message object
   *
   * @param mID 						message ID
   * @param rID 						request ID
   * @param protocol_version 			protocol version
   * @param interface_version  		interface version
   * @param Mtype 					message type
   */
  Message(Message_ID mID, Request_ID rID, uint8_t protocol_version,
          uint8_t interface_version, MessageType Mtype) noexcept;
  /**
   * @brief Construct a new Message object
   *
   * @param rID 						request ID
   * @param mID 						message ID
   * @param protocol_version 			protocol version
   * @param interface_version 		interface version
   * @param Rcode 					return code
   * @param Mtype 					message type
   */
  Message(Request_ID rID, Message_ID mID, uint8_t protocol_version,
          uint8_t interface_version, ReturnCode Rcode,
          MessageType Mtype) noexcept;

  virtual ~Message() noexcept = default;

  /// @brief Get message ID
  /// @returns Message ID consisting service and method/event ID
  struct Message_ID MessageId() const noexcept;

  /// @brief Get message payload
  /// @returns Byte array
  virtual std::vector<uint8_t> Serializer();

  /// @brief Get message length
  /// @returns Message length including the payload length
  virtual uint32_t Length() const noexcept;

  /// @brief Get client ID
  /// @returns Client ID including ID prefix
  uint16_t ClientId() const noexcept;

  /// @brief Get session ID
  /// @returns Active/non-active session ID
  uint16_t SessionId() const noexcept;

  /// @brief Set a new session ID
  /// @param sessionId New session ID
  virtual void SetSessionId(uint16_t sessionId);

  /// @brief Increment the session ID by one
  /// @returns True if the session ID is wrappered; otherwise false
  /// @note In the case of wrapping, session ID will start from 1
  virtual bool IncrementSessionId() noexcept;

  /// @brief Get protocol version
  /// @returns SOME/IP protocol header version
  uint8_t ProtocolVersion() const noexcept;

  /// @brief Get interface version
  /// @returns Service interface version
  uint8_t InterfaceVersion() const noexcept;

  /// @brief Get message type
  /// @returns SOME/IP message type
  MessageType Messagetype() const noexcept;

  /// @brief Get return code
  /// @returns SOME/IP message return code
  ReturnCode Returncode() const noexcept;

  /// @brief Set message payload
  /// @param data Byte array
  void SetPayload(std::vector<uint8_t> const &data);

  /// @brief Get message data payload
  /// @returns Byte array
  std::vector<uint8_t> GetPayload() const;

  /// @brief Deserialize a complete SOME/IP message (header + payload)
  /// @param msg Byte array in network byte order
  /// @throws std::invalid_argument when the buffer is not a well-formed message
  static Message Deserialize(const std::vector<uint8_t> &msg);

  /// @brief Non-throwing variant of Deserialize()
  /// @returns The message, or std::nullopt when the buffer is shorter than the
  ///          header or when the Length field does not match the buffer size
  static std::optional<Message>
  TryDeserialize(const std::vector<uint8_t> &msg) noexcept;

  /// @brief Check the header against what a receiver must accept
  /// @returns E_OK, E_WRONG_PROTOCOL_VERSION or E_WRONG_MESSAGE_TYPE
  /// @note Interface version / message type matching against a concrete
  ///       service is the responsibility of the dispatcher.
  ReturnCode ValidateHeader() const noexcept;

  bool check_Methode_ID() const {
    if ((uint16_t)(this->header.method_id & 0x8000) == 0)
      return true;
    else
      return false;
  }
};

} // namespace ara::com::SOMEIP_MESSAGE

#endif
