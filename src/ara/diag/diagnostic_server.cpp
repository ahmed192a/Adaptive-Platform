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
  case uds::kTesterPresent: return true;
  default: return false;
  }
}

/// UDS request SIDs: 0x01-0x3E and 0x80-0xBE (0x40-0x7F are response SIDs, >0xBE would overflow +0x40).
bool IsValidRequestSid(std::uint8_t sid) { return (sid >= 0x01 && sid <= 0x3E) || (sid >= 0x80 && sid <= 0xBE); }

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

void DiagnosticServer::ExpireSessionLocked(TimePoint now) {
  if (session_ != uds::Session::kDefault && now - lastActivity_ > cfg_.s3Timeout)
    session_ = uds::Session::kDefault;
}

uds::Session DiagnosticServer::CurrentSession() {
  std::lock_guard<std::mutex> lk(mutex_);
  ExpireSessionLocked(cfg_.clock());
  return session_;
}

std::optional<ByteVector> DiagnosticServer::Dispatch(ByteVector const &req) {
  if (req.empty()) return std::nullopt;
  try {
    std::uint8_t const sid = req[0];
    ServiceHandler custom;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      auto const now = cfg_.clock();
      ExpireSessionLocked(now);
      lastActivity_ = now; // any request keeps the session alive
      auto it = services_.find(sid);
      if (it != services_.end()) {
        if (!InSession(it->second.sessions))
          return uds::NegativeResponse(sid, Nrc::kServiceNotSupportedInActiveSession);
        custom = it->second.handler;
      }
    }
    if (custom) {
      auto r = custom(ByteVector(req.begin() + 1, req.end()));
      if (!r.HasValue()) return uds::NegativeResponse(sid, r.Error());
      return Positive(sid, std::move(r).Value());
    }
    switch (sid) {
    case uds::kDiagnosticSessionControl: return HandleSessionControl(req);
    case uds::kTesterPresent: return HandleTesterPresent(req);
    case uds::kEcuReset: return HandleEcuReset(req);
    case uds::kReadDataByIdentifier: return HandleRead(req);
    case uds::kWriteDataByIdentifier: return HandleWrite(req);
    default: return uds::NegativeResponse(sid, Nrc::kServiceNotSupported);
    }
  } catch (...) {
    return uds::NegativeResponse(req[0], Nrc::kGeneralReject);
  }
}

std::optional<ByteVector> DiagnosticServer::HandleSessionControl(ByteVector const &req) {
  constexpr auto sid = uds::kDiagnosticSessionControl;
  if (req.size() != 2) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const sub = req[1] & 0x7F;
  bool const suppress = (req[1] & uds::kSuppressPositiveResponseBit) != 0;
  if (sub < 1 || sub > 3) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  {
    std::lock_guard<std::mutex> lk(mutex_);
    session_ = static_cast<uds::Session>(sub);
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

std::optional<ByteVector> DiagnosticServer::HandleEcuReset(ByteVector const &req) {
  constexpr auto sid = uds::kEcuReset;
  if (req.size() != 2) return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  std::uint8_t const sub = req[1] & 0x7F;
  if (sub < 1 || sub > 3) return uds::NegativeResponse(sid, Nrc::kSubFunctionNotSupported);
  ResetHandler h;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    h = reset_;
  }
  if (!h) return uds::NegativeResponse(sid, Nrc::kServiceNotSupported);
  if (!h(sub)) return uds::NegativeResponse(sid, Nrc::kConditionsNotCorrect);
  if ((req[1] & uds::kSuppressPositiveResponseBit) != 0) return std::nullopt;
  return Positive(sid, {sub});
}

std::optional<ByteVector> DiagnosticServer::HandleRead(ByteVector const &req) {
  constexpr auto sid = uds::kReadDataByIdentifier;
  if (req.size() < 3 || (req.size() - 1) % 2 != 0)
    return uds::NegativeResponse(sid, Nrc::kIncorrectMessageLengthOrInvalidFormat);
  ByteVector out;
  for (std::size_t i = 1; i < req.size(); i += 2) {
    std::uint16_t const did = Be16(req, i);
    std::function<ara::core::Result<ByteVector, Nrc>()> read;
    {
      std::lock_guard<std::mutex> lk(mutex_);
      auto it = dids_.find(did);
      if (it == dids_.end() || !InSession(it->second.readSessions))
        return uds::NegativeResponse(sid, Nrc::kRequestOutOfRange);
      read = it->second.read;
    }
    auto r = read();
    if (!r.HasValue()) return uds::NegativeResponse(sid, r.Error());
    PutBe16(out, did);
    auto const &v = r.Value();
    out.insert(out.end(), v.begin(), v.end());
  }
  return Positive(sid, std::move(out));
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

} // namespace ara::diag
