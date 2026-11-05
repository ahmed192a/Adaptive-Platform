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
                                                UserData user) {
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
    } else {
      std::int64_t const current = TimeAtLocked(state_, now);
      std::int64_t const offset = target - current;
      if (std::llabs(offset) > Ns(config_.stepThreshold)) {
        state_.baseNs = target;
        state_.pendingOffsetNs = 0;
        state_.leap = true;
        stepped = true;
      } else {
        state_.baseNs = current; // continuity; the residual offset is slewed in
        state_.pendingOffsetNs = offset;
        state_.slewEnd = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(config_.slewWindow);
        state_.leap = false;
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

void TimeBaseResource::Deliver(TimeBaseSnapshot const &snap, ChangeReason reason, bool statusChanged) {
  UpdateCallback cb;
  bool every;
  {
    std::lock_guard<std::mutex> lk(mutex_);
    cb = callback_;
    every = notifyEvery_;
    lastNotified_ = snap.status;
  }
  if (!cb) return;
  if (reason == ChangeReason::kTimeStep) {
    cb(snap, ChangeReason::kTimeStep);
  } else if (statusChanged && reason != ChangeReason::kStatus) {
    cb(snap, ChangeReason::kStatus);
  } else if (reason == ChangeReason::kStatus) {
    cb(snap, ChangeReason::kStatus);
  } else if (every) {
    cb(snap, ChangeReason::kUpdate);
  }
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
