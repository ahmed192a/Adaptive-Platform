/**
 * @file diagnostic_server_ext.cpp
 * @brief Dispatch core plus the services added in the gap-closing pass: functional addressing,
 *        multi-DID reads, ECUReset, DTC memory (+persistence hooks), CommunicationControl,
 *        memory access and the data-transfer services.
 */
#include <algorithm>

#include "ara/diag/diagnostic_server.h"

namespace ara::diag {
namespace {
using uds::Nrc;

ByteVector Positive(std::uint8_t sid, ByteVector payload = {}) {
  ByteVector r;
  r.reserve(1 + payload.size());
  r.push_back(static_cast<std::uint8_t>(sid + uds::kPositiveResponseOffset));
  r.insert(r.end(), payload.begin(), payload.end());
  return r;
}

std::uint16_t Be16(ByteVector const &v, std::size_t i) {
  return static_cast<std::uint16_t>((v[i] << 8) | v[i + 1]);
}
void PutBe16(ByteVector &v, std::uint16_t x) {
  v.push_back(static_cast<std::uint8_t>(x >> 8));
  v.push_back(static_cast<std::uint8_t>(x & 0xFF));
}
void PutDtc(ByteVector &v, std::uint32_t dtc) {
  v.push_back(static_cast<std::uint8_t>(dtc >> 16));
  v.push_back(static_cast<std::uint8_t>(dtc >> 8));
  v.push_back(static_cast<std::uint8_t>(dtc));
}

template <class F>
auto RetryPending(std::uint8_t sid, DiagnosticServer::ResponseSink const &sink, unsigned maxRetries, F &&call) {
  auto r = call();
  for (unsigned n = 0; !r.HasValue() && r.Error() == Nrc::kResponsePending && sink && n < maxRetries; ++n) {
    sink(uds::NegativeResponse(sid, Nrc::kResponsePending));
    r = call();
  }
  return r;
}

/// Reads a big-endian unsigned integer of @p n bytes (1..8) at @p pos.
std::uint64_t ReadUint(ByteVector const &v, std::size_t pos, std::size_t n) {
  std::uint64_t x = 0;
  for (std::size_t i = 0; i < n; ++i) x = (x << 8) | v[pos + i];
  return x;
}

/// Parses an addressAndLengthFormatIdentifier block: [alfid, address(m), length(n)] starting at @p pos.
/// Advances @p pos past the block.
bool ParseAddressAndLength(ByteVector const &req, std::size_t &pos, std::uint64_t &address, std::uint64_t &length) {
  if (pos >= req.size()) return false;
  std::size_t const nAddr = req[pos] & 0x0FU;
  std::size_t const nLen = static_cast<std::size_t>(req[pos] >> 4U);
  if (nAddr < 1 || nAddr > 8 || nLen < 1 || nLen > 8 || pos + 1 + nAddr + nLen > req.size()) return false;
  address = ReadUint(req, pos + 1, nAddr);
  length = ReadUint(req, pos + 1 + nAddr, nLen);
  pos += 1 + nAddr + nLen;
  return true;
}

bool NoResponseOnFunctional(std::uint8_t nrc) {
  return nrc == 0x11 || nrc == 0x12 || nrc == 0x31 || nrc == 0x7E || nrc == 0x7F;
}
} // namespace

// --------------------------------------------------------------------------------------------- registries

ara::core::Result<void> DiagnosticServer::UnregisterDataIdentifier(std::uint16_t did) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (dids_.erase(did) == 0) return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kNotOffered));
  return {};
}

ara::core::Result<void> DiagnosticServer::UnregisterService(std::uint8_t sid) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (services_.erase(sid) == 0) return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kNotOffered));
  return {};
}

ara::core::Result<void> DiagnosticServer::UnregisterRoutine(std::uint16_t id) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (routines_.erase(id) == 0) return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kNotOffered));
  return {};
}

