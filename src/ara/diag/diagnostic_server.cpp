#include "ara/diag/diagnostic_server.h"

#include <algorithm>

namespace ara::diag {
namespace {
using uds::Nrc;

bool IsBuiltin(std::uint8_t sid) {
  switch (sid) {
  case uds::kDiagnosticSessionControl:
  case uds::kEcuReset:
  case uds::kReadDataByIdentifier:
  case uds::kWriteDataByIdentifier:
  case uds::kTesterPresent:
  case uds::kSecurityAccess:
  case uds::kRoutineControl:
  case uds::kClearDiagnosticInformation:
  case uds::kReadDtcInformation:
  case uds::kControlDtcSetting:
  case uds::kReadMemoryByAddress:
  case uds::kCommunicationControl:
  case uds::kRequestDownload:
  case uds::kRequestUpload:
  case uds::kTransferData:
  case uds::kRequestTransferExit:
  case uds::kWriteMemoryByAddress: return true;
  default: return false;
  }
}

/// UDS request SIDs: 0x01-0x3E and 0x80-0xBE (0x40-0x7F are response SIDs, >0xBE would overflow +0x40).
bool IsValidRequestSid(std::uint8_t sid) { return (sid >= 0x01 && sid <= 0x3E) || (sid >= 0x80 && sid <= 0xBE); }

/// Polls @p call again while it answers NRC 0x78, forwarding each "7F sid 78" to @p sink.
template <class F>
auto RetryPending(std::uint8_t sid, DiagnosticServer::ResponseSink const &sink, unsigned maxRetries, F &&call) {
  auto r = call();
  for (unsigned n = 0; !r.HasValue() && r.Error() == Nrc::kResponsePending && sink && n < maxRetries; ++n) {
    sink(uds::NegativeResponse(sid, Nrc::kResponsePending));
    r = call();
  }
  return r;
}

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
} // namespace

DiagnosticServer::DiagnosticServer() : DiagnosticServer(Config{}) {}

DiagnosticServer::DiagnosticServer(Config cfg) : cfg_(std::move(cfg)) {
  if (!cfg_.clock) cfg_.clock = [] { return std::chrono::steady_clock::now(); };
  lastActivity_ = cfg_.clock();
}

ara::core::Result<void> DiagnosticServer::RegisterDataIdentifier(std::uint16_t did,
                                                                 DataIdentifier id) {
  if (!id.read) return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kInvalidArgument));
  std::lock_guard<std::mutex> lk(mutex_);
  if (!dids_.emplace(did, std::move(id)).second)
    return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kAlreadyOffered));
  return {};
}

ara::core::Result<void> DiagnosticServer::RegisterService(std::uint8_t sid,
                                                          std::uint8_t allowedSessions,
                                                          ServiceHandler handler) {
  if (!handler || allowedSessions == 0 || (allowedSessions & ~kAllSessions) != 0 || !IsValidRequestSid(sid))
    return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kInvalidArgument));
  if (IsBuiltin(sid))
    return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kAlreadyOffered));
  std::lock_guard<std::mutex> lk(mutex_);
  if (!services_.emplace(sid, Service{allowedSessions, std::move(handler)}).second)
    return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kAlreadyOffered));
  return {};
}

void DiagnosticServer::SetResetHandler(ResetHandler h) {
  std::lock_guard<std::mutex> lk(mutex_);
  reset_ = std::move(h);
}

void DiagnosticServer::SetSessionLocked(uds::Session s) {
  if (s != session_) { // ISO 14229-1: a session change relocks security access
    securityLevel_ = 0;
    pendingLevel_ = 0;
    pendingSeed_.clear();
    xfer_ = TransferState{};
  }
  session_ = s;
}

void DiagnosticServer::ExpireSessionLocked(TimePoint now) {
  if (session_ != uds::Session::kDefault && now - lastActivity_ > cfg_.s3Timeout)
    SetSessionLocked(uds::Session::kDefault);
}

