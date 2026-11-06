#include "ara/tsync/synchronized_time_base.h"

#include <cstring>
#include <map>

namespace ara::tsync {

using ara::core::Result;

namespace {
std::mutex gMu;
std::map<std::string, std::shared_ptr<TimeBaseResource>> &Reg() {
  static std::map<std::string, std::shared_ptr<TimeBaseResource>> m;
  return m;
}
Result<UserData> ToUserData(std::uint8_t const *p, std::size_t n) {
  if (n > sizeof(UserData::bytes) || (n != 0 && p == nullptr)) {
    return Result<UserData>::FromError(MakeErrorCode(TsyncErrc::kInvalidArgument));
  }
  UserData u;
  u.length = static_cast<std::uint8_t>(n);
  if (n != 0) std::memcpy(u.bytes, p, n);
  return Result<UserData>::FromValue(u);
}
} // namespace

void TimeBaseRegistry::Bind(std::string const &specifier, std::shared_ptr<TimeBaseResource> resource) {
  std::lock_guard<std::mutex> l(gMu);
  Reg()[specifier] = std::move(resource);
}
void TimeBaseRegistry::Unbind(std::string const &specifier) {
  std::lock_guard<std::mutex> l(gMu);
  Reg().erase(specifier);
}
std::shared_ptr<TimeBaseResource> TimeBaseRegistry::Lookup(std::string const &specifier) {
  std::lock_guard<std::mutex> l(gMu);
  auto it = Reg().find(specifier);
  return it == Reg().end() ? nullptr : it->second;
}

// ------------------------------------------------------------------------------- consumer

struct SynchronizedTimeBaseConsumer::Impl {
  std::string specifier;
  std::mutex mu;
  StatusChangeNotifier onStatus;
  SynchronizationStateChangeNotifier onSync;
  TimeLeapNotifier onLeap;
  std::weak_ptr<TimeBaseResource> observed;
  std::size_t observerId{0};
  StatusFlags lastFlags{};
  bool lastEver{false};
  PrecisionMeasurementNotifier onPrecision;
  TimeValidationNotifier onValidation;
  std::weak_ptr<TimeBaseResource> precRes, valRes;
  std::size_t precObs{0}, valObs{0};
};

SynchronizedTimeBaseConsumer::SynchronizedTimeBaseConsumer(ara::core::InstanceSpecifier const &s) noexcept
    : impl_(std::make_shared<Impl>()) {
  impl_->specifier = std::string(s.ToString());
}
SynchronizedTimeBaseConsumer::SynchronizedTimeBaseConsumer(SynchronizedTimeBaseConsumer &&) noexcept = default;
SynchronizedTimeBaseConsumer &SynchronizedTimeBaseConsumer::operator=(SynchronizedTimeBaseConsumer &&) & noexcept = default;

SynchronizedTimeBaseConsumer::~SynchronizedTimeBaseConsumer() noexcept {
  if (!impl_) return;
  if (auto r = impl_->observed.lock()) {
    if (impl_->observerId != 0) r->RemoveObserver(impl_->observerId);
  }
  if (auto r = impl_->precRes.lock()) {
    if (impl_->precObs != 0) r->RemovePrecisionObserver(impl_->precObs);
  }
  if (auto r = impl_->valRes.lock()) {
    if (impl_->valObs != 0) r->RemoveValidationObserver(impl_->valObs);
  }
}

SynchronizedTimeBaseStatus SynchronizedTimeBaseConsumer::GetTimeWithStatus() const noexcept {
  auto r = TimeBaseRegistry::Lookup(impl_->specifier);
  if (!r) return SynchronizedTimeBaseStatus();
  auto snap = r->GetCurrentTime(); // atomic sample of time, status, leap and user data
  if (!snap.HasValue()) return SynchronizedTimeBaseStatus();
  return SynchronizedTimeBaseStatus(snap.Value(), true);
}

double SynchronizedTimeBaseConsumer::GetRateDeviation() const noexcept {
  auto r = TimeBaseRegistry::Lookup(impl_->specifier);
  if (!r) return 0.0;
  auto d = r->GetRateDeviation();
  return d.HasValue() ? d.Value() : 0.0;
}

namespace {
// Registers the single shared observer that fans out to the three notifier kinds.
template <class ImplT> void EnsureObserver(std::shared_ptr<ImplT> const &impl, std::shared_ptr<TimeBaseResource> const &r) {
  if (impl->observerId != 0) return;
  std::weak_ptr<ImplT> weak = impl;
  impl->observed = r;
  impl->observerId = r->AddObserver(
      [weak](TimeBaseSnapshot const &snap, ChangeReason reason) {
        auto self = weak.lock();
        if (!self) return;
        std::function<void(SynchronizedTimeBaseStatus const &)> st;
        std::function<void(SynchronizationStatus)> sy;
        std::function<void(LeapJump)> lp;
        bool statusChanged = false, syncChanged = false;
        {
          std::lock_guard<std::mutex> g(self->mu);
          st = self->onStatus;
          sy = self->onSync;
          lp = self->onLeap;
          statusChanged = snap.status != self->lastFlags;
          syncChanged = ToSynchronizationStatus(snap.status) != ToSynchronizationStatus(self->lastFlags) ||
                        !self->lastEver;
          self->lastFlags = snap.status;
          self->lastEver = true;
        }
        SynchronizedTimeBaseStatus status(snap, true);
        if (st && statusChanged) st(status);
        if (sy && syncChanged) sy(status.GetSynchronizationStatus());
        if (lp && reason == ChangeReason::kTimeStep) lp(snap.leapJump);
      },
      /*notifyEveryUpdate=*/true);
}
} // namespace

Result<void> SynchronizedTimeBaseConsumer::RegisterStatusChangeNotifier(StatusChangeNotifier n) noexcept {
  if (!n) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kInvalidArgument));
  auto r = TimeBaseRegistry::Lookup(impl_->specifier);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onStatus = std::move(n);
  impl_->lastFlags = r->GetStatus();
  EnsureObserver(impl_, r);
  return Result<void>();
}

