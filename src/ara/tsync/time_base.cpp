#include "ara/tsync/time_base.h"

#include <cmath>

namespace ara::tsync {

namespace {
using SteadyTp = std::chrono::steady_clock::time_point;
std::int64_t Ns(Duration d) { return d.count(); }
} // namespace

ara::core::Result<std::shared_ptr<TimeBaseResource>> TimeBaseResource::Create(TimeBaseConfig config) {
  if (config.timeout <= Duration::zero() || config.stepThreshold < Duration::zero() ||
      config.slewWindow <= Duration::zero()) {
    return ara::core::Result<std::shared_ptr<TimeBaseResource>>::FromError(
        MakeErrorCode(TsyncErrc::kInvalidArgument));
  }
  if (!config.clock) config.clock = [] { return std::chrono::steady_clock::now(); };
  return std::shared_ptr<TimeBaseResource>(new TimeBaseResource(std::move(config)));
}

TimeBaseResource::TimeBaseResource(TimeBaseConfig config) : config_(std::move(config)) {}

std::int64_t TimeBaseResource::TimeAtLocked(State const &s, SteadyTp now) const {
  std::int64_t const dt = Ns(std::chrono::duration_cast<Duration>(now - s.localAtSync));
  auto const scaled = static_cast<std::int64_t>(std::llround(static_cast<long double>(dt) * (1.0L + s.rate)));
  std::int64_t applied = s.pendingOffsetNs;
  if (s.pendingOffsetNs != 0) {
    std::int64_t const win = Ns(std::chrono::duration_cast<Duration>(s.slewEnd - s.localAtSync));
    if (win > 0 && dt < win) {
      applied = static_cast<std::int64_t>(
          std::llround(static_cast<long double>(s.pendingOffsetNs) * dt / static_cast<long double>(win)));
    }
  }
  return s.baseNs + scaled + applied;
}

StatusFlags TimeBaseResource::StatusLocked(State const &s, SteadyTp now) const {
  if (!s.everSynced) return {};
  std::uint32_t bits = 0;
  bool const timedOut = (now - s.lastUpdate) > config_.timeout;
  bits |= timedOut ? static_cast<std::uint32_t>(StatusFlag::kTimeout)
                   : static_cast<std::uint32_t>(StatusFlag::kSynchronized);
  if (s.gateway) bits |= static_cast<std::uint32_t>(StatusFlag::kSyncToGateway);
  if (s.leap) bits |= static_cast<std::uint32_t>(StatusFlag::kTimeLeap);
  if (s.pendingOffsetNs != 0 && now < s.slewEnd) bits |= static_cast<std::uint32_t>(StatusFlag::kPendingRateCorr);
  return StatusFlags(bits);
}

TimeBaseSnapshot TimeBaseResource::SnapshotLocked(SteadyTp now) const {
  TimeBaseSnapshot snap;
  snap.time = SynchronizedTimePoint(Duration(TimeAtLocked(state_, now)));
  snap.rateDeviation = state_.rate;
  snap.status = StatusLocked(state_, now);
  snap.userData = state_.user;
  snap.localTime = now;
  if (state_.leap) snap.leapJump = state_.leapDir >= 0 ? LeapJump::kTimeLeapFuture : LeapJump::kTimeLeapPast;
  return snap;
}

ara::core::Result<TimeBaseSnapshot> TimeBaseResource::GetCurrentTime() const {
  auto const now = Now();
  std::lock_guard<std::mutex> lk(mutex_);
  if (!state_.everSynced) {
    return ara::core::Result<TimeBaseSnapshot>::FromError(MakeErrorCode(TsyncErrc::kNotSynchronized));
  }
  return SnapshotLocked(now);
}

ara::core::Result<double> TimeBaseResource::GetRateDeviation() const {
  std::lock_guard<std::mutex> lk(mutex_);
  if (!state_.everSynced) {
    return ara::core::Result<double>::FromError(MakeErrorCode(TsyncErrc::kNotSynchronized));
  }
  return state_.rate;
}

StatusFlags TimeBaseResource::GetStatus() const {
  auto const now = Now();
  std::lock_guard<std::mutex> lk(mutex_);
  return StatusLocked(state_, now);
}

UserData TimeBaseResource::GetUserData() const {
  std::lock_guard<std::mutex> lk(mutex_);
  return state_.user;
}

void TimeBaseResource::SetUpdateCallback(UpdateCallback cb, bool notifyEveryUpdate) {
  auto const now = Now();
  std::lock_guard<std::mutex> lk(mutex_);
  callback_ = std::move(cb);
  notifyEvery_ = notifyEveryUpdate;
  lastNotified_ = StatusLocked(state_, now);
}

ara::core::Result<void> TimeBaseResource::Apply(SynchronizedTimePoint t, double rate, bool gateway,
                                                UserData user, bool forceStep) {
  if (!std::isfinite(rate) || rate <= -1.0 || user.length > 3) {
    return ara::core::Result<void>::FromError(MakeErrorCode(TsyncErrc::kInvalidArgument));
  }
  auto const now = Now();
  TimeBaseSnapshot snap;
  bool stepped = false;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    std::int64_t const target = t.time_since_epoch().count();
    if (!state_.everSynced) {
      state_.baseNs = target;
      state_.pendingOffsetNs = 0;
      state_.leap = false;
      state_.leapDir = 0;
    } else {
      std::int64_t const current = TimeAtLocked(state_, now);
      std::int64_t const offset = target - current;
      bool const beyondThreshold = std::llabs(offset) > Ns(config_.stepThreshold);
      if (beyondThreshold || forceStep) {
        state_.baseNs = target;
        state_.pendingOffsetNs = 0;
        state_.leap = beyondThreshold;
        state_.leapDir = offset >= 0 ? 1 : -1;
        stepped = beyondThreshold;
      } else {
        state_.baseNs = current; // continuity; the residual offset is slewed in
        state_.pendingOffsetNs = offset;
        state_.slewEnd = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(config_.slewWindow);
        state_.leap = false;
        state_.leapDir = 0;
      }
    }
    state_.everSynced = true;
    state_.localAtSync = now;
    state_.lastUpdate = now;
    state_.rate = rate;
    state_.gateway = gateway;
    state_.user = user;
    snap = SnapshotLocked(now);
  }
  bool const statusChanged = (snap.status != lastNotified_);
  Deliver(snap, stepped ? ChangeReason::kTimeStep : ChangeReason::kUpdate, statusChanged);
  return ara::core::Result<void>();
}