ara::core::Result<void> DiagnosticServer::RegisterRoutine(std::uint16_t id, Routine r) {
  if ((!r.start && !r.stop && !r.requestResults) || r.sessions == 0 || (r.sessions & ~kAllSessions) != 0)
    return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kInvalidArgument));
  std::lock_guard<std::mutex> lk(mutex_);
  if (!routines_.emplace(id, std::move(r)).second)
    return ara::core::Result<void>::FromError(MakeErrorCode(DiagErrc::kAlreadyOffered));
  return {};
}

void DiagnosticServer::SetSecurityProvider(SecurityProvider p) {
  std::lock_guard<std::mutex> lk(mutex_);
  security_ = std::move(p);
}

std::uint8_t DiagnosticServer::CurrentSecurityLevel() {
  std::lock_guard<std::mutex> lk(mutex_);
  ExpireSessionLocked(cfg_.clock());
  return securityLevel_;
}

uds::Session DiagnosticServer::CurrentSession() {
  std::lock_guard<std::mutex> lk(mutex_);
  ExpireSessionLocked(cfg_.clock());
  return session_;
}

std::optional<ByteVector> DiagnosticServer::HandleSessionControl(ByteVector const &req) {
  constexpr auto sid = uds::kDiagnosticSessionControl;
  if (req.size() != 2) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const sub = req[1] & 0x7F;
  bool const suppress = (req[1] & uds::kSuppressPositiveResponseBit) != 0;
  if (sub < 1 || sub > 3) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  {
    std::lock_guard<std::mutex> lk(mutex_);
    SetSessionLocked(static_cast<uds::Session>(sub));
  }
  if (suppress) return std::nullopt;
  ByteVector payload{sub};
  PutBe16(payload, static_cast<std::uint16_t>(cfg_.p2Max.count()));
  PutBe16(payload, static_cast<std::uint16_t>(cfg_.p2StarMax.count() / 10)); // 10 ms units
  return Positive(sid, std::move(payload));
}

std::optional<ByteVector> DiagnosticServer::HandleTesterPresent(ByteVector const &req) {
  constexpr auto sid = uds::kTesterPresent;
  if (req.size() != 2) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  if ((req[1] & 0x7F) != 0) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  if ((req[1] & uds::kSuppressPositiveResponseBit) != 0) return std::nullopt;
  return Positive(sid, {0x00});
}

std::optional<ByteVector> DiagnosticServer::HandleWrite(ByteVector const &req) {
  constexpr auto sid = uds::kWriteDataByIdentifier;
  if (req.size() < 4) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint16_t const did = Be16(req, 1);
  ByteVector const data(req.begin() + 3, req.end());
  std::function<ara::core::Result<void, Nrc>(ByteVector const &)> write;
  std::size_t expected = 0;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = dids_.find(did);
    if (it == dids_.end() || !it->second.write || !InSession(it->second.writeSessions))
      return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange);
    if (securityLevel_ < it->second.writeSecurityLevel)
      return uds::NegativeResponse(sid, Nrc::kSecurityAccessDenied);
    write = it->second.write;
    expected = it->second.writeLength;
  }
  if (expected != 0 && data.size() != expected)
    return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  auto r = write(data);
  if (!r.HasValue()) return uds::NegativeResponse(sid, r.Error());
  ByteVector payload;
  PutBe16(payload, did);
  return Positive(sid, std::move(payload));
}