Result<void> SynchronizedTimeBaseConsumer::RegisterSynchronizationStateChangeNotifier(
    SynchronizationStateChangeNotifier n) noexcept {
  if (!n) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kInvalidArgument));
  auto r = TimeBaseRegistry::Lookup(impl_->specifier);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onSync = std::move(n);
  impl_->lastFlags = r->GetStatus();
  impl_->lastEver = true;
  EnsureObserver(impl_, r);
  return Result<void>();
}

Result<void> SynchronizedTimeBaseConsumer::RegisterTimeLeapNotifier(TimeLeapNotifier n) noexcept {
  if (!n) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kInvalidArgument));
  auto r = TimeBaseRegistry::Lookup(impl_->specifier);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onLeap = std::move(n);
  EnsureObserver(impl_, r);
  return Result<void>();
}

Result<void> SynchronizedTimeBaseConsumer::RegisterTimePrecisionMeasurementNotifier(
    PrecisionMeasurementNotifier n) noexcept {
  if (!n) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kInvalidArgument));
  auto r = TimeBaseRegistry::Lookup(impl_->specifier);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  // SWS_TS_00803: not allowed for a system-wide global time master time base.
  if (r->Config().isSystemWideGlobalTimeMaster) {
    return Result<void>::FromError(MakeErrorCode(TsyncErrc::kFunctionNotSupported));
  }
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onPrecision = std::move(n);
  if (impl_->precObs == 0) {
    std::weak_ptr<Impl> weak = impl_;
    impl_->precRes = r;
    impl_->precObs = r->AddPrecisionObserver([weak](TimePrecisionMeasurement const &m) {
      auto self = weak.lock();
      if (!self) return;
      PrecisionMeasurementNotifier cb;
      {
        std::lock_guard<std::mutex> lk(self->mu);
        cb = self->onPrecision;
      }
      if (cb) cb(m);
    });
  }
  return Result<void>();
}

Result<void> SynchronizedTimeBaseConsumer::RegisterTimeValidationNotification(TimeValidationNotifier n) noexcept {
  if (!n) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kInvalidArgument));
  auto r = TimeBaseRegistry::Lookup(impl_->specifier);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onValidation = std::move(n);
  if (impl_->valObs == 0) {
    std::weak_ptr<Impl> weak = impl_;
    impl_->valRes = r;
    impl_->valObs = r->AddValidationObserver([weak](TimeValidationData const &d) {
      auto self = weak.lock();
      if (!self) return;
      TimeValidationNotifier cb;
      {
        std::lock_guard<std::mutex> lk(self->mu);
        cb = self->onValidation;
      }
      if (cb) cb(d);
    });
  }
  return Result<void>();
}

