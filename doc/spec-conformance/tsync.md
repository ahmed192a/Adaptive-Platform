# ara::tsync - conformance to AUTOSAR AP R25-11 (SWS_TimeSynchronization)

Source: *AUTOSAR_AP_SWS_TimeSynchronization.pdf*, R25-11 (prefix `SWS_TS_`).
**Important limitation:** the retrievable R25-11 text contains the functional specification and
the class/method *names*, but not the section 8 API tables. Signatures, callback types and numeric
error values below come from the R23-11 text where it was readable (marked R23-11) and are
otherwise **Unverified**. Nothing is invented.
Status values: Implemented, Partial, Missing, Deviation, Unverified.

## Layering

* `TimeBaseResource` / `TimeBaseProvider` (`time_base.h`) - the time base engine (clock extrapolation,
  slew/step, timeout). Not part of the SWS.
* `SynchronizedTimeBaseConsumer`, `SynchronizedTimeBaseProvider`, `SynchronizedTimeBaseStatus`
  (`synchronized_time_base.h`) - the SWS API. Bound to a time base by `InstanceSpecifier` through
  `TimeBaseRegistry::Bind`. Unbound -> `kDaemonConnectionLost` / neutral values.

## Types

| Requirement / API | Status | Notes |
|---|---|---|
| `SynchronizationStatus` (SWS_TS_01050, R23-11): `kNotSynchronizedUntilStartup=0, kTimeOut=1, kSynchronized=2, kSynchToGateway=3` | Implemented | **Fixed.** The engine used bit-mask flags (`SYNCHRONIZED=1, TIMEOUT=2, SYNC_TO_GATEWAY=4`); they remain internal (`StatusFlags`) and are mapped by `ToSynchronizationStatus()`. |
| `LeapJump` (SWS_TS_01051, R23-11): `kTimeLeapNone=0, kTimeLeapFuture=1, kTimeLeapPast=2` | Implemented | **Fixed.** Previously only a TIME_LEAP flag without direction. |
| Initial status `kNotSynchronizedUntilStartup` (SWS_TS_00032), initial leap none (00064) | Implemented | |
| `Timestamp` (8.9) | Deviation / Unverified | `std::chrono` time point (ns). The SWS struct layout is not retrievable. |
| `TimeBase` struct (8.9), `TimePrecisionMeasurement` struct | **Missing** | Fields listed in the SWS (glbSeconds, rateDeviation, pathDelay, ...) not implemented. |
| `UserData` | Deviation | Max 3 bytes, passed as pointer+length / returned as `std::vector<uint8_t>` (the SWS uses `ara::core::Span<const Byte>`, which does not compile under `-Werror` here). `kMaxUserDataSize` value Unverified. |

## SynchronizedTimeBaseStatus (SWS 8.6.2)

| Member | Status | Notes |
|---|---|---|
| `GetSynchronizationStatus` | Implemented | |
| `GetLeapJump` | Implemented | |
| `GetUserData` | Implemented | Empty when none (SWS_TS_00120). |
| `GetCreationTime`, `GetCreationLocalTime` | Implemented | Return types Unverified. |
| `GetRateCorrected`, `GetRateExceeded` | **Missing** | Semantics not retrievable. |

## SynchronizedTimeBaseConsumer (SWS 8.4.2)

| Member | Status | Notes |
|---|---|---|
| ctor(`InstanceSpecifier`), move-only | Implemented | Signature Unverified. |
| `GetTimeWithStatus` (SWS_TS_14218, 00127) | Implemented | Returns `SynchronizedTimeBaseStatus` (per Figure 5.1 / 00127; 14218 text says "GetSynchronizationStatus object" - a spec inconsistency). |
| `GetRateDeviation` (SWS_TS_00202, 00053, 00070) | Implemented | `0.0` if none calculated (00070). |
| `RegisterStatusChangeNotifier` (00701) | Implemented | Callback and return type Unverified (here `Result<void>`, `kDaemonConnectionLost` if unbound). |
| `RegisterSynchronizationStateChangeNotifier` (00702) | Implemented | Same; callback gets the new `SynchronizationStatus`. |
| `RegisterTimeLeapNotifier` (00703) | Implemented | Callback gets the `LeapJump`. |
| `Unregister*Notifier` x3 | Implemented | |
| `RegisterTimePrecisionMeasurementNotifier` (00800) and its Unregister | **Missing** | Needs `TimePrecisionMeasurement`. |
| `RegisterTimeValidationNotification` / validation notification classes (00424-00427) | **Missing** | |

## SynchronizedTimeBaseProvider (SWS 8.5.1, SWS_TS_01100-01115, R23-11)

| Member | Status | Notes |
|---|---|---|
| ctor(`InstanceSpecifier`), move-only, dtor | Implemented | |
| `SetTime(Timestamp, userData)` | Implemented | Steps the time unconditionally. `Span` parameter replaced by pointer+length. |
| `UpdateTime(Timestamp, userData)` | Implemented | Slews small offsets, steps large ones (`stepThreshold`). |
| `GetCurrentTime()` | Implemented | |
| `SetRateCorrection(double)` (00061-00063, 14225) | Implemented | `kFunctionNotSupported` unless `TimeBaseConfig::allowProviderRateCorrection` (00061); `kLimitsExceeded` beyond `providerRateDeviationMax` (00063; the spec also calls it `MasterRateDeviationMax` in 00061 - inconsistency). The time base runs at `1+correction`. |
| `GetRateDeviation()` | Implemented | |
| `SetUserData`, `GetUserData` | Implemented | |
| `OffsetTimeBaseProvider` (`SetOffsetTime`, SWS_TS_012xx) | **Missing** | Offset time bases are not modelled. |
| `RegisterTimeValidationNotification` / `Unregister...` | **Missing** | |

## Error domain

| Item | Status | Notes |
|---|---|---|
| `TsyncErrorDomain`, `GetTsyncErrorDomain`/`MakeErrorCode` (8.10) | Partial | Implemented as `TsyncErrorDomain`, `GetTsyncDomain()`, `MakeErrorCode`. The function name differs from the SWS (`GetTsyncErrorDomain` vs `GetTsyncDomain`) - Deviation, kept for consistency with the other clusters. |
| Codes `kDaemonConnectionLost`, `kFunctionNotSupported`, `kLimitsExceeded` | Implemented | Names from the SWS; **numeric values Unverified** (5, 6, 7 here). |
| Codes `kNotSynchronized`, `kInvalidArgument`, `kAlreadyExists`, `kNotFound` | Deviation | Implementation extensions of the engine API. |
| Domain id | **Unverified -> resolved as distinct** | The old id `0x8000000000000501` was identical to ara::nm's. tsync now uses `0x8000000000000502`; the normative id cannot be obtained from the retrievable text (SWS_CORE_90023 not available). |

## Functional behaviour

| Requirement | Status | Notes |
|---|---|---|
| Timeout detection -> `kTimeOut` | Implemented | |
| Gateway synchronization -> `kSynchToGateway` | Implemented | |
| Time leap detection and direction (SWS_TS_00027) | Implemented | Leap if an update differs by more than `stepThreshold`. |
| Time base as a continuous synchronized time with rate deviation | Implemented | |
| Security events (7.4.1), diagnostics, StbM-to-AP mapping (Table 7.1) | **Missing** | |
| `GetCurrentTime` on the consumer (Table 7.1 maps AP `GetTimeWithStatus` to `StbM_GetCurrentTime`) | n/a | Consumer exposes `GetTimeWithStatus` only, as in the SWS. |
