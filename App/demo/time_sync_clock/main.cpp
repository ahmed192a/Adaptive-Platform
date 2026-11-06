// time_sync_clock - a time-base provider (the "master") and a consumer (an application) sharing one
// synchronized time base. A simulated local clock makes the run instant and reproducible. Shows
// slew correction (small offset), step correction (large offset, leap jump), rate correction and
// the timeout when the provider falls silent.
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>

#include "ara/tsync/synchronized_time_base.h"

using namespace ara::tsync;
using namespace std::chrono;

namespace {
steady_clock::time_point gNow = steady_clock::time_point{} + hours(1);

char const *Name(SynchronizationStatus s) {
  switch (s) {
    case SynchronizationStatus::kNotSynchronizedUntilStartup: return "NotSynchronized";
    case SynchronizationStatus::kTimeOut: return "TimeOut";
    case SynchronizationStatus::kSynchronized: return "Synchronized";
    case SynchronizationStatus::kSynchToGateway: return "SynchToGateway";
  }
  return "?";
}
char const *Name(LeapJump j) {
  return j == LeapJump::kTimeLeapNone ? "none" : j == LeapJump::kTimeLeapFuture ? "future" : "past";
}

long long Ms(Timestamp t) { return duration_cast<milliseconds>(t.time_since_epoch()).count(); }

SynchronizedTimeBaseConsumer *gConsumer = nullptr;

void Show(char const *what) {
  auto s = gConsumer->GetTimeWithStatus();
  std::printf("local+%6lld ms | %-34s | time=%8lld ms  %-15s leap=%-6s rate=%s%s\n",
              static_cast<long long>(duration_cast<milliseconds>(gNow - (steady_clock::time_point{} + hours(1))).count()),
              what, Ms(s.GetCreationTime()), Name(s.GetSynchronizationStatus()), Name(s.GetLeapJump()),
              s.GetRateCorrected() ? "corrected" : "-", s.GetRateExceeded() ? " EXCEEDED" : "");
}
void Advance(milliseconds d) { gNow += d; }
}  // namespace

int main() {
  std::puts("== time sync clock: provider + consumer on time base Vehicle/DemoTime ==");
  TimeBaseConfig cfg;
  cfg.name = "Vehicle/DemoTime";
  cfg.clock = [] { return gNow; };
  cfg.timeout = seconds(1);
  cfg.stepThreshold = milliseconds(50);   // offsets above this are stepped, below are slewed
  cfg.slewWindow = milliseconds(400);
  cfg.allowProviderRateCorrection = true;
  cfg.providerRateDeviationMax = 1e-3;
  cfg.rateCorrectionThreshold = 1e-4;
  auto r = TimeBaseResource::Create(cfg);
  if (!r.HasValue()) return 1;
  auto tb = r.Value();
  TimeBaseRegistry::Bind("Vehicle/DemoTime", tb);

  SynchronizedTimeBaseConsumer consumer(ara::core::InstanceSpecifier{"Vehicle/DemoTime"});
  gConsumer = &consumer;
  TimeBaseProvider provider(tb);
  (void)consumer.RegisterSynchronizationStateChangeNotifier(
      [](SynchronizationStatus s) { std::printf("      >> notifier: state is now %s\n", Name(s)); });
  (void)consumer.RegisterTimeLeapNotifier(
      [](LeapJump j) { std::printf("      >> notifier: time leap %s\n", Name(j)); });

  Show("before the first update");
  std::puts("\n-- 1. master delivers the first time (5000 ms)");
  (void)provider.Update(Timestamp(duration_cast<Duration>(milliseconds(5000))));
  Show("first update");
  Advance(milliseconds(200));
  Show("200 ms later (extrapolated)");

  std::puts("\n-- 2. small offset (+20 ms): slewed smoothly, no leap");
  (void)provider.Update(Timestamp(duration_cast<Duration>(milliseconds(5220))));
  for (int i = 0; i < 3; ++i) {
    Advance(milliseconds(100));
    Show("slewing toward the master");
  }

  std::puts("\n-- 3. large offset (+2 s): stepped immediately, leap jump reported");
  (void)provider.Update(Timestamp(duration_cast<Duration>(milliseconds(7520))));
  Show("after the step");

  std::puts("\n-- 4. master reports its crystal runs 200 ppm fast (rate correction)");
  (void)provider.Update(Timestamp(duration_cast<Duration>(milliseconds(7520))), 2e-4);
  Show("rate corrected, above threshold");
  (void)provider.Update(Timestamp(duration_cast<Duration>(milliseconds(7520))), 5e-5);
  Show("rate back within threshold");

  std::puts("\n-- 5. master goes silent: the time base times out after 1 s");
  Advance(milliseconds(600));
  Show("600 ms without update");
  Advance(milliseconds(600));
  Show("1200 ms without update");

  std::puts("\n-- 6. master recovers");
  (void)provider.Update(Timestamp(duration_cast<Duration>(milliseconds(9000))));
  Show("synchronized again");

  TimeBaseRegistry::Unbind("Vehicle/DemoTime");
  return 0;
}
