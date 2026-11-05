#ifndef TESTING_PHM_UTIL_H_
#define TESTING_PHM_UTIL_H_

#include <gtest/gtest.h>

#include <atomic>

#include "ara/phm/health_monitor.h"
#include "ara/phm/supervision.h"

namespace phmtest {
using namespace ara::phm;

inline TimePoint T(std::int64_t ms) { return TimePoint(std::chrono::milliseconds(ms)); }
inline Duration Ms(std::int64_t ms) { return std::chrono::duration_cast<Duration>(std::chrono::milliseconds(ms)); }

struct FakeWatchdog : apext::phm::WatchdogInterface {
  std::atomic<int> alive{0};
  std::atomic<int> fired{0};
  void AliveNotification() override { ++alive; }
  void FireWatchdogReaction() override { ++fired; }
};

inline CheckpointRef CP(char const *entity, CheckpointType id) { return CheckpointRef{entity, id}; }

inline std::shared_ptr<AliveSupervision> MakeAlive(char const *name = "Alive", std::uint32_t expected = 2,
                                                   std::uint32_t minM = 0, std::uint32_t maxM = 0,
                                                   std::uint32_t tolerance = 0, std::int64_t cycleMs = 100) {
  AliveSupervision::Config c;
  c.checkpoint = CP("E", 1);
  c.expectedAliveIndications = expected;
  c.minMargin = minM;
  c.maxMargin = maxM;
  c.referenceCycle = Ms(cycleMs);
  c.failedReferenceCyclesTolerance = tolerance;
  return std::make_shared<AliveSupervision>(name, c);
}
} // namespace phmtest

#endif
