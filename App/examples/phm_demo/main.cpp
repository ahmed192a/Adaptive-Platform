/**
 * phm_demo - Platform Health Management end to end.
 *
 * A worker thread reports checkpoint 1 every 20 ms (alive supervision expects
 * 2..3 indications per 60 ms).  After ~400 ms the worker "hangs".  PHM then
 * - marks the supervision FAILED and finally EXPIRED,
 * - calls the RecoveryAction (here: it declines -> kSMCanNotHandleRecovery),
 * - fires the (simulated) watchdog.
 */
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "ara/log/logging.h"
#include "ara/phm/recovery_action.h"
#include "ara/phm/supervised_entity.h"

using namespace std::chrono_literals;

namespace {
constexpr char const *kConfig = R"({
  "supervisions": [
    {"name":"WorkerAlive","type":"alive","entity":"Demo/Worker","checkpoint":1,
     "expectedAliveIndications":3,"minMargin":1,"maxMargin":0,
     "referenceCycleMs":60,"failedCyclesTolerance":2}
  ],
  "globalSupervisions": [
    {"name":"WorkerHealth","members":["WorkerAlive"],"recoveryAction":"Demo/Recovery","executionError":1}
  ]
})";

struct ConsoleWatchdog : apext::phm::WatchdogInterface {
  std::atomic<bool> fired{false};
  void AliveNotification() override {}
  void FireWatchdogReaction() override { fired = true; }
};
} // namespace

int main() {
  auto &log = ara::log::CreateLogger("PHMD", "PHM demo");
  auto &monitor = ara::phm::HealthMonitor::Instance();
  auto watchdog = std::make_shared<ConsoleWatchdog>();
  monitor.SetWatchdog(watchdog);

  auto cfg = ara::phm::PhmConfig::FromJson(kConfig);
  if (!cfg.HasValue() || !monitor.Configure(std::move(cfg).Value()).HasValue()) {
    log.LogError() << "invalid PHM configuration";
    return 1;
  }
  monitor.SetStatusListener([&](std::string const &name, ara::phm::GlobalSupervisionStatus from,
                                ara::phm::GlobalSupervisionStatus to) {
    log.LogWarn() << "global supervision " << name << ": " << ara::phm::ToString(from) << " -> "
                  << ara::phm::ToString(to);
  });

  ara::phm::RecoveryAction recovery(
      ara::core::InstanceSpecifier("Demo/Recovery"), [&](ara::phm::ExpiredSupervision const &e) {
        log.LogError() << "recovery requested for " << e.globalSupervision << " (" << e.supervision
                       << ", " << ara::phm::ToString(e.type) << ")  - cannot recover";
        ara::core::Promise<void> p;
        p.SetError(ara::phm::MakeErrorCode(ara::phm::PhmErrc::kSMCanNotHandleRecovery));
        return p.get_future();
      });
  (void)recovery.Offer();

  monitor.Start(ara::core::SteadyClock::now());
  monitor.StartBackgroundEvaluation(10ms);

  std::atomic<bool> hang{false};
  std::thread worker([&] {
    auto entity = ara::phm::SupervisedEntity::Create(ara::core::InstanceSpecifier("Demo/Worker"));
    if (!entity.HasValue()) {
      return;
    }
    while (!watchdog->fired) {
      if (!hang) {
        entity.Value().ReportCheckpoint(1);
      }
      std::this_thread::sleep_for(20ms);
    }
  });

  std::this_thread::sleep_for(400ms);
  log.LogInfo() << "worker is healthy: " << ara::phm::ToString(monitor.GetGlobalStatus("WorkerHealth").Value());
  log.LogInfo() << "simulating a hang of the worker";
  hang = true;

  for (int i = 0; i < 100 && !watchdog->fired; ++i) {
    std::this_thread::sleep_for(20ms);
  }
  monitor.StopBackgroundEvaluation();
  worker.join();
  log.LogFatal() << "watchdog reaction fired: " << (watchdog->fired ? "yes" : "no");
  monitor.Clear();
  return watchdog->fired ? 0 : 2;
}
