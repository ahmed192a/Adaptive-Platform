# ara::exec — conformance with AUTOSAR AP R25-11 (SWS Execution Management)

Source: [AUTOSAR_AP_SWS_ExecutionManagement.pdf (R25-11)](https://www.autosar.org/fileadmin/standards/R25-11/AP/AUTOSAR_AP_SWS_ExecutionManagement.pdf).

**Method and limits.** Only requirement ids readable in the R25-11 text (chapters 5 and 7) are cited.
Chapter 8 (formal API tables: signatures, `ExecErrc` numeric values, `ExecutionState` enumerators) could
**not be retrieved**, so those rows are *Unverified* rather than guessed. The deterministic-execution
requirements were set obsolete in R23-11 and removed in R24-11 (change history), see below.

**Fallback release.** R22-11 (`AUTOSAR_SWS_ExecutionManagement.pdf`) was read as a fallback: it confirms the ids and
behaviour of SWS_EM_01309, SWS_EM_02241, SWS_EM_02275 and SWS_EM_02547, but its extracted text also lacks the
Chapter 8 signature tables, so exact parameter lists stay *Unverified*. Rows below say which release backs them.

Status legend: **Implemented**, **Partial**, **Missing**, **Deviation**, **Unverified**.

## Behaviour

| Requirement | Topic | Status | Notes / test |
|---|---|---|---|
| SWS_EM_02003 | `ExecutionClient::ReportExecutionState` | Partial | legacy form returns `std::variant<std::monostate, ExecErrc>` (used by every platform process); **added** `ReportExecutionStateResult` returning `ara::core::Result<void>` (`ReportExecutionStateResultMirrorsTheLegacyApi`, `...ReportsCommunicationErrors`). Exact spec name/signature Unverified (R22-11 and R25-11 name the method, not the signature) |
| SWS_EM_02243 | `ReportExecutionState(kRunning)` outside the Starting state returns `kInvalidTransition` | **Fixed in this audit** | second `kRunning` from the same client is rejected and not sent; a failed transport does not consume the Starting state (`SpecExec.*`) |
| SWS_EM_02582 | Only one `ExecutionClient` instance per process | **Implemented for `Create()`** | a second `Create()` returns `kGeneralError`, the slot is freed when the created client is destroyed or moved-from (`CreateEnforcesSingleInstanceAndReleasesTheSlot`). The legacy default constructor stays unrestricted because several platform processes (e.g. SM) hold more than one client |
| SWS_EM_02577 | `ExecutionClient::Create` takes a termination handler invoked on SIGTERM from a background thread | **Implemented** | `ExecutionClient::Create(std::function<void()>)` -> `Result<ExecutionClient>`; SIGTERM only writes to a self-pipe, a background thread runs the handler (`SigtermRunsTheTerminationHandlerOnABackgroundThread`, `DestroyingTheClientDoesNotInvokeTheHandler`); empty handler => `kInvalidArguments`. Signature Unverified; demo: `supervised_service` |
| SWS_EM_01403 | Reporting from a non-reporting process is a violation | Missing | EM side does not know which processes are "reporting" |
| SWS_EM_02000 | `ExecutionState` enumeration | Unverified | `kRunning`, `kTerminating` implemented. The R25-11 text read shows the process states Initializing/Running/Terminating, but not the enumerators of the type |
| SWS_EM_02281–02291 | `ExecErrc`, `ExecException`, `ExecErrorDomain`, `GetExecErrorDomain`, `MakeErrorCode` | Partial | all present; `GetExecErrorDomain()` free function **added in this audit** (the older name `GetExecDomain()` is kept); `ExecErrc` numeric values and domain id `0x8000000000000202` unverified; `ExecException` has extra member functions not in the spec |
| SWS_EM_02543 | Default `ExecutionError` value 1 for abnormal terminations | **Implemented** | `kDefaultExecutionError = 1`; EM publishes it for unexpectedly dead processes (`DefaultExecutionErrorIsOne`, `AbnormalTerminationDecision`) |
| SWS_EM_01309 | Abnormal termination outside a transition: Undefined Function Group State + `undefinedStateCallback(ExecutionErrorEvent)` | **Partial** (verified against R22-11 behaviour text) | EM now polls its children (250 ms), treats exit status 0 as normal and anything else as abnormal, and writes `"<FG>/<error>\n"` to `processes/sm_process/execution_error_fifo`; `StateClient(undefinedStateCallback)` delivers it from a background thread and keeps the last error per group (`StateClientDeliversExecutionErrorsToCallbackAndGetExecutionError`). Not done: the *Undefined Function Group State* itself (EM keeps its FG state), and the group of a process is its first configured function-group reference |

## API shape

| Item | Status | Notes |
|---|---|---|
| `StateClient::SetState` | Partial | legacy form returns `std::future<std::variant<...>>`; **added** `SetStateFuture` returning `ara::core::Future<void>` (`StateClientFutureVariantsReportErrorsAndSuccess`). FIFO transport is newline framed and EM validates `<FG>/<state>` |
| `StateClient::GetInitialMachineStateTransitionResult` | Partial | present; **added** `GetInitialMachineStateTransitionFuture` (`ara::core::Future<void>`). R22-11 (SWS_EM_02241) says a cancelled startup transition yields `kCancelled`: not produced, the transition result is not tracked |
| `StateClient::GetExecutionError`, `undefinedStateCallback` | **Implemented** (signature Unverified) | constructor takes the callback, `GetExecutionError(FunctionGroup const&)` returns the last event or `kGeneralError`; see SWS_EM_01309 |
| `FunctionGroup` (`Preconstruct`, `CtorToken`, `==`, `!=`) | Implemented / Unverified | unknown meta-model id => `kMetaModelError` (tested). Extra setters/getters (`set_FGname`, `get_states`) are non-standard. Spec-level `Create()` shown in chapter 5 is not provided |
| `FunctionGroupState` (`Preconstruct`, `CtorToken`, `==`, `!=`) | Implemented / Unverified | invalid ids => `kMetaModelError` (tested) |
| `ExecutionErrorEvent` | Implemented | fields `executionError`, `functionGroup` (SWS_EM_02547 names `executionError`); now produced by EM (see SWS_EM_01309); header got an include guard |
| `DeterministicClient` | N/A | present as legacy code (`WaitForNextActivation`, `RunWorkerPool`, `GetRandom`, ...); the standard obsoleted these requirements in R23-11 and removed them in R24-11. Kept for existing users, not conformance relevant |

## Still open

1. `ara::core::Result` / `Future` as the *only* return types: the legacy forms stay for the platform processes; migrate them, then retire the variant/std::future overloads.
2. The *Undefined Function Group State* in EM and the cancelled-transition result (SWS_EM_01309, SWS_EM_02241).
3. Non-reporting process violations (SWS_EM_01403): the EM does not know which processes are reporting processes.
4. Verify chapter 8 signatures, `ExecErrc` numeric values and the `ExecutionState` enumerators (not in the R25-11 or R22-11 text that could be read).
