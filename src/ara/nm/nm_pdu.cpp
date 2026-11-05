#include "ara/nm/nm_pdu.h"

#include "ara/nm/nm_error_domain.h"

namespace ara::nm {

namespace {
constexpr std::uint8_t kRmrBit = 0x01;
constexpr std::uint8_t kPniBit = 0x10;
} // namespace

NmPdu::Bytes NmPdu::Encode() const noexcept {
  Bytes b{};
  b[0] = sourceNodeId;
  b[1] = static_cast<std::uint8_t>((repeatMessageRequest ? kRmrBit : 0) |
                                   (partialNetworkInfo ? kPniBit : 0));
  for (std::size_t i = 0; i < 4; ++i) {
    b[2 + i] = static_cast<std::uint8_t>((pnRequest >> (8 * i)) & 0xFFU);
  }
  b[6] = userData[0];
  b[7] = userData[1];
  return b;
}

ara::core::Result<NmPdu> NmPdu::Decode(std::uint8_t const *bytes, std::size_t size) {
  if (bytes == nullptr || size != kSize) return ara::core::Result<NmPdu>::FromError(MakeErrorCode(NmErrc::kBadPdu));
  NmPdu p;
  p.sourceNodeId = bytes[0];
  p.repeatMessageRequest = (bytes[1] & kRmrBit) != 0;
  p.partialNetworkInfo = (bytes[1] & kPniBit) != 0;
  for (std::size_t i = 0; i < 4; ++i) p.pnRequest |= static_cast<std::uint32_t>(bytes[2 + i]) << (8 * i);
  if (!p.partialNetworkInfo && p.pnRequest != 0) {
    return ara::core::Result<NmPdu>::FromError(MakeErrorCode(NmErrc::kBadPdu));
  }
  p.userData = {bytes[6], bytes[7]};
  return ara::core::Result<NmPdu>::FromValue(p);
}

} // namespace ara::nm