std::optional<ByteVector> DiagnosticServer::HandleSecurityAccess(ByteVector const &req) {
  constexpr auto sid = uds::kSecurityAccess;
  if (req.size() < 2) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const sub = req[1] & 0x7F;
  bool const suppress = (req[1] & uds::kSuppressPositiveResponseBit) != 0;
  if (sub == 0 || sub > 0x7E) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  SecurityProvider prov;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    prov = security_;
  }
  if (!prov.seed || !prov.validateKey) return uds::NegativeResponse(sid, Nrc::kServiceNotSupported);
  auto const now = cfg_.clock();
  if (sub % 2 == 1) { // requestSeed
    std::uint8_t const level = sub;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      if (failedAttempts_ >= prov.maxAttempts && now < lockedUntil_)
        return uds::NegativeResponse(sid, Nrc::kRequiredTimeDelayNotExpired);
      if (securityLevel_ == level) { // already unlocked: all-zero seed of the usual length
        ByteVector zeros(std::max<std::size_t>(1, pendingSeed_.size()), 0);
        return Positive(sid, [&] { ByteVector p{sub}; p.insert(p.end(), zeros.begin(), zeros.end()); return p; }());
      }
    }
    auto seed = prov.seed(level);
    if (!seed.HasValue()) return uds::NegativeResponse(sid, seed.Error());
    if (seed.Value().empty()) return uds::NegativeResponse(sid, Nrc::kGeneralReject);
    ByteVector payload{sub};
    payload.insert(payload.end(), seed.Value().begin(), seed.Value().end());
    {
      std::lock_guard<std::mutex> lk(mutex_);
      pendingLevel_ = level;
      pendingSeed_ = seed.Value();
    }
    return Positive(sid, std::move(payload)); // a seed response is never suppressed
  }
  // sendKey
  if (req.size() < 3) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const level = static_cast<std::uint8_t>(sub - 1);
  ByteVector seed;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (pendingLevel_ != level) return uds::NegativeResponse(sid, Nrc::kRequestSequenceError);
    if (failedAttempts_ >= prov.maxAttempts) {
      if (now < lockedUntil_) return uds::NegativeResponse(sid, Nrc::kRequiredTimeDelayNotExpired);
      failedAttempts_ = 0;
    }
    seed = pendingSeed_;
  }
  bool const ok = prov.validateKey(level, seed, ByteVector(req.begin() + 2, req.end()));
  std::lock_guard<std::mutex> lk(mutex_);
  pendingLevel_ = 0;
  if (!ok) {
    if (++failedAttempts_ >= prov.maxAttempts) {
      lockedUntil_ = now + prov.lockout;
      return uds::NegativeResponse(sid, Nrc::kExceededNumberOfAttempts);
    }
    return uds::NegativeResponse(sid, Nrc::kInvalidKey);
  }
  failedAttempts_ = 0;
  securityLevel_ = level;
  if (suppress) return std::nullopt;
  return Positive(sid, {sub});
}

std::optional<ByteVector> DiagnosticServer::HandleRoutineControl(ByteVector const &req) {
  constexpr auto sid = uds::kRoutineControl;
  if (req.size() < 4) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const sub = req[1] & 0x7F;
  bool const suppress = (req[1] & uds::kSuppressPositiveResponseBit) != 0;
  if (sub < 1 || sub > 3) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  std::uint16_t const id = Be16(req, 2);
  Routine::Handler h;
  ResponseSink sink;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    sink = pendingSink_;
    auto it = routines_.find(id);
    if (it == routines_.end()) return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange);
    if (!InSession(it->second.sessions)) return uds::NegativeResponse(sid, Nrc::kServiceNotSupportedInActiveSession);
    h = sub == 1 ? it->second.start : sub == 2 ? it->second.stop : it->second.requestResults;
    if (!h) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
    if (securityLevel_ < it->second.securityLevel) return uds::NegativeResponse(sid, Nrc::kSecurityAccessDenied);
  }
  ByteVector const option(req.begin() + 4, req.end());
  auto r = RetryPending(sid, sink, cfg_.maxPendingRetries, [&] { return h(option); });
  if (!r.HasValue()) return uds::NegativeResponse(sid, r.Error());
  if (suppress) return std::nullopt;
  ByteVector payload{sub};
  PutBe16(payload, id);
  payload.insert(payload.end(), r.Value().begin(), r.Value().end());
  return Positive(sid, std::move(payload));
}

std::optional<ByteVector> DiagnosticServer::HandleControlDtcSetting(ByteVector const &req) {
  constexpr auto sid = uds::kControlDtcSetting;
  if (req.size() < 2) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const sub = req[1] & 0x7F;
  bool const suppress = (req[1] & uds::kSuppressPositiveResponseBit) != 0;
  if (sub != 0x01 && sub != 0x02) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  {
    std::lock_guard<std::mutex> lk(mutex_);
    dtcSettingOn_ = (sub == 0x01);
  }
  if (suppress) return std::nullopt;
  return Positive(sid, {sub});
}

} // namespace ara::diag
