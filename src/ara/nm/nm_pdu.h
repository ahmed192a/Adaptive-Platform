/**
 * @file nm_pdu.h
 * @brief 8 byte NM message (CAN-NM like layout).
 *
 *   byte 0     source node id
 *   byte 1     control bit vector: bit0 repeat-message-request, bit4 partial-network-info;
 *              other bits are reserved (written 0, ignored on receive)
 *   bytes 2..5 partial network request bitmap, little endian (valid iff PNI is set)
 *   bytes 6..7 user data
 */
#ifndef ARA_NM_NM_PDU_H_
#define ARA_NM_NM_PDU_H_

#include <array>
#include <cstdint>

#include "ara/core/result.h"

namespace ara::nm {

struct NmPdu {
  static constexpr std::size_t kSize = 8;
  using Bytes = std::array<std::uint8_t, kSize>;

  std::uint8_t sourceNodeId{0};
  bool repeatMessageRequest{false};
  bool partialNetworkInfo{false};
  std::uint32_t pnRequest{0};
  std::array<std::uint8_t, 2> userData{};

  Bytes Encode() const noexcept;
  /// kBadPdu if the length is not 8 or a PN bitmap is present without the PNI bit.
  static ara::core::Result<NmPdu> Decode(std::uint8_t const *bytes, std::size_t size);
  static ara::core::Result<NmPdu> Decode(Bytes const &bytes) { return Decode(bytes.data(), bytes.size()); }

  friend bool operator==(NmPdu const &a, NmPdu const &b) noexcept {
    return a.sourceNodeId == b.sourceNodeId && a.repeatMessageRequest == b.repeatMessageRequest &&
           a.partialNetworkInfo == b.partialNetworkInfo && a.pnRequest == b.pnRequest &&
           a.userData == b.userData;
  }
};

} // namespace ara::nm

#endif // ARA_NM_NM_PDU_H_