ara::core::Result<void> DiagnosticServer::RestrictBuiltinService(std::uint8_t sid, std::uint8_t allowedSessions) {
  bool const builtin = sid == uds::kDiagnosticSessionControl || sid == uds::kEcuReset || sid == uds::kClearDiagnosticInformation ||
                       sid == uds::kReadDtcInformation || sid == uds::kReadDataByIdentifier || sid == uds::kReadMemoryByAddress ||
                       sid == uds::kSecurityAccess || sid == uds::kCommunicationControl || sid == uds::kWriteDataByIdentifier ||
                       sid == uds::kRoutineControl || sid == uds::kRequestDownload || sid == uds::kRequestUpload ||
                       sid == uds::kTransferData || sid == uds::kRequestTransferExit || sid == uds::kWriteMemoryByAddress ||
                       sid == uds::kTesterPresent || sid == uds::kControlDtcSetting;
  if (!builtin || allowedSessions == 0 || (allowedSessions & ~kAllSessions) != 0)
    return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kInvalidArgument));
  std::lock_guard<std::mutex> lk(mutex_);
  builtinSessions_[sid] = allowedSessions;
  return {};
}

void DiagnosticServer::SetCommControlHandler(CommControlHandler h) {
  std::lock_guard<std::mutex> lk(mutex_);
  commControl_ = std::move(h);
}

CommControlState DiagnosticServer::CommControl() {
  std::lock_guard<std::mutex> lk(mutex_);
  return commState_;
}

void DiagnosticServer::SetResponsePendingSink(ResponseSink s) {
  std::lock_guard<std::mutex> lk(mutex_);
  pendingSink_ = std::move(s);
}

ara::core::Result<void> DiagnosticServer::RegisterMemoryRegion(MemoryRegion r) {
  if (!r.read || r.size == 0 || r.start + r.size < r.start || r.sessions == 0 || (r.sessions & ~kAllSessions) != 0)
    return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kInvalidArgument));
  std::lock_guard<std::mutex> lk(mutex_);
  for (auto const &o : regions_)
    if (r.start < o.start + o.size && o.start < r.start + r.size)
      return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kConflict));
  regions_.push_back(std::move(r));
  return {};
}

void DiagnosticServer::SetTransferHandler(TransferHandler h) {
  std::lock_guard<std::mutex> lk(mutex_);
  transfer_ = std::move(h);
  xfer_ = TransferState{};
}

bool DiagnosticServer::TransferActive() {
  std::lock_guard<std::mutex> lk(mutex_);
  ExpireSessionLocked(cfg_.clock());
  return xfer_.active;
}

// --------------------------------------------------------------------------------------------- DTC memory

void DiagnosticServer::SaveDtcsLocked() {
  if (dtcStorage_.save) dtcStorage_.save(dtcs_);
}

void DiagnosticServer::SetDtcStorage(DtcStorage storage) {
  std::lock_guard<std::mutex> lk(mutex_);
  dtcStorage_ = std::move(storage);
  if (dtcStorage_.load) dtcs_ = dtcStorage_.load();
}

void DiagnosticServer::ReportDtc(std::uint32_t dtc, bool testFailed) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!dtcSettingOn_) return;
  auto &rec = dtcs_[dtc & 0xFFFFFF];
  bool const wasFailed = (rec.status & dtc::kTestFailed) != 0;
  if (testFailed) {
    if (!wasFailed && rec.occurrences < 255) ++rec.occurrences;
    rec.status = static_cast<std::uint8_t>(rec.status | dtc::kTestFailed | dtc::kTestFailedThisOperationCycle |
                                           dtc::kConfirmed | dtc::kTestFailedSinceLastClear);
  } else {
    rec.status = static_cast<std::uint8_t>(rec.status & ~dtc::kTestFailed);
  }
  SaveDtcsLocked();
}

std::optional<std::uint8_t> DiagnosticServer::DtcStatus(std::uint32_t dtc) {
  std::lock_guard<std::mutex> lk(mutex_);
  auto it = dtcs_.find(dtc & 0xFFFFFF);
  if (it == dtcs_.end()) return std::nullopt;
  return it->second.status;
}

