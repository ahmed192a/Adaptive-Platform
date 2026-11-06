#include "ara/phm/health_monitor.h"

#include <algorithm>

namespace ara::phm {

using ara::core::Result;

HealthMonitor::HealthMonitor() = default;
HealthMonitor::HealthMonitor(std::shared_ptr<apext::phm::WatchdogInterface> watchdog)
    : mWatchdog(std::move(watchdog)) {}
HealthMonitor::~HealthMonitor() { StopBackgroundEvaluation(); }

HealthMonitor &HealthMonitor::Instance() {
  static HealthMonitor instance;
  return instance;
}

// ---------------------------------------------------------------------------
// configuration
// ---------------------------------------------------------------------------
std::set<std::string> HealthMonitor::EntitiesOf(ElementarySupervision const &s) {
  std::set<std::string> out;
  for (auto const &c : s.Checkpoints()) {
    out.insert(c.entity);
  }
  return out;
}

Result<void> HealthMonitor::AddSupervision(std::shared_ptr<ElementarySupervision> s) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (!s || mSupervisions.count(s->Name()) != 0) {
    return Result<void>(MakeErrorCode(PhmErrc::kInvalidConfiguration, 0, "duplicate or null supervision"));
  }
  for (auto const &cp : s->Checkpoints()) {
    mIndex[cp].push_back(s.get());
    mKnownEntities.insert(cp.entity);
  }
  mSupervisions[s->Name()] = std::move(s);
  return Result<void>();
}

Result<void> HealthMonitor::AddGlobalSupervision(GlobalSupervisionConfig cfg) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (mGlobals.count(cfg.name) != 0 || cfg.members.empty()) {
    return Result<void>(MakeErrorCode(PhmErrc::kInvalidConfiguration, 0, "duplicate global or no members"));
  }
  for (auto const &m : cfg.members) {
    if (mSupervisions.count(m) == 0) {
      return Result<void>(MakeErrorCode(PhmErrc::kUnknownSupervision, 0, "member supervision not configured"));
    }
  }
  Global g;
  g.cfg = cfg;
  mGlobals[cfg.name] = std::move(g);
  return Result<void>();
}

Result<void> HealthMonitor::Configure(PhmConfig config) {
  for (auto &s : config.supervisions) {
    auto r = AddSupervision(s);
    if (!r.HasValue()) return r;
  }
  for (auto &g : config.globals) {
    auto r = AddGlobalSupervision(g);
    if (!r.HasValue()) return r;
  }
  return Result<void>();
}

void HealthMonitor::SetWatchdog(std::shared_ptr<apext::phm::WatchdogInterface> w) {
  std::lock_guard<std::mutex> lock(mMutex);
  mWatchdog = std::move(w);
}
void HealthMonitor::SetProductionErrorReporter(ProductionErrorReporter reporter) {
  std::lock_guard<std::mutex> lock(mMutex);
  mErrorReporter = std::move(reporter);
}

void HealthMonitor::SetStatusListener(StatusListener l) {
  std::lock_guard<std::mutex> lock(mMutex);
  mListener = std::move(l);
}
void HealthMonitor::Clear() {
  StopBackgroundEvaluation();
  std::lock_guard<std::mutex> lock(mMutex);
  mSupervisions.clear();
  mIndex.clear();
  mGlobals.clear();
  mRecovery.clear();
  mDisabledEntities.clear();
  mKnownEntities.clear();
  mWatchdogFired = false;
}

Result<void> HealthMonitor::OfferRecoveryAction(std::string const &spec, RecoveryHandler h) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (mRecovery.count(spec) != 0) {
    return Result<void>(MakeErrorCode(PhmErrc::kAlreadyOffered));
  }
  mRecovery[spec] = std::move(h);
  return Result<void>();
}
void HealthMonitor::StopOfferRecoveryAction(std::string const &spec) {
  std::lock_guard<std::mutex> lock(mMutex);
  mRecovery.erase(spec);
}

// ---------------------------------------------------------------------------
// activation
// ---------------------------------------------------------------------------
void HealthMonitor::ActivateGlobal(Global &g, TimePoint now) {
  for (auto const &m : g.cfg.members) {
    mSupervisions[m]->Activate(now);
  }
  g.status = GlobalSupervisionStatus::kOK;
  g.recoveryDispatched = false;
  g.pendingRecovery.reset();
}
void HealthMonitor::DeactivateGlobal(Global &g) {
  for (auto const &m : g.cfg.members) {
    mSupervisions[m]->Deactivate();
  }
  g.status = GlobalSupervisionStatus::kDEACTIVATED;
  g.recoveryDispatched = false;
  g.pendingRecovery.reset();
}

void HealthMonitor::Start(TimePoint now) {
  std::lock_guard<std::mutex> lock(mMutex);
  for (auto &[name, g] : mGlobals) {
    if (g.cfg.activeStates.empty()) {
      ActivateGlobal(g, now);
    }
  }
}

