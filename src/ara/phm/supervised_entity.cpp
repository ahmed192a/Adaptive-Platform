#include "ara/phm/supervised_entity.h"

#include <mutex>

namespace ara::phm {

namespace {
std::mutex gSinkMutex;
std::shared_ptr<CheckpointSink> gSink;

std::shared_ptr<CheckpointSink> CurrentSink() {
  std::lock_guard<std::mutex> lock(gSinkMutex);
  if (!gSink) {
    // non-owning view of the process wide monitor
    return std::shared_ptr<CheckpointSink>(&HealthMonitor::Instance(), [](CheckpointSink *) {});
  }
  return gSink;
}
} // namespace

void SetCheckpointSink(std::shared_ptr<CheckpointSink> sink) {
  std::lock_guard<std::mutex> lock(gSinkMutex);
  gSink = std::move(sink);
}

ara::core::Result<SupervisedEntity> SupervisedEntity::Create(ara::core::InstanceSpecifier const &instance) {
  auto sink = CurrentSink();
  std::string const name(instance.ToString());
  if (!sink->IsKnownEntity(name)) {
    return ara::core::Result<SupervisedEntity>(MakeErrorCode(PhmErrc::kUnknownEntity));
  }
  return ara::core::Result<SupervisedEntity>(SupervisedEntity(name, std::move(sink)));
}

SupervisedEntity::SupervisedEntity(ara::core::InstanceSpecifier const &instance)
    : mName(instance.ToString()), mSink(CurrentSink()) {
  if (!mSink->IsKnownEntity(mName)) {
    ara::core::ThrowOrTerminate<PhmException>(MakeErrorCode(PhmErrc::kUnknownEntity));
  }
}

void SupervisedEntity::ReportCheckpoint(CheckpointType checkpointId) noexcept {
  if (mSink) {
    mSink->ReportCheckpoint(mName, checkpointId);
  }
}
void SupervisedEntity::Disable() noexcept {
  if (mSink) {
    mSink->SetEntityEnabled(mName, false);
  }
}
void SupervisedEntity::Enable() noexcept {
  if (mSink) {
    mSink->SetEntityEnabled(mName, true);
  }
}

} // namespace ara::phm