std::optional<DtcRecord> DiagnosticServer::DtcInfo(std::uint32_t dtc) {
  std::lock_guard<std::mutex> lk(mutex_);
  auto it = dtcs_.find(dtc & 0xFFFFFF);
  if (it == dtcs_.end()) return std::nullopt;
  return it->second;
}

void DiagnosticServer::SetDtcSnapshot(std::uint32_t dtc, std::uint8_t recordNumber,
                                      std::vector<std::pair<std::uint16_t, ByteVector>> const &dids) {
  ByteVector enc{static_cast<std::uint8_t>(dids.size())};
  for (auto const &[did, data] : dids) {
    PutBe16(enc, did);
    enc.insert(enc.end(), data.begin(), data.end());
  }
  std::lock_guard<std::mutex> lk(mutex_);
  dtcs_[dtc & 0xFFFFFF].snapshots[recordNumber] = std::move(enc);
  SaveDtcsLocked();
}

void DiagnosticServer::SetDtcExtendedData(std::uint32_t dtc, std::uint8_t recordNumber, ByteVector data) {
  std::lock_guard<std::mutex> lk(mutex_);
  dtcs_[dtc & 0xFFFFFF].extendedData[recordNumber] = std::move(data);
  SaveDtcsLocked();
}

void DiagnosticServer::RegisterDtcGroup(std::uint32_t group, std::set<std::uint32_t> dtcs) {
  std::lock_guard<std::mutex> lk(mutex_);
  dtcGroups_[group & 0xFFFFFF] = std::move(dtcs);
}

std::optional<ByteVector> DiagnosticServer::HandleClearDtc(ByteVector const &req) {
  constexpr auto sid = uds::kClearDiagnosticInformation;
  if (req.size() != 4) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint32_t const group = (static_cast<std::uint32_t>(req[1]) << 16) | (static_cast<std::uint32_t>(req[2]) << 8) | req[3];
  std::lock_guard<std::mutex> lk(mutex_);
  if (group == 0xFFFFFF) {
    dtcs_.clear();
  } else if (auto g = dtcGroups_.find(group); g != dtcGroups_.end()) {
    for (auto dtc : g->second) dtcs_.erase(dtc);
  } else if (dtcs_.erase(group) == 0) {
    return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange); // unknown DTC / group
  }
  SaveDtcsLocked();
  return Positive(sid);
}

