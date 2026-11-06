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
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ara/core/result.h"

namespace ara::nm {

/**
 * Configurable NM PDU layout (nmPduLength, nmNidPosition, nmCbvPosition, pncVectorOffset /
 * pncVectorLength, SWS_ANM_00033..00040, 00055). Every byte that is not the node id, the control
 * bit vector or the PN vector is user data. The default reproduces the fixed 8 byte layout.
 */
struct NmPduLayout {
  static constexpr int kAbsent = -1;
  std::size_t pduLength{8};       ///< 2..64 bytes
  int nidPosition{0};             ///< byte of the source node id: 0, 1 or kAbsent
  int cbvPosition{1};             ///< byte of the control bit vector: 0, 1 or kAbsent
  std::size_t pnVectorOffset{2};  ///< first byte of the PN request vector
  std::size_t pnVectorLength{4};  ///< 0..4 bytes (0: no partial-network information in the PDU)

  /// True if the layout is consistent (positions in range, no overlap, PNI needs a CBV).
  bool IsValid() const noexcept;
  /// Number of user-data bytes of this layout.
  std::size_t UserDataLength() const noexcept;
  friend bool operator==(NmPduLayout const &a, NmPduLayout const &b) noexcept {
    return a.pduLength == b.pduLength && a.nidPosition == b.nidPosition && a.cbvPosition == b.cbvPosition &&
           a.pnVectorOffset == b.pnVectorOffset && a.pnVectorLength == b.pnVectorLength;
  }
};

struct NmPdu {
  static constexpr std::size_t kSize = 8;
  using Bytes = std::array<std::uint8_t, kSize>;

  std::uint8_t sourceNodeId{0};
  bool repeatMessageRequest{false};
  bool partialNetworkInfo{false};
  std::uint32_t pnRequest{0};
  std::array<std::uint8_t, 2> userData{};
  /// User-data bytes beyond the first two (only used by layouts longer than 8 bytes).
  std::vector<std::uint8_t> moreUserData{};

  Bytes Encode() const noexcept;
  /// Encode with a configurable layout. kInvalidConfig if the layout is invalid.
  ara::core::Result<std::vector<std::uint8_t>> Encode(NmPduLayout const &layout) const;
  /// Decode with a configurable layout (size must equal layout.pduLength).
  static ara::core::Result<NmPdu> Decode(std::uint8_t const *bytes, std::size_t size, NmPduLayout const &layout);
  /// kBadPdu if the length is not 8 or a PN bitmap is present without the PNI bit.
  static ara::core::Result<NmPdu> Decode(std::uint8_t const *bytes, std::size_t size);
  static ara::core::Result<NmPdu> Decode(Bytes const &bytes) { return Decode(bytes.data(), bytes.size()); }

  friend bool operator==(NmPdu const &a, NmPdu const &b) noexcept {
    return a.sourceNodeId == b.sourceNodeId && a.repeatMessageRequest == b.repeatMessageRequest &&
           a.partialNetworkInfo == b.partialNetworkInfo && a.pnRequest == b.pnRequest &&
           a.userData == b.userData && a.moreUserData == b.moreUserData;
  }
};

} // namespace ara::nm

#endif // ARA_NM_NM_PDU_H_