void HealthMonitor::SetFunctionGroupState(std::string const &fg, std::string const &state, TimePoint now) {
  std::lock_guard<std::mutex> lock(mMutex);
  for (auto &[name, g] : mGlobals) {
    if (g.cfg.functionGroup != fg || g.cfg.activeStates.empty()) {
      continue;
    }
    bool const shouldRun = g.cfg.activeStates.count(state) != 0;
    bool const running = g.status != GlobalSupervisionStatus::kDEACTIVATED;
    if (shouldRun && !running) {
      ActivateGlobal(g, now);
    } else if (!shouldRun && running) {
      DeactivateGlobal(g);
    }
  }
}

// ---------------------------------------------------------------------------
// runtime
// ---------------------------------------------------------------------------
bool HealthMonitor::Suspended(ElementarySupervision const &s) const {
  for (auto const &e : EntitiesOf(s)) {
    if (mDisabledEntities.count(e) != 0) {
      return true;
    }
  }
  return false;
}

void HealthMonitor::ReportCheckpoint(std::string const &entity, CheckpointType cp, TimePoint now) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (mDisabledEntities.count(entity) != 0) {
    return; // SupervisedEntity::Disable(): checkpoints are ignored
  }
  CheckpointRef const ref{entity, cp};
  auto it = mIndex.find(ref);
  if (it == mIndex.end()) {
    return;
  }
  for (auto *s : it->second) {
    s->OnCheckpoint(ref, now);
  }
}

void HealthMonitor::SetEntityEnabled(std::string const &entity, bool enabled, TimePoint now) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (!enabled) {
    mDisabledEntities.insert(entity);
    return;
  }
  if (mDisabledEntities.erase(entity) != 0) {
    for (auto &[name, s] : mSupervisions) { // restart alive reference cycles
      if (EntitiesOf(*s).count(entity) != 0 && s->IsActive()) {
        s->RestartTiming(now);
      }
    }
  }
}

void HealthMonitor::Escalate(std::vector<std::function<void()>> &deferred) {
  if (mWatchdogFired) {
    return;
  }
  mWatchdogFired = true;
  if (mWatchdog) {
    auto wd = mWatchdog;
    auto reporter = mErrorReporter;
    deferred.emplace_back([wd, reporter] {
      if (reporter) {
        reporter(kWatchdogResetEvent, true); // SWS_PHM_01363: report before the reset takes effect
      }
      wd->FireWatchdogReaction();
    });
  }
}

void HealthMonitor::EvaluateLocked(TimePoint now, std::vector<std::function<void()>> &deferred) {
  for (auto &[name, s] : mSupervisions) {
    if (!Suspended(*s)) {
      s->Evaluate(now);
    }
  }
  for (auto &[name, g] : mGlobals) {
    GlobalSupervisionStatus const before = g.status;
    if (before == GlobalSupervisionStatus::kDEACTIVATED) {
      continue;
    }
    // aggregate: worst member status wins
    bool anyExpired = false, anyFailed = false;
    ElementarySupervision const *firstExpired = nullptr;
    for (auto const &m : g.cfg.members) {
      auto const &s = *mSupervisions[m];
      if (s.Status() == ElementarySupervisionStatus::kEXPIRED) {
        anyExpired = true;
        if (firstExpired == nullptr) firstExpired = &s;
      } else if (s.Status() == ElementarySupervisionStatus::kFAILED) {
        anyFailed = true;
      }
    }
    GlobalSupervisionStatus next = before;
    if (before != GlobalSupervisionStatus::kSTOPPED) { // STOPPED is final
      next = anyExpired ? GlobalSupervisionStatus::kEXPIRED
                        : anyFailed ? GlobalSupervisionStatus::kFAILED : GlobalSupervisionStatus::kOK;
    }
    if (next == GlobalSupervisionStatus::kEXPIRED && before != GlobalSupervisionStatus::kEXPIRED) {
      g.expiredSince = now;
    }
    // ---- recovery on the EXPIRED edge ---------------------------------------
    if (next == GlobalSupervisionStatus::kEXPIRED && !g.recoveryDispatched && firstExpired != nullptr) {
      g.recoveryDispatched = true;
      ExpiredSupervision ev{g.cfg.name, firstExpired->Name(), g.cfg.functionGroup,
                            firstExpired->Type(), g.cfg.executionError};
      auto handler = mRecovery.find(g.cfg.recoveryAction);
      if (handler == mRecovery.end()) {
        if (g.cfg.escalateToWatchdog) {
          Escalate(deferred); // nobody can recover: let the watchdog act
        }
      } else {
        // run outside the lock; the resulting future is polled on later evaluations
        auto fn = handler->second;
        auto *gp = &g;
        auto holder = std::make_shared<std::optional<ara::core::Future<void>>>();
        deferred.emplace_back([fn, ev, holder] { holder->emplace(fn(ev)); });
        deferred.emplace_back([this, gp, holder] {
          std::lock_guard<std::mutex> lock(mMutex);
          auto found = std::find_if(mGlobals.begin(), mGlobals.end(),
                                    [gp](auto const &e) { return &e.second == gp; });
          if (found != mGlobals.end() && holder->has_value()) {
            found->second.pendingRecovery = std::move(**holder);
          }
        });
      }
    }
    // ---- pending recovery result ------------------------------------------------
    if (g.pendingRecovery.has_value() && g.pendingRecovery->is_ready()) {
      auto res = g.pendingRecovery->GetResult();
      g.pendingRecovery.reset();
      if (!res.HasValue() && g.cfg.escalateToWatchdog) {
        Escalate(deferred); // e.g. kSMCanNotHandleRecovery
      }
    }
    // ---- platform critical supervisions: EXPIRED -> STOPPED ---------------------
    if (next == GlobalSupervisionStatus::kEXPIRED && g.cfg.platformCritical &&
        now - g.expiredSince >= g.cfg.expiredTolerance) {
      next = GlobalSupervisionStatus::kSTOPPED;
      Escalate(deferred);
    }
    g.status = next;
    if (next != before && mListener) {
      auto listener = mListener;
      std::string const gname = g.cfg.name;
      deferred.emplace_back([listener, gname, before, next] { listener(gname, before, next); });
    }
  }
  bool anyStopped = std::any_of(mGlobals.begin(), mGlobals.end(), [](auto const &e) {
    return e.second.status == GlobalSupervisionStatus::kSTOPPED;
  });
  if (mWatchdog && !anyStopped && !mWatchdogFired) {
    auto wd = mWatchdog;
    deferred.emplace_back([wd] { wd->AliveNotification(); });
  }
}