std::optional<ByteVector> DiagnosticServer::HandleReadDtc(ByteVector const &req) {
  constexpr auto sid = uds::kReadDtcInformation;
  if (req.size() < 2) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const sub = req[1] & 0x7F;
  bool const suppress = (req[1] & uds::kSuppressPositiveResponseBit) != 0;
  switch (sub) {
  case 0x01: case 0x02: case 0x03: case 0x04: case 0x06: case 0x0A: break;
  default: return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  }
  std::size_t const expected = (sub == 0x0A || sub == 0x03) ? 2U : (sub == 0x01 || sub == 0x02) ? 3U : 6U;
  if (req.size() != expected) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);

  ByteVector payload{sub};
  std::lock_guard<std::mutex> lk(mutex_);
  if (sub == 0x03) { // reportDTCSnapshotIdentification: no availability mask
    for (auto const &[code, rec] : dtcs_)
      for (auto const &[num, enc] : rec.snapshots) {
        (void)enc;
        PutDtc(payload, code);
        payload.push_back(num);
      }
  } else if (sub == 0x04 || sub == 0x06) {
    std::uint32_t const code = (static_cast<std::uint32_t>(req[2]) << 16) | (static_cast<std::uint32_t>(req[3]) << 8) | req[4];
    std::uint8_t const recNum = req[5];
    auto it = dtcs_.find(code);
    if (it == dtcs_.end()) return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange);
    payload.push_back(dtc::kStatusAvailabilityMask);
    PutDtc(payload, code);
    payload.push_back(static_cast<std::uint8_t>(it->second.status & dtc::kStatusAvailabilityMask));
    if (sub == 0x04) {
      for (auto const &[num, enc] : it->second.snapshots) {
        if (recNum != 0xFF && recNum != num) continue;
        payload.push_back(num);
        payload.insert(payload.end(), enc.begin(), enc.end());
      }
    } else {
      auto ext = it->second.extendedData;
      ext[0x01] = ByteVector{it->second.occurrences}; // occurrence counter is synthesised
      for (auto const &[num, data] : ext) {
        if (recNum < 0xFE && recNum != num) continue;
        payload.push_back(num);
        payload.insert(payload.end(), data.begin(), data.end());
      }
    }
  } else {
    std::uint8_t const mask = (sub == 0x0A) ? 0xFF : req[2];
    payload.push_back(dtc::kStatusAvailabilityMask);
    if (sub == 0x01) {
      std::uint16_t n = 0;
      for (auto const &[code, rec] : dtcs_) {
        (void)code;
        if ((rec.status & mask & dtc::kStatusAvailabilityMask) != 0) ++n;
      }
      payload.push_back(0x01); // DTCFormatIdentifier: ISO 14229-1
      PutBe16(payload, n);
    } else {
      for (auto const &[code, rec] : dtcs_) {
        if (sub == 0x02 && (rec.status & mask & dtc::kStatusAvailabilityMask) == 0) continue;
        PutDtc(payload, code);
        payload.push_back(static_cast<std::uint8_t>(rec.status & dtc::kStatusAvailabilityMask));
      }
    }
  }
  if (suppress) return std::nullopt;
  return Positive(sid, std::move(payload));
}

// --------------------------------------------------------------------------------------------- dispatch

std::optional<ByteVector> DiagnosticServer::Dispatch(ByteVector const &req, Addressing addressing) {
  auto r = DispatchPhysical(req);
  if (addressing == Addressing::kFunctional && r && r->size() == 3 && (*r)[0] == uds::kNegativeResponseSid &&
      NoResponseOnFunctional((*r)[2]))
    return std::nullopt; // ISO 14229-1: functional requests are not answered with "not supported" NRCs
  return r;
}

std::optional<ByteVector> DiagnosticServer::DispatchPhysical(ByteVector const &req) {
  if (req.empty()) return std::nullopt;
  try {
    std::uint8_t const sid = req[0];
    ServiceHandler custom;
    ResponseSink sink;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      auto const now = cfg_.clock();
      ExpireSessionLocked(now);
      lastActivity_ = now; // any request keeps the session alive
      sink = pendingSink_;
      auto it = services_.find(sid);
      if (it != services_.end()) {
        if (!InSession(it->second.sessions))
          return uds::NegativeResponse(sid, Nrc::kServiceNotSupportedInActiveSession);
        custom = it->second.handler;
      } else if (auto b = builtinSessions_.find(sid); b != builtinSessions_.end() && !InSession(b->second)) {
        return uds::NegativeResponse(sid, Nrc::kServiceNotSupportedInActiveSession);
      }
    }
    if (custom) {
      ByteVector const args(req.begin() + 1, req.end());
      auto r = RetryPending(sid, sink, cfg_.maxPendingRetries, [&] { return custom(args); });
      if (!r.HasValue()) return uds::NegativeResponse(sid, r.Error());
      return Positive(sid, std::move(r).Value());
    }
    switch (sid) {
    case uds::kDiagnosticSessionControl: return HandleSessionControl(req);
    case uds::kTesterPresent: return HandleTesterPresent(req);
    case uds::kEcuReset: return HandleEcuReset(req);
    case uds::kReadDataByIdentifier: return HandleRead(req);
    case uds::kWriteDataByIdentifier: return HandleWrite(req);
    case uds::kSecurityAccess: return HandleSecurityAccess(req);
    case uds::kRoutineControl: return HandleRoutineControl(req);
    case uds::kClearDiagnosticInformation: return HandleClearDtc(req);
    case uds::kReadDtcInformation: return HandleReadDtc(req);
    case uds::kControlDtcSetting: return HandleControlDtcSetting(req);
    case uds::kCommunicationControl: return HandleCommunicationControl(req);
    case uds::kReadMemoryByAddress: return HandleReadMemory(req);
    case uds::kWriteMemoryByAddress: return HandleWriteMemory(req);
    case uds::kRequestDownload:
    case uds::kRequestUpload: return HandleRequestTransfer(req);
    case uds::kTransferData: return HandleTransferData(req);
    case uds::kRequestTransferExit: return HandleTransferExit(req);
    default: return uds::NegativeResponse(sid, Nrc::kServiceNotSupported);
    }
  } catch (...) {
    return uds::NegativeResponse(req[0], Nrc::kGeneralReject);
  }
}

