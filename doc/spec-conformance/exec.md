# ara::exec — conformance with AUTOSAR AP R25-11 (SWS Execution Management)

Source: [AUTOSAR_AP_SWS_ExecutionManagement.pdf (R25-11)](https://www.autosar.org/fileadmin/standards/R25-11/AP/AUTOSAR_AP_SWS_ExecutionManagement.pdf).

**Method and limits.** Only requirement ids readable in the R25-11 text (chapters 5 and 7) are cited.
Chapter 8 (formal API tables: signatures, `ExecErrc` numeric values, `ExecutionState` enumerators) could
**not be retrieved**, so those rows are *Unverified* rather than guessed. The deterministic-execution
requirements were set obsolete in R23-11 and removed in R24-11 (change history), see below.

Status legend: **Implemented**, **Partial**, **Missing**, **Deviation**, **Unverified**.

## Behaviour

| Requirement | Topic | Status | Notes / test |
|---|---|---|---|
| SWS_EM_02003 | `ExecutionClient::ReportExecutionState` | Partial | exists; returns `std::variant<std::monostate, ExecErrc>` instead of `ara::core::Result<void>` (legacy API used by every platform process) |
| SWS_EM_02243 | `ReportExecutionState(kRunning)` outside the Starting state returns `kInvalidTransition` | **Fixed in this audit** | second `kRunning` from the same client is rejected and not sent; a failed transport does not consume the Starting state (`SpecExec.*`) |
| SWS_EM_02582 | Only one `ExecutionClient` instance per process | Deviation | not enforced: several platform processes legitimately hold more than one client object (e.g. SM). Needs the apps migrated first |
| SWS_EM_02577 | `ExecutionClient::Create` takes a termination handler invoked on SIGTERM from a background thread | Missing | no `Create`, no handler; processes install their own SIGTERM handling and report `kTerminating` (legacy) |
| SWS_EM_01403 | Reporting from a non-reporting process is a violation | Missing | EM side does not know which processes are "reporting" |
| SWS_EM_02000 | `ExecutionState` enumeration | Unverified | `kRunning`, `kTerminating` implemented. The R25-11 text read shows the process states Initializing/Running/Terminating, but not the enumerators of the type |
| SWS_EM_02281–02291 | `ExecErrc`, `ExecException`, `ExecErrorDomain`, `GetExecErrorDomain`, `MakeErrorCode` | Partial | all present; `GetExecErrorDomain()` free function **added in this audit** (the older name `GetExecDomain()` is kept); `ExecErrc` numeric values and domain id `0x8000000000000202` unverified; `ExecException` has extra member functions not in the spec |
| SWS_EM_02543 | Default `ExecutionError` value 1 for abnormal terminations | Missing | EM process does not raise `ExecutionErrorEvent`s yet; PHM side already defaults to 1 (see phm.md, SWS_PHM_01361) |
| SWS_EM_01309 | Abnormal termination outside a transition: Undefined Function Group State + `undefinedStateCallback(ExecutionErrorEvent)` | Missing | `StateClient` has no `undefinedStateCallback` / `GetExecutionError`; the `ExecutionErrorEvent` type exists (`ExecutionError` + `FunctionGroup`) but is never produced |

## API shape

| Item | Status | Notes |
|---|---|---|
| `StateClient::SetState` | Partial | returns `std::future<std::variant<std::monostate, ExecErrc>>`; spec uses `ara::core::Future<void>`. FIFO transport is newline framed and EM validates `<FG>/<state>` |
| `StateClient::GetInitialMachineStateTransitionResult` | Partial | present, same return-type deviation |
| `StateClient::GetExecutionError`, `undefinedStateCallback` | Missing | see SWS_EM_01309 |
| `FunctionGroup` (`Preconstruct`, `CtorToken`, `==`, `!=`) | Implemented / Unverified | unknown meta-model id => `kMetaModelError` (tested). Extra setters/getters (`set_FGname`, `get_states`) are non-standard. Spec-level `Create()` shown in chapter 5 is not provided |
| `FunctionGroupState` (`Preconstruct`, `CtorToken`, `==`, `!=`) | Implemented / Unverified | invalid ids => `kMetaModelError` (tested) |
| `ExecutionErrorEvent` | Partial | fields `executionError`, `functionGroup`; never produced by EM |
| `DeterministicClient` | N/A | present as legacy code (`WaitForNextActivation`, `RunWorkerPool`, `GetRandom`, ...); the standard obsoleted these requirements in R23-11 and removed them in R24-11. Kept for existing users, not conformance relevant |

## Still open

1. `ExecutionClient::Create` with termination handler and single-instance rule (needs the platform processes migrated).
2. `ExecutionErrorEvent` generation + `undefinedStateCallback` / `GetExecutionError` in `StateClient`.
3. `ara::core::Result` / `Future` return types for the client APIs (breaking change for the apps).
4. Verify chapter 8 signatures, `ExecErrc` numeric values and the `ExecutionState` enumerators.
