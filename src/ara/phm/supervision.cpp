#include "ara/phm/supervision.h"

namespace ara::phm {

void ElementarySupervision::Activate(TimePoint now) {
  mStatus = ElementarySupervisionStatus::kOK;
  DoActivate(now);
}
void ElementarySupervision::Deactivate() noexcept {
  mStatus = ElementarySupervisionStatus::kDEACTIVATED;
}
void ElementarySupervision::OnCheckpoint(CheckpointRef const &cp, TimePoint now) {
  if (IsActive() && mStatus != ElementarySupervisionStatus::kEXPIRED) {
    DoCheckpoint(cp, now);
  }
}
void ElementarySupervision::Evaluate(TimePoint now) {
  if (IsActive() && mStatus != ElementarySupervisionStatus::kEXPIRED) {
    DoEvaluate(now);
  }
}

// ---- alive ------------------------------------------------------------------
void AliveSupervision::DoActivate(TimePoint now) { RestartTiming(now); mFailedCycles = 0; }

void AliveSupervision::RestartTiming(TimePoint now) {
  mCycleStart = now;
  mIndications = 0;
}

void AliveSupervision::DoCheckpoint(CheckpointRef const &cp, TimePoint now) {
  // Close all reference cycles that ended before this checkpoint so that it is
  // attributed to the cycle it actually belongs to, independent of how often
  // Evaluate() is called.
  DoEvaluate(now);
  if (cp == mCfg.checkpoint && Status() != ElementarySupervisionStatus::kEXPIRED) {
    ++mIndications;
  }
}

void AliveSupervision::DoEvaluate(TimePoint now) {
  // Close every reference cycle that has completely elapsed.
  while (mCfg.referenceCycle > Duration::zero() && now - mCycleStart >= mCfg.referenceCycle &&
         Status() != ElementarySupervisionStatus::kEXPIRED) {
    std::int64_t const expected = mCfg.expectedAliveIndications;
    std::int64_t const lo = expected - static_cast<std::int64_t>(mCfg.minMargin);
    std::int64_t const hi = expected + static_cast<std::int64_t>(mCfg.maxMargin);
    std::int64_t const got = mIndications;
    if (got >= lo && got <= hi) {
      if (mFailedCycles > 0) {
        --mFailedCycles;
      }
    } else {
      ++mFailedCycles;
    }
    if (mFailedCycles > mCfg.failedReferenceCyclesTolerance) {
      Expire();
    } else if (mFailedCycles > 0) {
      SetStatus(ElementarySupervisionStatus::kFAILED);
    } else {
      SetStatus(ElementarySupervisionStatus::kOK);
    }
    mCycleStart += std::chrono::duration_cast<Duration>(mCfg.referenceCycle);
    mIndications = 0;
  }
}

// ---- deadline ---------------------------------------------------------------
void DeadlineSupervision::DoCheckpoint(CheckpointRef const &cp, TimePoint now) {
  if (cp == mCfg.start) {
    mStarted = now; // a repeated start restarts the measurement
  } else if (cp == mCfg.end && mStarted.has_value()) {
    Duration const elapsed = now - *mStarted;
    mStarted.reset();
    if (elapsed < mCfg.minDeadline || elapsed > mCfg.maxDeadline) {
      Expire();
    }
  }
}

void DeadlineSupervision::DoEvaluate(TimePoint now) {
  if (mStarted.has_value() && now - *mStarted > mCfg.maxDeadline) {
    Expire(); // end checkpoint did not arrive in time
  }
}

// ---- logical ----------------------------------------------------------------
std::set<CheckpointRef> LogicalSupervision::Checkpoints() const {
  std::set<CheckpointRef> out;
  for (auto c : mCfg.checkpoints) {
    out.insert(CheckpointRef{mCfg.entity, c});
  }
  return out;
}

void LogicalSupervision::DoCheckpoint(CheckpointRef const &cp, TimePoint) {
  if (cp.entity != mCfg.entity || mCfg.checkpoints.count(cp.checkpoint) == 0) {
    return;
  }
  if (!mLast.has_value()) {
    if (mCfg.initial.count(cp.checkpoint) == 0) {
      Expire();
      return;
    }
  } else if (mCfg.transitions.count({*mLast, cp.checkpoint}) == 0) {
    Expire();
    return;
  }
  mLast = mCfg.final.count(cp.checkpoint) != 0 ? std::nullopt : std::optional<CheckpointType>(cp.checkpoint);
}

} // namespace ara::phm