void SynchronizedTimeBaseConsumer::UnregisterTimePrecisionMeasurementNotifier() noexcept {
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onPrecision = nullptr;
}

void SynchronizedTimeBaseConsumer::UnregisterTimeValidationNotification() noexcept {
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onValidation = nullptr;
}

void SynchronizedTimeBaseConsumer::UnregisterStatusChangeNotifier() noexcept {
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onStatus = nullptr;
}
void SynchronizedTimeBaseConsumer::UnregisterSynchronizationStateChangeNotifier() noexcept {
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onSync = nullptr;
}
void SynchronizedTimeBaseConsumer::UnregisterTimeLeapNotifier() noexcept {
  std::lock_guard<std::mutex> g(impl_->mu);
  impl_->onLeap = nullptr;
}

// ------------------------------------------------------------------------------- provider

SynchronizedTimeBaseProvider::SynchronizedTimeBaseProvider(ara::core::InstanceSpecifier const &s) noexcept
    : specifier_(std::string(s.ToString())) {}

Result<void> SynchronizedTimeBaseProvider::SetTime(Timestamp t, std::uint8_t const *u, std::size_t n) noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  auto ud = ToUserData(u, n);
  if (!ud.HasValue()) return Result<void>::FromError(ud.Error());
  return r->Apply(t, r->GetRateDeviation().ValueOr(0.0), false, ud.Value(), /*forceStep=*/true);
}

Result<void> SynchronizedTimeBaseProvider::UpdateTime(Timestamp t, std::uint8_t const *u, std::size_t n) noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  auto ud = ToUserData(u, n);
  if (!ud.HasValue()) return Result<void>::FromError(ud.Error());
  return r->Apply(t, r->GetRateDeviation().ValueOr(0.0), false, ud.Value(), /*forceStep=*/false);
}

Timestamp SynchronizedTimeBaseProvider::GetCurrentTime() const noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return Timestamp{};
  auto s = r->GetCurrentTime();
  return s.HasValue() ? s.Value().time : Timestamp{};
}

Result<void> SynchronizedTimeBaseProvider::SetRateCorrection(double c) noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  return r->SetRateCorrection(c);
}

double SynchronizedTimeBaseProvider::GetRateDeviation() const noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return 0.0;
  auto d = r->GetRateDeviation();
  return d.HasValue() ? d.Value() : 0.0;
}

Result<void> SynchronizedTimeBaseProvider::SetUserData(std::uint8_t const *u, std::size_t n) noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  auto ud = ToUserData(u, n);
  if (!ud.HasValue()) return Result<void>::FromError(ud.Error());
  return r->SetUserDataOnly(ud.Value());
}

std::vector<std::uint8_t> SynchronizedTimeBaseProvider::GetUserData() const {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return {};
  UserData u = r->GetUserData();
  return std::vector<std::uint8_t>(u.bytes, u.bytes + u.length);
}

// ------------------------------------------------------------------ provider validation helper

namespace internal {
struct ValidationRegistration {
  std::mutex mu;
  std::weak_ptr<TimeBaseResource> resource;
  std::size_t id{0};
  SynchronizedTimeBaseConsumer::TimeValidationNotifier notifier;
  ~ValidationRegistration() {
    if (auto r = resource.lock()) {
      if (id != 0) r->RemoveValidationObserver(id);
    }
  }
};
} // namespace internal

namespace {
Result<void> RegisterValidation(std::shared_ptr<internal::ValidationRegistration> &reg, std::string const &spec,
                                SynchronizedTimeBaseConsumer::TimeValidationNotifier n) {
  if (!n) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kInvalidArgument));
  auto r = TimeBaseRegistry::Lookup(spec);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  if (!reg) reg = std::make_shared<internal::ValidationRegistration>();
  std::lock_guard<std::mutex> g(reg->mu);
  reg->notifier = std::move(n); // SWS_TS_01114/01115: a new registration overwrites the old one
  if (reg->id == 0) {
    std::weak_ptr<internal::ValidationRegistration> weak = reg;
    reg->resource = r;
    reg->id = r->AddValidationObserver([weak](TimeValidationData const &d) {
      auto self = weak.lock();
      if (!self) return;
      SynchronizedTimeBaseConsumer::TimeValidationNotifier cb;
      {
        std::lock_guard<std::mutex> lk(self->mu);
        cb = self->notifier;
      }
      if (cb) cb(d);
    });
  }
  return Result<void>();
}