namespace {
void Invoke(TimeBaseResource::UpdateCallback const &cb, bool every, TimeBaseSnapshot const &snap,
            ChangeReason reason, bool statusChanged) {
  if (!cb) return;
  if (reason == ChangeReason::kTimeStep) {
    cb(snap, ChangeReason::kTimeStep);
  } else if (statusChanged || reason == ChangeReason::kStatus) {
    cb(snap, ChangeReason::kStatus);
  } else if (every) {
    cb(snap, ChangeReason::kUpdate);
  }
}
} // namespace

void TimeBaseResource::Deliver(TimeBaseSnapshot const &snap, ChangeReason reason, bool statusChanged) {
  UpdateCallback cb;
  bool every;
  std::vector<Observer> obs;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    cb = callback_;
    every = notifyEvery_;
    obs = observers_;
    lastNotified_ = snap.status;
  }
  Invoke(cb, every, snap, reason, statusChanged);
  for (auto const &o : obs) Invoke(o.cb, o.every, snap, reason, statusChanged);
}

std::size_t TimeBaseResource::AddObserver(UpdateCallback cb, bool notifyEveryUpdate) {
  std::lock_guard<std::mutex> lk(mutex_);
  std::size_t const id = nextObserverId_++;
  observers_.push_back(Observer{id, std::move(cb), notifyEveryUpdate});
  return id;
}

void TimeBaseResource::RemoveObserver(std::size_t id) {
  std::lock_guard<std::mutex> lk(mutex_);
  for (auto it = observers_.begin(); it != observers_.end(); ++it) {
    if (it->id == id) { observers_.erase(it); return; }
  }
}

ara::core::Result<void> TimeBaseResource::SetRateCorrection(double correction) {
  if (!config_.allowProviderRateCorrection) {
    return ara::core::Result<void>::FromError(MakeErrorCode(TsyncErrc::kFunctionNotSupported));
  }
  if (!std::isfinite(correction) || std::fabs(correction) > config_.providerRateDeviationMax ||
      correction <= -1.0) {
    return ara::core::Result<void>::FromError(MakeErrorCode(TsyncErrc::kLimitsExceeded));
  }
  auto const now = Now();
  std::lock_guard<std::mutex> lk(mutex_);
  if (state_.everSynced) {
    // Re-base so the time stays continuous, then run at the new rate. The not yet applied part
    // of a slew continues from "now".
    std::int64_t remaining = 0;
    if (state_.pendingOffsetNs != 0 && now < state_.slewEnd) {
      std::int64_t const win = Ns(std::chrono::duration_cast<Duration>(state_.slewEnd - state_.localAtSync));
      std::int64_t const dt = Ns(std::chrono::duration_cast<Duration>(now - state_.localAtSync));
      std::int64_t const applied = win > 0 ? static_cast<std::int64_t>(std::llround(
                                                 static_cast<long double>(state_.pendingOffsetNs) * dt / win))
                                           : state_.pendingOffsetNs;
      remaining = state_.pendingOffsetNs - applied;
    }
    state_.baseNs = TimeAtLocked(state_, now);
    state_.localAtSync = now;
    state_.pendingOffsetNs = remaining;
  }
  state_.rate = correction;
  return ara::core::Result<void>();
}

ara::core::Result<void> TimeBaseResource::SetUserDataOnly(UserData user) {
  if (user.length > 3) return ara::core::Result<void>::FromError(MakeErrorCode(TsyncErrc::kInvalidArgument));
  std::lock_guard<std::mutex> lk(mutex_);
  state_.user = user;
  return ara::core::Result<void>();
}

void TimeBaseResource::Poll() {
  auto const now = Now();
  TimeBaseSnapshot snap;
  bool changed;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    if (!state_.everSynced) return;
    snap = SnapshotLocked(now);
    changed = snap.status != lastNotified_;
  }
  if (changed) Deliver(snap, ChangeReason::kStatus, true);
}

} // namespace ara::tsync
