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


bool NmPduLayout::IsValid() const noexcept {
  if (pduLength < 2 || pduLength > 64) return false;
  if (nidPosition != kAbsent && (nidPosition < 0 || nidPosition > 1)) return false;
  if (cbvPosition != kAbsent && (cbvPosition < 0 || cbvPosition > 1)) return false;
  if (nidPosition != kAbsent && nidPosition == cbvPosition) return false;
  if (pnVectorLength > 4) return false;
  if (pnVectorLength > 0) {
    if (cbvPosition == kAbsent) return false; // the PNI bit lives in the CBV
    if (pnVectorOffset + pnVectorLength > pduLength) return false;
    auto const lo = pnVectorOffset, hi = pnVectorOffset + pnVectorLength;
    for (int fixed : {nidPosition, cbvPosition}) {
      if (fixed != kAbsent && static_cast<std::size_t>(fixed) >= lo && static_cast<std::size_t>(fixed) < hi)
        return false;
    }
  }
  return true;
}

std::size_t NmPduLayout::UserDataLength() const noexcept {
  std::size_t used = pnVectorLength + (nidPosition != kAbsent ? 1U : 0U) + (cbvPosition != kAbsent ? 1U : 0U);
  return pduLength > used ? pduLength - used : 0U;
}

namespace {
// Indices of the user-data bytes of a layout, in ascending order.
std::vector<std::size_t> UserDataIndices(NmPduLayout const &l) {
  std::vector<std::size_t> idx;
  for (std::size_t i = 0; i < l.pduLength; ++i) {
    if (l.nidPosition >= 0 && i == static_cast<std::size_t>(l.nidPosition)) continue;
    if (l.cbvPosition >= 0 && i == static_cast<std::size_t>(l.cbvPosition)) continue;
    if (l.pnVectorLength > 0 && i >= l.pnVectorOffset && i < l.pnVectorOffset + l.pnVectorLength) continue;
    idx.push_back(i);
  }
  return idx;
}
} // namespace

ara::core::Result<std::vector<std::uint8_t>> NmPdu::Encode(NmPduLayout const &l) const {
  using R = ara::core::Result<std::vector<std::uint8_t>>;
  if (!l.IsValid()) return R::FromError(MakeErrorCode(NmErrc::kInvalidConfig));
  std::vector<std::uint8_t> b(l.pduLength, 0U);
  if (l.nidPosition != NmPduLayout::kAbsent) b[static_cast<std::size_t>(l.nidPosition)] = sourceNodeId;
  if (l.cbvPosition != NmPduLayout::kAbsent) {
    bool const pni = partialNetworkInfo && l.pnVectorLength > 0;
    b[static_cast<std::size_t>(l.cbvPosition)] =
        static_cast<std::uint8_t>((repeatMessageRequest ? kRmrBit : 0) | (pni ? kPniBit : 0));
  }
  if (partialNetworkInfo) {
    for (std::size_t i = 0; i < l.pnVectorLength; ++i)
      b[l.pnVectorOffset + i] = static_cast<std::uint8_t>((pnRequest >> (8 * i)) & 0xFFU);
  }
  auto const idx = UserDataIndices(l);
  for (std::size_t k = 0; k < idx.size(); ++k) {
    std::uint8_t v = 0;
    if (k < 2) v = userData[k];
    else if (k - 2 < moreUserData.size()) v = moreUserData[k - 2];
    b[idx[k]] = v;
  }
  return R::FromValue(std::move(b));
}

ara::core::Result<NmPdu> NmPdu::Decode(std::uint8_t const *bytes, std::size_t size, NmPduLayout const &l) {
  using R = ara::core::Result<NmPdu>;
  if (!l.IsValid()) return R::FromError(MakeErrorCode(NmErrc::kInvalidConfig));
  if (bytes == nullptr || size != l.pduLength) return R::FromError(MakeErrorCode(NmErrc::kBadPdu));
  NmPdu p;
  if (l.nidPosition != NmPduLayout::kAbsent) p.sourceNodeId = bytes[l.nidPosition];
  if (l.cbvPosition != NmPduLayout::kAbsent) {
    std::uint8_t const cbv = bytes[l.cbvPosition];
    p.repeatMessageRequest = (cbv & kRmrBit) != 0;
    p.partialNetworkInfo = (cbv & kPniBit) != 0 && l.pnVectorLength > 0;
  }
  for (std::size_t i = 0; i < l.pnVectorLength; ++i)
    p.pnRequest |= static_cast<std::uint32_t>(bytes[l.pnVectorOffset + i]) << (8 * i);
  if (!p.partialNetworkInfo && p.pnRequest != 0) return R::FromError(MakeErrorCode(NmErrc::kBadPdu));
  auto const idx = UserDataIndices(l);
  for (std::size_t k = 0; k < idx.size(); ++k) {
    if (k < 2) p.userData[k] = bytes[idx[k]];
    else p.moreUserData.push_back(bytes[idx[k]]);
  }
  return R::FromValue(std::move(p));
}

} // namespace ara::nm
