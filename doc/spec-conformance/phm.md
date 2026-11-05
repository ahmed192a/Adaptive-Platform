# ara::phm — conformance with AUTOSAR AP R25-11 (SWS Platform Health Management)

Source: [AUTOSAR_AP_SWS_PlatformHealthManagement.pdf (R25-11)](https://www.autosar.org/fileadmin/standards/R25-11/AP/AUTOSAR_AP_SWS_PlatformHealthManagement.pdf).

**Method and limits.** The requirement ids below are the ones that could be read from the R25-11 text
(chapters 5 and 7: functional behaviour). Chapter 8 (the formal C++ API tables: exact signatures, `PhmErrc`
numeric values, `TypeOfSupervision` values) was **not retrievable** in this audit, so API-shape rows are
marked *Unverified* instead of being guessed. Ids not listed here were not read and are not claimed.

Status legend: **Implemented**, **Partial**, **Missing**, **Deviation** (known, intentional or forced),
**Unverified** (could not be checked against the text).

## Behaviour

| Requirement | Topic | Status | Notes / test |
|---|---|---|---|
| SWS_PHM_00216 | Global supervision statuses kOK, kDEACTIVATED, kFAILED, kEXPIRED, kSTOPPED | Implemented | `GlobalSupervisionStatus`; `SpecFixture.StoppedOnlyForPlatformCriticalSupervisions` |
| SWS_PHM_00218 | Global status starts kDEACTIVATED | Implemented | `StatusesStartDeactivatedUntilActivated` |
| SWS_PHM_01343 | Elementary statuses kOK, kDEACTIVATED, kEXPIRED, kFAILED (kFAILED alive only) | Implemented | `ElementarySupervisionStatus`; only `AliveSupervision` produces kFAILED |
| SWS_PHM_01344 | Elementary status starts kDEACTIVATED | Implemented | `StatusesStartDeactivatedUntilActivated`, `AliveSupervision.StartsDeactivatedAndIgnoresInput` |
| SWS_PHM_01346, SWS_PHM_01347 | Alive: kFAILED within `failedReferenceCyclesTolerance`, kEXPIRED beyond; deadline/logical go straight to kEXPIRED | Implemented | `SpecSupervision.FailedThenExpiredForAliveDirectExpiredForOthers`. Good cycles decrement the failed-cycle counter (debounce); the exact debounce rule in the SWS text beyond the above was not verified |
| SWS_PHM_01357 | Logical supervision: first report that is not a valid start expires | Implemented | same test; `LogicalSupervision.WrongStartTransitionAndSkippedStepExpire` |
| SWS_PHM_00105 | kSTOPPED only for State Management / Execution Management / OS supervisions and triggers the watchdog reaction | Implemented | `GlobalSupervisionConfig::platformCritical` + `expiredToleranceMs`; `HealthMonitorTest.PlatformCriticalSupervisionStopsAfterTolerance`. Which supervisions are platform-critical is configuration, not hard-wired |
| SWS_PHM_00101 | `RecoveryHandler` receives the execution error (function group + application error) and the `TypeOfSupervision` | Deviation | Delivered as one struct `ExpiredSupervision{globalSupervision, supervision, functionGroup, type, executionError}` instead of the spec parameters; `ara::exec::ExecutionErrorEvent` is not used because PHM must not depend on a manifest-backed `FunctionGroup` |
| SWS_PHM_00107 | `kSMCanNotHandleRecovery` => stop alive notifications and fire watchdog reaction | Implemented | `UnhandledRecoveryStopsAliveNotificationsAndFiresWatchdog`; no handler offered also escalates (`escalateToWatchdog`) |
| SWS_PHM_01147, SWS_PHM_01148 | `RecoveryAction::Offer` / `StopOffer` enable / disable handler calls | Implemented | `RecoveryActionOfferRules`; `Offer()` returns `Result<void>`, `kAlreadyOffered` on a second offer (implementation specific) |
| SWS_PHM_01361 | Default execution error 1 when none is configured | **Fixed in this audit** | was 0; struct default and JSON default are now 1 (`DefaultExecutionErrorIsOne`, `MissingExecutionErrorParsesAsOne`) |
| SWS_PHM_01363 | Production error `PHM_E_WATCHDOG_RESET` reported FAILED when the watchdog reaction fires | Missing | no `ara::diag` event reporting wired into PHM; the reaction is observable via `WatchdogInterface::FireWatchdogReaction` and `WatchdogReactionFired()`. Candidate once `ara::diag` has DTC/event reporting |
| SWS_PHM_01364, 01365, 01366 | `Disable()` turns supervision off, checkpoints ignored while disabled, `Enable()` restarts the alive reference cycle | Implemented | `DisabledEntityIgnoresCheckpointsAndEnableRestartsCycle`, `HealthMonitorTest.DisableIgnoresCheckpointsAndSuspendsEvaluationEnableRestarts` |
| SWS_PHM_01341 | Reports for checkpoints/entities with NoSupervision in the active mode are ignored | Partial | checkpoints not part of an *active* supervision are ignored; there is no explicit NoSupervision mode element in `PhmConfig` |
| SWS_PHM_01229 | Reports from a process other than the configured one are ignored | Deviation | PHM is process-local (in-process `HealthMonitor` or a `CheckpointSink`); there is no process identity check. A multi-process transport would need to add it |

## API shape

| Item | Status | Notes |
|---|---|---|
| `SupervisedEntity` (`Create`/constructor, `ReportCheckpoint`, `Enable`, `Disable`) | Partial / Unverified | all four behaviours exist; constructor throws or `Create()` returns `Result`; exact spec signature (`ReportCheckpoint` return type, noexcept) unverified |
| `RecoveryAction` (constructor with handler, `Offer`, `StopOffer`) | Partial / Unverified | handler returns `ara::core::Future<void>` (matches the R24-11 change note "RecoveryHandler returns Future"); handler parameter list differs (see SWS_PHM_00101) |
| `TypeOfSupervision` | Partial / Unverified | enumerators `kAliveSupervision`, `kDeadlineSupervision`, `kLogicalSupervision`; numeric values and any extra enumerators unverified |
| `PhmErrc` / `PhmErrorDomain` | Partial / Unverified | `kSMCanNotHandleRecovery = 1` plus implementation-specific codes 2–5; domain id `0x8000000000000407` is an implementation choice (documented in the header); numeric values and `ThrowAsException` shape unverified |
| `HealthChannel`, `HealthStatus` | N/A | removed from the standard in R24-11 (change history: "Removed Health Channels"); intentionally not implemented |
| Watchdog interface | Deviation (extension) | `apext::phm::WatchdogInterface` (`AliveNotification`, `FireWatchdogReaction`) is a platform extension; R25-11 describes watchdog behaviour but not a C++ interface in the part read |

## Still open

1. `PHM_E_WATCHDOG_RESET` production error (SWS_PHM_01363).
2. Verify chapter 8 signatures and `PhmErrc`/`TypeOfSupervision` numeric values once the formal API tables can be read.
3. Process-identity check for incoming checkpoints (SWS_PHM_01229) when a cross-process transport exists.
4. Explicit NoSupervision mode in the configuration model (SWS_PHM_01341).
