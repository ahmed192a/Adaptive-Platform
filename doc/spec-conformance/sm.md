# ara::sm - conformance to AUTOSAR AP R25-11 (SWS State Management)

Audited against
[AUTOSAR_AP_SWS_StateManagement.pdf (R25-11)](https://www.autosar.org/fileadmin/standards/R25-11/AP/AUTOSAR_AP_SWS_StateManagement.pdf).

**Verification limits.** Only the first ~100 000 characters of the PDF could be read (chapters 1-7 and
parts of 8). Method signatures, argument types and the *numeric* values of error codes (chapter 8/9) were
not readable, so they are marked *Unverified*. Requirement ids below are copied from the text that was read;
nothing was inferred.

Status legend: **Implemented** (behaviour present and unit-tested), **Partial**, **Missing**,
**Deviation** (present but different from the spec / obsolete), **Unverified** (cannot be confirmed from
the readable text).

## UpdateRequest service interface (sections 5.1, 7.3)

| Spec item | Id | Status | Notes |
|---|---|---|---|
| `RequestUpdateSession` | SWS_SM_00203 | Implemented | `UpdateSession::RequestUpdateSession` |
| `kNotAllowedMultipleUpdateSessions` on a second request | SWS_SM_00209 | Implemented | tested, also under 16 racing threads |
| Definition of an active session | SWS_SM_00210 | Implemented | active from a successful `RequestUpdateSession` until `StopUpdateSession` |
| `PrepareUpdate` | SWS_SM_00206 | Partial | session rules done; the function-group list is accepted but nothing is prepared (no call into Execution Management) |
| `VerifyUpdate` | SWS_SM_00207 | Partial | same as above |
| `PrepareRollback` | SWS_SM_00208 | Partial | same as above |
| `StopUpdateSession` | SWS_SM_00205 | Implemented | |
| `kOperationRejected` outside an active session | SWS_SM_00213 | Implemented | enumerator added; `kRejected` kept as an alias with the same value |
| `ResetMachine` | SWS_SM_00202 | Partial | accepted only inside an active session (the legacy doc comment says so; the R25-11 text for this id was not readable - *Unverified*). The restart itself is a handler (`SetResetHandler`) that the SM application has to install |
| Persist the session status across a restart | SWS_SM_00204 | Partial | `UpdateSession::Save/Load` exist and are tested; the SM application does not call them yet |
| `ResetMachineNotifier` field (default `kIdle`) | SWS_SM_00211, SWS_SM_00212 | Missing | |
| Reject Suspend-to-RAM during an active session (DRAFT) | SWS_SM_00214 | Missing | no S2R support |
| Extra errors `kVerifyFailed`, `kPrepareFailed`, `kRollbackFailed` | - | Deviation | from an older SWS release; not in the R25-11 text that was read. Kept for compatibility |
| Numeric error values (`kOperationRejected`, ...) | - | Unverified | legacy values 5..9 kept |
| Interface offered by the SM process | - | Missing | `App/SM` only offers the two TriggerIn services; the UpdateRequest skeleton is not instantiated there |

## State machines and Function Group handling (sections 7.6, 7.7)

| Spec item | Id | Status | Notes |
|---|---|---|---|
| Request any Function Group state through `StateClient::SetState` | SWS_SM_00400 | Partial | `App/SM/state_manage.cpp` requests `MachineFG=Running`, `FG_1=on/off` - hard-coded |
| Evaluate the returned `ExecErrc` | SWS_SM_00401 | Partial | only "has value / has error" is printed; no per-error handling |
| `GetInitialMachineStateTransitionResult` | section 5.2 | Missing | call is commented out in `StateManager::init` |
| `StateMachineService::RequestTransition` and its errors (`kTransitionNotAllowed`, `kInvalidValue`, `kRecoveryTransitionOngoing`, `kOperationCanceled`, `kTransitionFailed`) | SWS_SM_00600, 00603-00606, 00650 | Missing | no configurable state-machine model |
| `StateMachineNotification` | SWS_SM_00665 | Missing | |
| Offer / StopOffer timing | SWS_SM_00618, 00619 | Missing | |
| Valid request aborts the running ActionList | SWS_SM_00607 | Missing | |
| Minimum states per StateMachine (Initial, Off, PrepareUpdate, VerifyUpdate, PrepareRollback) and the configuration constraints | section 7.7.1, SWS_SM_CONSTR_00011/13/17/18/24/31 | Missing | SM has one fixed FSM (`STATE_INITIAL/RUNNING/UPDATE`) |
| `RecoveryHandler` returning `kSMCanNotHandleRecovery` for an unknown Function Group | SWS_SM_00030, 00031 | Missing in `ara::sm` | the dashboard node (`App/dashboard`) implements such a handler for its own workers |
| NetworkHandle integration (`FullCom` / `NoCom` triggering transitions, `SetNetworkHandle`) | SWS_SM_00620, 00621 | Missing | `ara::nm` now exists but is not wired into SM |
| Suspend-to-RAM: `S2RHub`, `S2RSatellite` (DRAFT) | SWS_SM_00701-00716 | Missing | |

## Legacy services that are not in the R25-11 text

| Item | Status | Notes |
|---|---|---|
| `TriggerIn` (UCM_State, OTA_State), `TriggerOut`, `TriggerInOut` | Deviation | not mentioned anywhere in the readable R25-11 text; used by the OTA/UCM demo to tell SM when to run an update. Kept |
| `DiagnosticReset` | Deviation | the R24-11 change history says support was removed |
| `PowerMode` | Deviation | R25-11 describes `apext::sm::PowerStateInterface` (Annex C) instead - *Unverified*, only the table of contents was visible |
| `ComMode` | Deviation | the spec uses NetworkHandle states `NoCom` / `FullCom` |

## What changed in this audit

* `ara/sm/update_request/update_session.h` - the session rules, thread-safe, unit-tested (12 tests in
  `Testing/sm`).
* `updaterequest_skeleton.h` previously could not link (an undefined `static activeSession`, undeclared
  bodies for `VerifyUpdate`/`PrepareRollback`, a reference to a non-existent `StateManager::EM`) and its
  `ResetMachine` could never succeed. It now delegates to `UpdateSession` and is compile/link-checked by a
  test.
* `kOperationRejected` added to `errorDomains`.

## Suggested next steps

1. Instantiate the UpdateRequest skeleton in `App/SM`, install a reset handler, call `Save/Load` at start-up.
2. Replace the hard-coded FSM by a configurable StateMachine model and `RequestTransition`.
3. Read chapters 8/9 of the PDF (needs a full copy of the document) to confirm signatures and error values.

## Gap closure (feature/gap-com-sm-ucm)

Verified against R25-11 where readable, otherwise R22-11 / R24-11. Numeric values and signatures remain
*Unverified*. Tests: `Testing/sm/state_machine_test.cpp` (24 tests; 36 in the cluster).

| Item | Status | Notes |
|---|---|---|
| Configurable StateMachine model, `RequestTransition` | Implemented | `ara/sm/state_machine/state_machine.h`; errors kTransitionNotAllowed, kInvalidValue, kRecoveryTransitionOngoing, kOperationCanceled, kTransitionFailed (00600, 00603-00607, 00650). Superseded requests are cancelled by a generation counter |
| Initial state + `GetInitialMachineStateTransitionResult` | Implemented | `StateManager::Start` |
| RecoveryHandler | Implemented | register/handle per function group, kSMCanNotHandleRecovery (00030, 00031) |
| NetworkHandle integration | Implemented | `NetworkHandleBridge` maps machine states to kNoCom/kFullCom (00620, 00621) |
| UpdateRequest <-> UCM | Implemented | UCM `UpdateHooks` drive `UpdateSession`; used by the `ota_updater` demo |

Still missing: instantiating the UpdateRequest skeleton in `App/SM` as a SOME/IP service, ResetMachineNotifier,
suspend-to-RAM (S2R). Shutdown after an OTA reset stays opt-in via `AP_SM_SHUTDOWN_ON_OTA=1`.
