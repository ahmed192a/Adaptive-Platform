/**
 * @file health_monitor.h
 * @brief PHM engine: owns the supervisions, aggregates global supervision
 *        status and dispatches recovery.
 *
 * In a deployed system this runs inside the PHM daemon; here it is a library
 * object so that it can be hosted in-process (see HealthMonitor::Instance())
 * or behind an IPC transport implementing CheckpointSink.
 *
 * Time is always passed in, so behaviour is fully deterministic in tests.
 */
#ifndef ARA_PHM_HEALTH_MONITOR_H_
#define ARA_PHM_HEALTH_MONITOR_H_

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ara/core/future.h"
#include "ara/core/result.h"
#include "ara/phm/phm_config.h"
#include "ara/phm/phm_error_domain.h"
#include "ara/phm/phm_types.h"
#include "ara/phm/watchdog_interface.h"

namespace ara::phm {

/// Describes why a recovery is requested (stand-in for ara::exec::ExecutionErrorEvent,
/// which needs a manifest backed FunctionGroup).
struct ExpiredSupervision {
  std::string globalSupervision;
  std::string supervision;      ///< first expired elementary supervision
  std::string functionGroup;
  TypeOfSupervision type{TypeOfSupervision::kAliveSupervision};
  std::uint32_t executionError{0};
};

using RecoveryHandler = std::function<ara::core::Future<void>(ExpiredSupervision const &)>;
using StatusListener = std::function<void(std::string const &globalSupervision,
                                          GlobalSupervisionStatus oldStatus,
                                          GlobalSupervisionStatus newStatus)>;

/// Transport used by SupervisedEntity to deliver checkpoints.
/// SWS_PHM_01363: name of the production error reported FAILED when the watchdog reaction fires.
inline constexpr char const *kWatchdogResetEvent = "PHM_E_WATCHDOG_RESET";
/// Sink for production errors (typically forwarding to ara::diag event reporting): (event name, failed).
using ProductionErrorReporter = std::function<void(std::string const &eventName, bool failed)>;

class CheckpointSink {
public:
  virtual ~CheckpointSink() = default;
  virtual bool IsKnownEntity(std::string const &entity) const = 0;
  virtual void ReportCheckpoint(std::string const &entity, CheckpointType checkpoint) = 0;
  virtual void SetEntityEnabled(std::string const &entity, bool enabled) = 0;
};

class HealthMonitor final : public CheckpointSink {
public:
  HealthMonitor();
  explicit HealthMonitor(std::shared_ptr<apext::phm::WatchdogInterface> watchdog);
  ~HealthMonitor() override;
  HealthMonitor(HealthMonitor const &) = delete;
  HealthMonitor &operator=(HealthMonitor const &) = delete;

  /// Process wide instance used by the default SupervisedEntity transport.
  static HealthMonitor &Instance();

  // ---- configuration -------------------------------------------------------
  ara::core::Result<void> Configure(PhmConfig config);
  ara::core::Result<void> AddSupervision(std::shared_ptr<ElementarySupervision> supervision);
  ara::core::Result<void> AddGlobalSupervision(GlobalSupervisionConfig config);
  void SetWatchdog(std::shared_ptr<apext::phm::WatchdogInterface> watchdog);
  void SetStatusListener(StatusListener listener);
  /// Registers the production-error sink used for PHM_E_WATCHDOG_RESET (SWS_PHM_01363).
  void SetProductionErrorReporter(ProductionErrorReporter reporter);
  /// Removes all configuration and state.
  void Clear();

  // ---- recovery actions ----------------------------------------------------
  ara::core::Result<void> OfferRecoveryAction(std::string const &specifier, RecoveryHandler handler);
  void StopOfferRecoveryAction(std::string const &specifier);

  // ---- runtime (explicit time) ----------------------------------------------
  /// Activates all supervisions that are not bound to function group states.
  void Start(TimePoint now);
  void SetFunctionGroupState(std::string const &functionGroup, std::string const &state, TimePoint now);
  void ReportCheckpoint(std::string const &entity, CheckpointType checkpoint, TimePoint now);
  void SetEntityEnabled(std::string const &entity, bool enabled, TimePoint now);
  void Evaluate(TimePoint now);

  // ---- runtime (wall clock; CheckpointSink) ---------------------------------
  bool IsKnownEntity(std::string const &entity) const override;
  void ReportCheckpoint(std::string const &entity, CheckpointType checkpoint) override;
  void SetEntityEnabled(std::string const &entity, bool enabled) override;
  /// Starts a thread calling Evaluate(now) every `period`.
  void StartBackgroundEvaluation(std::chrono::milliseconds period);
  void StopBackgroundEvaluation();

  // ---- observation -----------------------------------------------------------
  ara::core::Result<ElementarySupervisionStatus> GetElementaryStatus(std::string const &name) const;
  ara::core::Result<GlobalSupervisionStatus> GetGlobalStatus(std::string const &name) const;
  bool WatchdogReactionFired() const;

private:
  struct Global {
    GlobalSupervisionConfig cfg;
    GlobalSupervisionStatus status{GlobalSupervisionStatus::kDEACTIVATED};
    TimePoint expiredSince{};
    bool recoveryDispatched{false};
    std::optional<ara::core::Future<void>> pendingRecovery;
  };

  void EvaluateLocked(TimePoint now, std::vector<std::function<void()>> &deferred);
  void ActivateGlobal(Global &g, TimePoint now);
  void DeactivateGlobal(Global &g);
  void Escalate(std::vector<std::function<void()>> &deferred);
  static std::set<std::string> EntitiesOf(ElementarySupervision const &s);
  bool Suspended(ElementarySupervision const &s) const;

  mutable std::mutex mMutex;
  std::map<std::string, std::shared_ptr<ElementarySupervision>> mSupervisions;
  std::map<CheckpointRef, std::vector<ElementarySupervision *>> mIndex;
  std::map<std::string, Global> mGlobals;
  std::map<std::string, RecoveryHandler> mRecovery;
  std::set<std::string> mDisabledEntities;
  std::set<std::string> mKnownEntities;
  std::shared_ptr<apext::phm::WatchdogInterface> mWatchdog;
  StatusListener mListener;
  ProductionErrorReporter mErrorReporter;
  bool mWatchdogFired{false};

  std::thread mThread;
  std::mutex mThreadMutex;
  std::condition_variable mThreadCv;
  bool mThreadStop{false};
};

} // namespace ara::phm

#endif // ARA_PHM_HEALTH_MONITOR_H_