// --------------------------------------------------------------------------------------------- 0x11 / 0x22

std::optional<ByteVector> DiagnosticServer::HandleEcuReset(ByteVector const &req) {
  constexpr auto sid = uds::kEcuReset;
  if (req.size() != 2) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const sub = req[1] & 0x7F;
  if (sub < 1 || sub > 5) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  ResetHandler h;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    h = reset_;
  }
  if (!h) return uds::NegativeResponse(sid, Nrc::kServiceNotSupported);
  if (!h(sub)) return uds::NegativeResponse(sid, Nrc::kConditionsNotCorrect);
  {
    std::lock_guard<std::mutex> lk(mutex_); // after a reset the server is back in the default session, locked
    SetSessionLocked(uds::Session::kDefault);
    securityLevel_ = 0;
    pendingLevel_ = 0;
    pendingSeed_.clear();
    xfer_ = TransferState{};
  }
  if ((req[1] & uds::kSuppressPositiveResponseBit) != 0) return std::nullopt;
  ByteVector payload{sub};
  if (sub == 0x04) payload.push_back(cfg_.powerDownTime); // powerDownTime follows enableRapidPowerShutDown
  return Positive(sid, std::move(payload));
}

std::optional<ByteVector> DiagnosticServer::HandleRead(ByteVector const &req) {
  constexpr auto sid = uds::kReadDataByIdentifier;
  if (req.size() < 3 || (req.size() - 1) % 2 != 0)
    return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  ByteVector out;
  bool any = false;
  for (std::size_t i = 1; i < req.size(); i += 2) {
    std::uint16_t const did = Be16(req, i);
    std::function<ara::core::Result<ByteVector, Nrc>()> read;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      auto it = dids_.find(did);
      if (it == dids_.end() || !InSession(it->second.readSessions)) continue; // ISO: answer the DIDs that exist
      read = it->second.read;
    }
    auto r = read();
    if (!r.HasValue()) return uds::NegativeResponse(sid, r.Error());
    PutBe16(out, did);
    auto const &v = r.Value();
    out.insert(out.end(), v.begin(), v.end());
    any = true;
  }
  if (!any) return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange); // none of the DIDs is available
  return Positive(sid, std::move(out));
}

// --------------------------------------------------------------------------------------------- 0x28

std::optional<ByteVector> DiagnosticServer::HandleCommunicationControl(ByteVector const &req) {
  constexpr auto sid = uds::kCommunicationControl;
  if (req.size() < 3) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const sub = req[1] & 0x7F;
  bool const suppress = (req[1] & uds::kSuppressPositiveResponseBit) != 0;
  if (sub > 0x05) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  // sub-functions 0x04/0x05 carry a 2-byte nodeIdentificationNumber after the communicationType
  if (req.size() != ((sub >= 0x04) ? 5U : 3U))
    return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const type = req[2];
  if ((type & 0xFC) != 0 || (type & 0x03) == 0) return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange);
  CommControlHandler h;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    h = commControl_;
  }
  if (!h) return uds::NegativeResponse(sid, Nrc::kServiceNotSupported);
  if (!h(sub, type)) return uds::NegativeResponse(sid, Nrc::kConditionsNotCorrect);
  {
    std::lock_guard<std::mutex> lk(mutex_);
    commState_.communicationType = type;
    switch (sub) {
    case 0x00: case 0x05: commState_.rxEnabled = true; commState_.txEnabled = true; break;
    case 0x01: case 0x04: commState_.rxEnabled = true; commState_.txEnabled = false; break;
    case 0x02: commState_.rxEnabled = false; commState_.txEnabled = true; break;
    default: commState_.rxEnabled = false; commState_.txEnabled = false; break;
    }
  }
  if (suppress) return std::nullopt;
  return Positive(sid, {sub});
}

