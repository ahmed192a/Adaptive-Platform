#include "ara/diag/diag_facade.h"

#include <algorithm>

namespace ara::diag {
namespace {
using uds::Nrc;
ara::core::Result<void> Err(DiagErrc e) { return ara::core::Result<void>::FromError(MakeErrorCode(e)); }
} // namespace

// ---- GenericUDSService
ara::core::Result<void> GenericUDSService::Offer() {
  if (offered_) return Err(DiagErrc::kAlreadyOffered);
  auto r = server_.RegisterService(sid_, sessions_, [this](ByteVector const &a) { return HandleMessage(a); });
  if (r.HasValue()) offered_ = true;
  return r;
}
ara::core::Result<void> GenericUDSService::StopOffer() {
  if (!offered_) return Err(DiagErrc::kNotOffered);
  offered_ = false;
  return server_.UnregisterService(sid_);
}

// ---- GenericRoutine
ara::core::Result<ByteVector, Nrc> GenericRoutine::Start(ByteVector const &) { return ara::core::Result<ByteVector, Nrc>::FromError(Nrc::kSubFunctionNotSupported); }
ara::core::Result<ByteVector, Nrc> GenericRoutine::Stop(ByteVector const &) { return ara::core::Result<ByteVector, Nrc>::FromError(Nrc::kSubFunctionNotSupported); }
ara::core::Result<ByteVector, Nrc> GenericRoutine::RequestResults(ByteVector const &) { return ara::core::Result<ByteVector, Nrc>::FromError(Nrc::kSubFunctionNotSupported); }

ara::core::Result<void> GenericRoutine::Offer() {
  if (offered_) return Err(DiagErrc::kAlreadyOffered);
  Routine r;
  r.start = [this](ByteVector const &o) { return Start(o); };
  r.stop = [this](ByteVector const &o) { return Stop(o); };
  r.requestResults = [this](ByteVector const &o) { return RequestResults(o); };
  r.sessions = sessions_;
  r.securityLevel = level_;
  auto res = server_.RegisterRoutine(id_, std::move(r));
  if (res.HasValue()) offered_ = true;
  return res;
}
ara::core::Result<void> GenericRoutine::StopOffer() {
  if (!offered_) return Err(DiagErrc::kNotOffered);
  offered_ = false;
  return server_.UnregisterRoutine(id_);
}

// ---- GenericDataIdentifier
ara::core::Result<void> GenericDataIdentifier::Offer() {
  if (offered_) return Err(DiagErrc::kAlreadyOffered);
  DataIdentifier d;
  d.read = [this] { return Read(); };
  if (IsWritable()) {
    d.write = [this](ByteVector const &v) { return Write(v); };
    d.writeLength = writeLength_;
    d.writeSecurityLevel = writeSecurityLevel_;
  }
  auto res = server_.RegisterDataIdentifier(did_, std::move(d));
  if (res.HasValue()) offered_ = true;
  return res;
}
ara::core::Result<void> GenericDataIdentifier::StopOffer() {
  if (!offered_) return Err(DiagErrc::kNotOffered);
  offered_ = false;
  return server_.UnregisterDataIdentifier(did_);
}

// ---- Monitor
ara::core::Result<void> Monitor::Offer() {
  if (offered_) return Err(DiagErrc::kAlreadyOffered);
  offered_ = true;
  return {};
}
ara::core::Result<void> Monitor::StopOffer() {
  if (!offered_) return Err(DiagErrc::kNotOffered);
  offered_ = false;
  counter_ = 0;
  frozen_ = false;
  return {};
}

ara::core::Result<void> Monitor::ReportMonitorAction(MonitorAction action) {
  if (!offered_) return Err(DiagErrc::kNotOffered);
  switch (action) {
  case MonitorAction::kFailed:
    counter_ = cfg_.failedThreshold;
    server_.ReportDtc(dtc_, true);
    break;
  case MonitorAction::kPassed:
    counter_ = -cfg_.passedThreshold;
    server_.ReportDtc(dtc_, false);
    break;
  case MonitorAction::kPrefailed:
    if (frozen_) break;
    counter_ = std::max(counter_, 0) + 1;
    if (counter_ >= cfg_.failedThreshold) {
      counter_ = cfg_.failedThreshold;
      server_.ReportDtc(dtc_, true);
    }
    break;
  case MonitorAction::kPrepassed:
    if (frozen_) break;
    counter_ = std::min(counter_, 0) - 1;
    if (-counter_ >= cfg_.passedThreshold) {
      counter_ = -cfg_.passedThreshold;
      server_.ReportDtc(dtc_, false);
    }
    break;
  case MonitorAction::kFreezeDebouncing: frozen_ = true; break;
  case MonitorAction::kResetDebouncing:
    frozen_ = false;
    counter_ = 0;
    break;
  default: return Err(DiagErrc::kInvalidArgument);
  }
  return {};
}

std::int8_t Monitor::GetFaultDetectionCounter() const noexcept {
  int const th = counter_ >= 0 ? std::max(cfg_.failedThreshold, 1) : std::max(cfg_.passedThreshold, 1);
  int const v = counter_ * (counter_ >= 0 ? 127 : 128) / th;
  return static_cast<std::int8_t>(std::clamp(v, -128, 127));
}

} // namespace ara::diag
