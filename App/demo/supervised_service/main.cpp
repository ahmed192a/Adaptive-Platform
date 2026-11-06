// supervised_service - a worker supervised by ara::phm, with a recovery action and a graceful SIGTERM exit.
//
//   supervised_service --scenario     scripted run: healthy -> hang -> EXPIRED -> recovery -> healthy (exit 0 = ok)
//   supervised_service                interactive: type  hang | heal | status | quit   (SIGTERM also quits)
//
// Shows: alive supervision + global supervision + recovery action (ara::phm), the PHM_E_WATCHDOG_RESET
// production error hook, ExecutionClient::Create with a termination handler (ara::exec).
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "ara/core/instance_specifier.h"
#include "ara/core/promise.h"
#include "ara/core/steady_clock.h"
#include "ara/exec/execution_client.h"
#include "ara/phm/health_monitor.h"
#include "ara/phm/phm_config.h"
#include "ara/phm/recovery_action.h"
#include "ara/phm/supervised_entity.h"

using namespace std::chrono_literals;

namespace {
std::mutex gOut;
template <typename... Args> void Say(Args &&...args) {
  std::lock_guard<std::mutex> lk(gOut);
  (std::cout << ... << args) << std::endl;
}

struct DemoWatchdog : apext::phm::WatchdogInterface {
  void AliveNotification() override {}
  void FireWatchdogReaction() override { Say("[watchdog] reaction fired - the ECU would reset now"); }
};

std::atomic<bool> gHung{false};
std::atomic<bool> gStop{false};
std::atomic<int> gRestarts{0};
std::mutex gMutex;
std::string gStatus = "DEACTIVATED";
bool gSawExpired = false;

void Worker() {
  auto entity = ara::phm::SupervisedEntity::Create(ara::core::InstanceSpecifier("Svc/Worker"));
  if (!entity.HasValue()) {
    Say("cannot create supervised entity: ", entity.Error().Message());
    return;
  }
  while (!gStop) {
    if (!gHung) entity.Value().ReportCheckpoint(1); // "I am alive"
    std::this_thread::sleep_for(50ms);
  }
}

bool WaitFor(std::function<bool()> pred, std::chrono::milliseconds limit) {
  auto const end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end) {
    if (pred()) return true;
    std::this_thread::sleep_for(20ms);
  }
  return pred();
}

// Activation by SetFunctionGroupState does not raise a status event, so the status is polled.
bool GlobalIsOk() {
  auto s = ara::phm::HealthMonitor::Instance().GetGlobalStatus("ServiceHealth");
  return s.HasValue() && s.Value() == ara::phm::GlobalSupervisionStatus::kOK;
}
bool SawExpired() {
  std::lock_guard<std::mutex> lk(gMutex);
  return gSawExpired;
}
} // namespace