// --------------------------------------------------------------------------------------------- 0x23 / 0x3D

namespace {
/// Finds the region that fully contains [address, address+length).
MemoryRegion const *FindRegion(std::vector<MemoryRegion> const &regions, std::uint64_t address, std::uint64_t length) {
  for (auto const &r : regions)
    if (length != 0 && address >= r.start && length <= r.size && address - r.start <= r.size - length) return &r;
  return nullptr;
}
} // namespace

std::optional<ByteVector> DiagnosticServer::HandleReadMemory(ByteVector const &req) {
  constexpr auto sid = uds::kReadMemoryByAddress;
  std::size_t pos = 1;
  std::uint64_t address = 0, length = 0;
  if (!ParseAddressAndLength(req, pos, address, length) || pos != req.size())
    return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  decltype(MemoryRegion::read) read;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto const *r = FindRegion(regions_, address, length);
    if (r == nullptr) return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange);
    if (!InSession(r->sessions)) return uds::NegativeResponse(sid, Nrc::kServiceNotSupportedInActiveSession);
    if (securityLevel_ < r->securityLevel) return uds::NegativeResponse(sid, Nrc::kSecurityAccessDenied);
    read = r->read;
  }
  auto data = read(address, length);
  if (!data.HasValue()) return uds::NegativeResponse(sid, data.Error());
  if (data.Value().size() != length) return uds::NegativeResponse(sid, Nrc::kGeneralReject);
  return Positive(sid, std::move(data).Value());
}

std::optional<ByteVector> DiagnosticServer::HandleWriteMemory(ByteVector const &req) {
  constexpr auto sid = uds::kWriteMemoryByAddress;
  std::size_t pos = 1;
  std::uint64_t address = 0, length = 0;
  if (!ParseAddressAndLength(req, pos, address, length) || req.size() - pos != length)
    return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  ByteVector const data(req.begin() + static_cast<std::ptrdiff_t>(pos), req.end());
  decltype(MemoryRegion::write) write;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto const *r = FindRegion(regions_, address, length);
    if (r == nullptr || !r->write) return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange);
    if (!InSession(r->sessions)) return uds::NegativeResponse(sid, Nrc::kServiceNotSupportedInActiveSession);
    if (securityLevel_ < r->securityLevel) return uds::NegativeResponse(sid, Nrc::kSecurityAccessDenied);
    write = r->write;
  }
  auto w = write(address, data);
  if (!w.HasValue()) return uds::NegativeResponse(sid, w.Error());
  // positive response echoes addressAndLengthFormatIdentifier, memoryAddress and memorySize
  return Positive(sid, ByteVector(req.begin() + 1, req.begin() + static_cast<std::ptrdiff_t>(pos)));
}

// --------------------------------------------------------------------------------------------- 0x34..0x37

