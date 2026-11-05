/**
 * @file supervision.h
 * @brief Elementary supervisions (alive, deadline, logical) of PHM.
 *
 * Pure state machines: they never read a clock themselves, every call gets
 * the current time, which makes them deterministic and unit-testable.
 *
 * Semantics (SWS Platform Health Management, chapter "Supervision"):
 *  - Alive:    every reference cycle the number of indications must be within
 *              [expected - minMargin, expected + maxMargin].  A bad cycle
 *              raises the failure counter (status FAILED); a good one lowers
 *              it.  Counter > failedReferenceCyclesTolerance => EXPIRED.
 *  - Deadline: time between start and end checkpoint must be within
 *              [minDeadline, maxDeadline]; otherwise EXPIRED.
 *  - Logical:  checkpoints must follow the configured transition graph;
 *              a violation => EXPIRED.
 * EXPIRED is final until the supervision is deactivated and activated again.
 */
#ifndef ARA_PHM_SUPERVISION_H_
#define ARA_PHM_SUPERVISION_H_

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ara/phm/phm_types.h"

namespace ara::phm {

/// Reference to a checkpoint of a supervised entity.
struct CheckpointRef {
  std::string entity;
  CheckpointType checkpoint{0};
  bool operator<(CheckpointRef const &o) const {
    return std::tie(entity, checkpoint) < std::tie(o.entity, o.checkpoint);
  }
  bool operator==(CheckpointRef const &o) const {
    return entity == o.entity && checkpoint == o.checkpoint;
  }
};

class ElementarySupervision {
public:
  explicit ElementarySupervision(std::string name) : mName(std::move(name)) {}
  virtual ~ElementarySupervision() = default;

  std::string const &Name() const noexcept { return mName; }
  ElementarySupervisionStatus Status() const noexcept { return mStatus; }
  virtual TypeOfSupervision Type() const noexcept = 0;
  /// All checkpoints that influence this supervision.
  virtual std::set<CheckpointRef> Checkpoints() const = 0;

  /// (Re-)start supervising at `now`; status becomes kOK.
  void Activate(TimePoint now);
  void Deactivate() noexcept;
  bool IsActive() const noexcept { return mStatus != ElementarySupervisionStatus::kDEACTIVATED; }

  /// A checkpoint of one of Checkpoints() was reported at `now`.
  void OnCheckpoint(CheckpointRef const &cp, TimePoint now);
  /// Time progressed to `now` (evaluates cycles / timeouts).
  void Evaluate(TimePoint now);
  /// Restart timing without touching the status (used by SupervisedEntity::Enable()).
  virtual void RestartTiming(TimePoint now) = 0;

protected:
  virtual void DoActivate(TimePoint now) = 0;
  virtual void DoCheckpoint(CheckpointRef const &cp, TimePoint now) = 0;
  virtual void DoEvaluate(TimePoint now) = 0;
  void Expire() noexcept { mStatus = ElementarySupervisionStatus::kEXPIRED; }
  void SetStatus(ElementarySupervisionStatus s) noexcept {
    if (mStatus != ElementarySupervisionStatus::kEXPIRED) {
      mStatus = s;
    }
  }

private:
  std::string mName;
  ElementarySupervisionStatus mStatus{ElementarySupervisionStatus::kDEACTIVATED};
};

// ---------------------------------------------------------------------------
class AliveSupervision final : public ElementarySupervision {
public:
  struct Config {
    CheckpointRef checkpoint;
    std::uint32_t expectedAliveIndications{1};
    std::uint32_t minMargin{0};
    std::uint32_t maxMargin{0};
    Duration referenceCycle{std::chrono::milliseconds(100)};
    std::uint32_t failedReferenceCyclesTolerance{0};
  };
  AliveSupervision(std::string name, Config cfg) : ElementarySupervision(std::move(name)), mCfg(std::move(cfg)) {}
  TypeOfSupervision Type() const noexcept override { return TypeOfSupervision::kAliveSupervision; }
  std::set<CheckpointRef> Checkpoints() const override { return {mCfg.checkpoint}; }
  void RestartTiming(TimePoint now) override;
  std::uint32_t FailedCycles() const noexcept { return mFailedCycles; }

private:
  void DoActivate(TimePoint now) override;
  void DoCheckpoint(CheckpointRef const &cp, TimePoint now) override;
  void DoEvaluate(TimePoint now) override;
  Config mCfg;
  TimePoint mCycleStart{};
  std::uint32_t mIndications{0};
  std::uint32_t mFailedCycles{0};
};

class DeadlineSupervision final : public ElementarySupervision {
public:
  struct Config {
    CheckpointRef start;
    CheckpointRef end;
    Duration minDeadline{Duration::zero()};
    Duration maxDeadline{std::chrono::milliseconds(100)};
  };
  DeadlineSupervision(std::string name, Config cfg) : ElementarySupervision(std::move(name)), mCfg(std::move(cfg)) {}
  TypeOfSupervision Type() const noexcept override { return TypeOfSupervision::kDeadlineSupervision; }
  std::set<CheckpointRef> Checkpoints() const override { return {mCfg.start, mCfg.end}; }
  void RestartTiming(TimePoint) override { mStarted.reset(); }

private:
  void DoActivate(TimePoint) override { mStarted.reset(); }
  void DoCheckpoint(CheckpointRef const &cp, TimePoint now) override;
  void DoEvaluate(TimePoint now) override;
  Config mCfg;
  std::optional<TimePoint> mStarted;
};

class LogicalSupervision final : public ElementarySupervision {
public:
  struct Config {
    std::set<CheckpointType> checkpoints;
    std::string entity;
    std::set<CheckpointType> initial;
    std::set<CheckpointType> final;
    std::set<std::pair<CheckpointType, CheckpointType>> transitions;
  };
  LogicalSupervision(std::string name, Config cfg) : ElementarySupervision(std::move(name)), mCfg(std::move(cfg)) {}
  TypeOfSupervision Type() const noexcept override { return TypeOfSupervision::kLogicalSupervision; }
  std::set<CheckpointRef> Checkpoints() const override;
  void RestartTiming(TimePoint) override {}

private:
  void DoActivate(TimePoint) override { mLast.reset(); }
  void DoCheckpoint(CheckpointRef const &cp, TimePoint now) override;
  void DoEvaluate(TimePoint) override {}
  Config mCfg;
  std::optional<CheckpointType> mLast; ///< empty = at the beginning of a sequence
};

} // namespace ara::phm

#endif // ARA_PHM_SUPERVISION_H_