void UnregisterValidation(std::shared_ptr<internal::ValidationRegistration> const &reg) {
  if (!reg) return;
  std::lock_guard<std::mutex> g(reg->mu);
  reg->notifier = nullptr;
}

Result<void> PublishValidation(std::string const &spec, TimeValidationData data, TimeValidationData::Kind kind) {
  auto r = TimeBaseRegistry::Lookup(spec);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  data.kind = kind;
  r->PublishValidationData(data);
  return Result<void>();
}
} // namespace

Result<void> SynchronizedTimeBaseProvider::RegisterTimeValidationNotification(TimeValidationNotifier n) noexcept {
  return RegisterValidation(validation_, specifier_, std::move(n));
}
void SynchronizedTimeBaseProvider::UnregisterTimeValidationNotification() noexcept { UnregisterValidation(validation_); }
Result<void> SynchronizedTimeBaseProvider::SetMasterTimingData(TimeValidationData d) noexcept {
  return PublishValidation(specifier_, d, TimeValidationData::Kind::kMasterTimingData);
}
Result<void> SynchronizedTimeBaseProvider::SetPdelayResponderData(TimeValidationData d) noexcept {
  return PublishValidation(specifier_, d, TimeValidationData::Kind::kPdelayResponderData);
}

// ---------------------------------------------------------------------- offset time base provider

OffsetTimeBaseProvider::OffsetTimeBaseProvider(ara::core::InstanceSpecifier const &s) noexcept
    : specifier_(std::string(s.ToString())) {}

Result<void> OffsetTimeBaseProvider::SetOffsetTime(Timestamp t, std::uint8_t const *u, std::size_t n) noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  if (!r->Config().isOffsetTimeBase) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kTimeCannotSet));
  auto ud = ToUserData(u, n);
  if (!ud.HasValue()) return Result<void>::FromError(ud.Error());
  return r->Apply(t, r->GetRateDeviation().ValueOr(0.0), false, ud.Value(), /*forceStep=*/true);
}

Timestamp OffsetTimeBaseProvider::GetCurrentTime() const noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return Timestamp{};
  auto s = r->GetCurrentTime();
  return s.HasValue() ? s.Value().time : Timestamp{};
}

Result<void> OffsetTimeBaseProvider::SetRateCorrection(double c) noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  return r->SetRateCorrection(c);
}

double OffsetTimeBaseProvider::GetRateDeviation() const noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return 0.0;
  auto d = r->GetRateDeviation();
  return d.HasValue() ? d.Value() : 0.0;
}

Result<void> OffsetTimeBaseProvider::SetUserData(std::uint8_t const *u, std::size_t n) noexcept {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return Result<void>::FromError(MakeErrorCode(TsyncErrc::kDaemonConnectionLost));
  auto ud = ToUserData(u, n);
  if (!ud.HasValue()) return Result<void>::FromError(ud.Error());
  return r->SetUserDataOnly(ud.Value());
}

std::vector<std::uint8_t> OffsetTimeBaseProvider::GetUserData() const {
  auto r = TimeBaseRegistry::Lookup(specifier_);
  if (!r) return {};
  UserData u = r->GetUserData();
  return std::vector<std::uint8_t>(u.bytes, u.bytes + u.length);
}

Result<void> OffsetTimeBaseProvider::RegisterTimeValidationNotification(TimeValidationNotifier n) noexcept {
  return RegisterValidation(validation_, specifier_, std::move(n));
}
void OffsetTimeBaseProvider::UnregisterTimeValidationNotification() noexcept { UnregisterValidation(validation_); }
Result<void> OffsetTimeBaseProvider::SetMasterTimingData(TimeValidationData d) noexcept {
  return PublishValidation(specifier_, d, TimeValidationData::Kind::kMasterTimingData);
}
Result<void> OffsetTimeBaseProvider::SetPdelayResponderData(TimeValidationData d) noexcept {
  return PublishValidation(specifier_, d, TimeValidationData::Kind::kPdelayResponderData);
}

} // namespace ara::tsync