std::optional<ByteVector> DiagnosticServer::HandleRequestTransfer(ByteVector const &req) {
  std::uint8_t const sid = req[0];
  std::size_t pos = 2;
  std::uint64_t address = 0, size = 0;
  if (req.size() < 3 || !ParseAddressAndLength(req, pos, address, size) || pos != req.size())
    return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  TransferDirection const dir = sid == uds::kRequestDownload ? TransferDirection::kDownload : TransferDirection::kUpload;
  TransferHandler h;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!transfer_.request) return uds::NegativeResponse(sid, Nrc::kServiceNotSupported);
    if (!InSession(transfer_.sessions)) return uds::NegativeResponse(sid, Nrc::kServiceNotSupportedInActiveSession);
    if (securityLevel_ < transfer_.securityLevel) return uds::NegativeResponse(sid, Nrc::kSecurityAccessDenied);
    if (xfer_.active) return uds::NegativeResponse(sid, Nrc::kConditionsNotCorrect);
    h = transfer_;
  }
  if (size == 0) return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange);
  auto max = h.request(dir, address, size, req[1]);
  if (!max.HasValue()) return uds::NegativeResponse(sid, max.Error());
  if (max.Value() < 3) return uds::NegativeResponse(sid, Nrc::kGeneralReject);
  std::uint32_t const block = std::min<std::uint32_t>(max.Value(), 0xFFFF);
  {
    std::lock_guard<std::mutex> lk(mutex_);
    xfer_ = TransferState{true, dir, size, 0, block, 1};
  }
  ByteVector payload{0x20}; // lengthFormatIdentifier: maxNumberOfBlockLength is 2 bytes
  PutBe16(payload, static_cast<std::uint16_t>(block));
  return Positive(sid, std::move(payload));
}

std::optional<ByteVector> DiagnosticServer::HandleTransferData(ByteVector const &req) {
  constexpr auto sid = uds::kTransferData;
  if (req.size() < 2) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const seq = req[1];
  ByteVector const data(req.begin() + 2, req.end());
  decltype(TransferHandler::block) block;
  TransferState st;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!xfer_.active) return uds::NegativeResponse(sid, Nrc::kRequestSequenceError);
    if (seq != xfer_.nextCounter) return uds::NegativeResponse(sid, Nrc::kWrongBlockSequenceCounter);
    st = xfer_;
    block = transfer_.block;
  }
  if (!block) return uds::NegativeResponse(sid, Nrc::kServiceNotSupported);
  if (st.dir == TransferDirection::kDownload) {
    if (data.empty() || req.size() > st.maxBlock || st.done + data.size() > st.size)
      return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange);
  } else if (!data.empty()) {
    return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  }
  auto r = block(seq, data);
  if (!r.HasValue()) return uds::NegativeResponse(sid, r.Error());
  std::uint64_t moved = data.size();
  if (st.dir == TransferDirection::kUpload) {
    moved = r.Value().size();
    if (moved == 0 || moved + 2 > st.maxBlock || st.done + moved > st.size)
      return uds::NegativeResponse(sid, Nrc::kGeneralReject);
  }
  {
    std::lock_guard<std::mutex> lk(mutex_);
    xfer_.done += moved;
    xfer_.nextCounter = static_cast<std::uint8_t>(xfer_.nextCounter + 1); // wraps 0xFF -> 0x00
  }
  ByteVector payload{seq};
  payload.insert(payload.end(), r.Value().begin(), r.Value().end());
  return Positive(sid, std::move(payload));
}

std::optional<ByteVector> DiagnosticServer::HandleTransferExit(ByteVector const &req) {
  constexpr auto sid = uds::kRequestTransferExit;
  decltype(TransferHandler::exit) exit;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!xfer_.active) return uds::NegativeResponse(sid, Nrc::kRequestSequenceError);
    if (xfer_.dir == TransferDirection::kDownload && xfer_.done != xfer_.size)
      return uds::NegativeResponse(sid, Nrc::kRequestSequenceError); // not all announced bytes were sent
    exit = transfer_.exit;
  }
  ByteVector record;
  if (exit) {
    auto r = exit(ByteVector(req.begin() + 1, req.end()));
    if (!r.HasValue()) return uds::NegativeResponse(sid, r.Error());
    record = std::move(r).Value();
  }
  {
    std::lock_guard<std::mutex> lk(mutex_);
    xfer_ = TransferState{};
  }
  return Positive(sid, std::move(record));
}

} // namespace ara::diag