int main(int argc, char **argv) {
  bool const scenario = argc > 1 && std::string(argv[1]) == "--scenario";

  // --- ara::exec: report state, shut down gracefully on SIGTERM -----------------------------------
  auto exec = ara::exec::ExecutionClient::Create([] {
    Say("[exec] SIGTERM received - shutting down");
    gStop = true;
    });
  if (exec.HasValue()) (void)exec.Value().ReportExecutionStateResult(ara::exec::ExecutionState::kRunning);

  // --- ara::phm configuration ---------------------------------------------------------------------
  auto &mon = ara::phm::HealthMonitor::Instance();
  mon.Clear();
  auto cfg = ara::phm::PhmConfig::FromJson(R"({
    "supervisions": [{"name": "ServiceAlive", "type": "alive", "entity": "Svc/Worker", "checkpoint": 1,
                      "expectedAliveIndications": 4, "minMargin": 2, "maxMargin": 2,
                      "referenceCycleMs": 200, "failedCyclesTolerance": 1}],
    "globalSupervisions": [{"name": "ServiceHealth", "members": ["ServiceAlive"], "functionGroup": "FG_Service",
                            "activeStates": ["on"], "recoveryAction": "Svc/Recovery", "executionError": 1}]
  })");
  if (!cfg.HasValue() || !mon.Configure(std::move(cfg).Value()).HasValue()) {
    Say("PHM configuration failed");
    return 2;
  }
  mon.SetWatchdog(std::make_shared<DemoWatchdog>());
  mon.SetProductionErrorReporter([](std::string const &name, bool failed) {
    Say("[diag] production error ", name, failed ? " FAILED" : " PASSED");
  });
  mon.SetStatusListener([](std::string const &g, ara::phm::GlobalSupervisionStatus from,
                           ara::phm::GlobalSupervisionStatus to) {
    Say("[phm] ", g, ": ", ara::phm::ToString(from), " -> ", ara::phm::ToString(to));
    std::lock_guard<std::mutex> lk(gMutex);
    gStatus = ara::phm::ToString(to);
    if (to == ara::phm::GlobalSupervisionStatus::kEXPIRED) gSawExpired = true;
  });

  // Recovery action: "State Management" restarts the function group, here by un-hanging the worker.
  std::thread restarter;
  ara::phm::RecoveryAction recovery(
      ara::core::InstanceSpecifier("Svc/Recovery"), [&restarter](ara::phm::ExpiredSupervision const &e) {
        Say("[recovery] ", e.supervision, " expired in ", e.functionGroup, " (error ", e.executionError,
            ") - restarting");
        if (restarter.joinable()) restarter.join();
        restarter = std::thread([fg = e.functionGroup] {
          std::this_thread::sleep_for(300ms);
          gHung = false;
          ++gRestarts;
          auto &m = ara::phm::HealthMonitor::Instance();
          m.SetFunctionGroupState(fg, "off", ara::core::SteadyClock::now());
          std::this_thread::sleep_for(50ms);
          m.SetFunctionGroupState(fg, "on", ara::core::SteadyClock::now());
        });
        ara::core::Promise<void> p;
        p.set_value();
        return p.get_future();
      });
  if (!recovery.Offer().HasValue()) {
    Say("cannot offer recovery action");
    return 2;
  }

  auto const now = ara::core::SteadyClock::now();
  mon.Start(now);
  mon.SetFunctionGroupState("FG_Service", "on", now);
  mon.StartBackgroundEvaluation(20ms);
  std::thread worker(Worker);
  Say("supervised_service running (", scenario ? "scenario" : "interactive: hang | heal | status | quit", ")");

  int rc = 0;
  if (scenario) {
    bool ok = WaitFor(GlobalIsOk, 3s);
    Say(ok ? "step 1/3 healthy" : "step 1/3 FAILED: never became OK");
    if (ok) {
      Say("step 2/3 hanging the worker");
      gHung = true;
      ok = WaitFor(SawExpired, 5s);
      Say(ok ? "         supervision expired" : "         FAILED: no expiry");
    }
    if (ok) {
      ok = WaitFor([] { return gRestarts.load() >= 1 && GlobalIsOk(); }, 8s);
      Say(ok ? "step 3/3 recovered after restart" : "step 3/3 FAILED: no recovery");
    }
    rc = ok && gRestarts == 1 ? 0 : 1;
    Say(rc == 0 ? "scenario OK" : "scenario FAILED");
  } else {
    std::string line;
    while (!gStop && std::getline(std::cin, line)) {
      if (line == "hang") {
        gHung = true;
        Say("worker hung");
      } else if (line == "heal") {
        gHung = false;
        Say("worker released");
      } else if (line == "status") {
        std::lock_guard<std::mutex> lk(gMutex);
        Say("status ", gStatus, ", restarts ", gRestarts.load());
      } else if (line == "quit") {
        break;
      }
    }
  }

  gStop = true;
  worker.join();
  mon.StopBackgroundEvaluation();
  if (restarter.joinable()) restarter.join();
  recovery.StopOffer();
  if (exec.HasValue()) (void)exec.Value().ReportExecutionStateResult(ara::exec::ExecutionState::kTerminating);
  return rc;
}