void HealthMonitor::Evaluate(TimePoint now) {
  std::vector<std::function<void()>> deferred;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    EvaluateLocked(now, deferred);
  }
  for (auto &d : deferred) {
    d(); // callbacks may call back into the monitor, hence outside the lock
  }
}

// ---------------------------------------------------------------------------
// wall clock API
// ---------------------------------------------------------------------------
bool HealthMonitor::IsKnownEntity(std::string const &entity) const {
  std::lock_guard<std::mutex> lock(mMutex);
  return mKnownEntities.count(entity) != 0;
}
void HealthMonitor::ReportCheckpoint(std::string const &entity, CheckpointType cp) {
  ReportCheckpoint(entity, cp, ara::core::SteadyClock::now());
}
void HealthMonitor::SetEntityEnabled(std::string const &entity, bool enabled) {
  SetEntityEnabled(entity, enabled, ara::core::SteadyClock::now());
}

void HealthMonitor::StartBackgroundEvaluation(std::chrono::milliseconds period) {
  StopBackgroundEvaluation();
  {
    std::lock_guard<std::mutex> lock(mThreadMutex);
    mThreadStop = false;
  }
  mThread = std::thread([this, period] {
    std::unique_lock<std::mutex> lock(mThreadMutex);
    while (!mThreadStop) {
      lock.unlock();
      Evaluate(ara::core::SteadyClock::now());
      lock.lock();
      mThreadCv.wait_for(lock, period, [this] { return mThreadStop; });
    }
  });
}
void HealthMonitor::StopBackgroundEvaluation() {
  {
    std::lock_guard<std::mutex> lock(mThreadMutex);
    mThreadStop = true;
  }
  mThreadCv.notify_all();
  if (mThread.joinable()) {
    mThread.join();
  }
}

// ---------------------------------------------------------------------------
// observation
// ---------------------------------------------------------------------------
Result<ElementarySupervisionStatus> HealthMonitor::GetElementaryStatus(std::string const &name) const {
  std::lock_guard<std::mutex> lock(mMutex);
  auto it = mSupervisions.find(name);
  if (it == mSupervisions.end()) {
    return Result<ElementarySupervisionStatus>(MakeErrorCode(PhmErrc::kUnknownSupervision));
  }
  return Result<ElementarySupervisionStatus>(it->second->Status());
}
Result<GlobalSupervisionStatus> HealthMonitor::GetGlobalStatus(std::string const &name) const {
  std::lock_guard<std::mutex> lock(mMutex);
  auto it = mGlobals.find(name);
  if (it == mGlobals.end()) {
    return Result<GlobalSupervisionStatus>(MakeErrorCode(PhmErrc::kUnknownSupervision));
  }
  return Result<GlobalSupervisionStatus>(it->second.status);
}
bool HealthMonitor::WatchdogReactionFired() const {
  std::lock_guard<std::mutex> lock(mMutex);
  return mWatchdogFired;
}

} // namespace ara::phm
