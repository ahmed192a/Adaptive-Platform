# ara::tsync – Time Synchronization

`ara::tsync` offers a **synchronized time base** to applications. A provider feeds in
times from a master (gPTP, NTP, GNSS, …); consumers read a continuous time derived from the local
steady clock.

```
T(now) = T_sync + (now - local_at_sync) * (1 + rateDeviation) + slewed offset
```

| Type | Role |
|---|---|
| `TimeBaseResource` | consumer view: `GetCurrentTime()`, `GetRateDeviation()`, `GetStatus()`, `GetUserData()`, `SetUpdateCallback()`, `Poll()` |
| `TimeBaseProvider` | provider: `Update(time, rateDeviation, syncToGateway, userData)` |
| `TimeBaseConfig` | name, injectable `clock`, `timeout`, `stepThreshold`, `slewWindow` |
| `StatusFlag` | `kSynchronized`, `kTimeout`, `kSyncToGateway`, `kTimeLeap`, `kPendingRateCorr` |

Behaviour:

* **First update** synchronizes (no leap flag). Before that `GetCurrentTime()` returns `kNotSynchronized`.
* **Offset correction**: an update that differs from the current time base by at most `stepThreshold`
  is slewed in linearly over `slewWindow` (time stays continuous, `kPendingRateCorr` is set).
  A larger difference *steps* the time, sets `kTimeLeap` and notifies with `ChangeReason::kTimeStep`.
* **Timeout**: no update within `timeout` → `kTimeout` instead of `kSynchronized`; the extrapolated time stays readable.
  The next update clears it.
* **Callback**: delivered on status changes and time steps (and on every update if requested). Timeouts
  have no event of their own, so call `Poll()` periodically to get the notification.

```cpp
auto tb = ara::tsync::TimeBaseResource::Create({.name = "GlobalTime"}).Value();
ara::tsync::TimeBaseProvider provider(tb);
provider.Update(masterTime, /*rateDeviation=*/2e-6);
auto now = tb->GetCurrentTime();           // Result<TimeBaseSnapshot>
```

Tests: `ctest --preset debug -L tsync` (14 tests, deterministic via the injected clock).

> **Spec conformance:** see [spec-conformance/tsync.md](spec-conformance/tsync.md). The SWS API is `SynchronizedTimeBaseConsumer/Provider/Status` (`synchronized_time_base.h`), bound with `TimeBaseRegistry::Bind`.
