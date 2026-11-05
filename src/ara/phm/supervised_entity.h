/**
 * @file supervised_entity.h
 * @brief ara::phm::SupervisedEntity (SWS_PHM) - checkpoint reporting API for applications.
 *
 * @code
 *   auto entity = ara::phm::SupervisedEntity::Create(ara::core::InstanceSpecifier("App/Planner")).Value();
 *   entity.ReportCheckpoint(1);          // call from the supervised code path
 * @endcode
 */
#ifndef ARA_PHM_SUPERVISED_ENTITY_H_
#define ARA_PHM_SUPERVISED_ENTITY_H_

#include <memory>
#include <string>

#include "ara/core/instance_specifier.h"
#include "ara/core/result.h"
#include "ara/phm/health_monitor.h"

namespace ara::phm {

/// Replaces the transport used by all SupervisedEntity objects created afterwards
/// (default: the in-process HealthMonitor::Instance()).  Pass nullptr to reset.
void SetCheckpointSink(std::shared_ptr<CheckpointSink> sink);

class SupervisedEntity final {
public:
  /// Validating factory; kUnknownEntity if the entity is not configured in PHM.
  static ara::core::Result<SupervisedEntity> Create(ara::core::InstanceSpecifier const &instance);
  /// Throws PhmException (or terminates) like Create() would fail.
  explicit SupervisedEntity(ara::core::InstanceSpecifier const &instance);

  SupervisedEntity(SupervisedEntity const &) = delete;
  SupervisedEntity &operator=(SupervisedEntity const &) = delete;
  SupervisedEntity(SupervisedEntity &&) noexcept = default;
  SupervisedEntity &operator=(SupervisedEntity &&) noexcept = default;
  ~SupervisedEntity() = default;

  /// Reports that checkpoint `checkpointId` was reached.  Non-blocking.
  void ReportCheckpoint(CheckpointType checkpointId) noexcept;
  /// Turns supervision of this entity off: checkpoints are ignored.
  void Disable() noexcept;
  /// Turns supervision on again and restarts alive reference cycles.
  void Enable() noexcept;

private:
  SupervisedEntity(std::string name, std::shared_ptr<CheckpointSink> sink)
      : mName(std::move(name)), mSink(std::move(sink)) {}
  std::string mName;
  std::shared_ptr<CheckpointSink> mSink;
};

} // namespace ara::phm

#endif // ARA_PHM_SUPERVISED_ENTITY_H_
