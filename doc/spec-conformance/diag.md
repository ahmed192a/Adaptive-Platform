# ara::diag - conformance to AUTOSAR AP Diagnostics

## Verification basis (read this first)

* Target release: **R25-11**. The R25-11 SWS PDF could not be fetched from the build environment (host not
  allow-listed), and the R24-11 copy that could be reached returned only its front matter and table of contents.
* So **requirement IDs and numeric values are not verified** and none are quoted here. What *is* confirmed (R24-11
  table of contents) is the **class and function names**: `Conversation`, `GenericUDSService`
  (`HandleMessage`, `Offer`, `StopOffer`), `GenericDataIdentifier`, `DataTransfer*`, `GenericRoutine`
  (`Offer`, `Start`, `Stop`, `RequestResults`, `StopOffer`), `Monitor` (`Offer`, `ReportMonitorAction`,
  `StopOffer`), `DTCInformation`, `Event`, and the error enumerations `DiagErrc`, `DiagOfferErrc`,
  `DiagReportingErrc`, `DiagUdsNrcErrc`.
* **Fallback to earlier releases**: R22-11 (and the nearest reachable releases) SWS PDFs are also not on the
  allow-list, and third-party Doxygen mirrors only list names without values. The gap work below therefore follows
  ISO 14229-1 for service behaviour and the confirmed ara::diag class names for the API; no SWS requirement text
  or numeric `DiagErrc` value was available to quote. Scope changes between releases are small for this cluster.
* UDS behaviour is checked against ISO 14229-1 knowledge, which is what the SWS defers to; status "Implemented"
  below means "behaves as ISO 14229-1 describes and is covered by a test", not "SWS-certified".

## What ara::diag is today

`DiagnosticServer` is a **transport-independent UDS engine** (call `Dispatch(request, Addressing)` from DoIP/ISO-TP).
`diag_facade.h` adds pragmatic counterparts of the SWS classes (`Conversation`, `GenericUDSService`,
`GenericRoutine`, `GenericDataIdentifier`, `Monitor`) on top of it, using `ara::core::Result` and byte vectors
rather than the SWS `Future`/`OperationOutput` signatures. `App/demo/uds_tester` drives the whole server through
a scripted session (see the demo section).

## Service status (ISO 14229-1)

| SID | Service | Status | Notes |
|---|---|---|---|
| 0x10 | DiagnosticSessionControl | Implemented | default/programming/extended, P2/P2* in response, S3 timeout, relocks security on change |
| 0x11 | ECUReset | Implemented | sub 0x01-0x05 via reset handler; 0x04 (enableRapidPowerShutDown) returns powerDownTime; session falls back to default afterwards |
| 0x14 | ClearDiagnosticInformation | Partial | 0xFFFFFF, one exact DTC, or a group registered with `RegisterDtcGroup`; no memory selection |
| 0x19 | ReadDTCInformation | Partial | sub 0x01, 0x02, 0x03, 0x04, 0x06, 0x0A; no severity (0x07/0x08) and other subs -> 0x12 |
| 0x22 | ReadDataByIdentifier | Implemented | multi-DID; answers the supported DIDs, 0x31 only if none is available |
| 0x27 | SecurityAccess | Implemented | seed/key, sequence error, attempt counter, lockout delay; seed/key algorithm is user-provided |
| 0x2E | WriteDataByIdentifier | Implemented | per-DID session mask, length check, security level |
| 0x31 | RoutineControl | Implemented | start/stop/requestResults, session + security gating |
| 0x3E | TesterPresent | Implemented | sub 0x00 (and 0x80 suppress); keeps the S3 timer alive |
| 0x85 | ControlDTCSetting | Implemented | on/off freezes event-memory updates |
| 0x28 | CommunicationControl | Implemented | sub 0-5 (4/5 need node id), communicationType validated, user handler; `RestrictBuiltinService` gates sessions |
| 0x23 / 0x3D | Read/WriteMemoryByAddress | Implemented | addressAndLengthFormatIdentifier parsing, registered `MemoryRegion`s, session + security gating |
| 0x34 / 0x35 / 0x36 / 0x37 | RequestDownload/Upload, TransferData, RequestTransferExit | Implemented | block sequence counter with wrap (NRC 0x73), size checks, cancelled on session change |
| 0x38, 0x2A, 0x2C, 0x86, 0x87 | RequestFileTransfer, periodic DIDs, DynamicallyDefineDID, ResponseOnEvent, LinkControl | Missing | custom services can be added with `RegisterService` |

## Cross-cutting behaviour

| Topic | Status | Notes |
|---|---|---|
| SID validity (0x01-0x3E, 0x80-0xBE) | Implemented | invalid SIDs rejected at registration |
| Unknown SID -> NRC 0x11 | Implemented | |
| Wrong length -> NRC 0x13, unknown sub-function -> 0x12 | Implemented | per built-in service |
| Service/sub-function not in active session | Implemented | custom services, routines and built-ins restricted via `RestrictBuiltinService` -> 0x7F |
| suppressPositiveResponse | Implemented | never suppresses negative responses |
| Functional addressing (suppress 0x11/0x12/0x31/0x7E/0x7F) | Implemented | `Dispatch(request, Addressing::kFunctional)` |
| NRC 0x78 responsePending | Implemented | a handler returning 0x78 triggers `7F sid 78` on the response sink and a re-poll, bounded by `maxPendingRetries` |
| Concurrency | Implemented | mutex-protected registries; handlers run outside the lock |

## SWS API surface

| SWS element | Status | Notes |
|---|---|---|
| `Conversation`, `ConversationIdentifierType` | Partial | `diag_facade.h` `Conversation` wraps a server; signatures are pragmatic, not SWS `Future`-based |
| `GenericUDSService` (Offer/StopOffer/HandleMessage) | Partial | `Offer`/`StopOffer` over `RegisterService`/`UnregisterService`; byte-vector signatures |
| `GenericDataIdentifier` / data-transfer classes | Partial | read/write DIDs plus `SetTransferHandler` for 0x34-0x37; no SWS `DataTransfer*` class hierarchy |
| `GenericRoutine` | Partial | Offer/StopOffer lifecycle, start/stop/results; no routine-specific serialization |
| `Monitor`, `Event`, `DTCInformation` | Partial | `Monitor` with counter debounce (failed/passed thresholds, freeze, reset, fault detection counter -128..127), occurrence counter, snapshot and extended data; no time-based debouncing or aging |
| `DiagErrc` enumerators | Partial | names kAlreadyOffered/kConflict/kNotOffered/... are used; **numeric values and domain id are implementation choices (Unverified)**; `DiagOfferErrc`, `DiagReportingErrc`, `DiagUdsNrcErrc` do not exist yet |
| Persistent DTC memory (via ara::per) | Implemented | `MakePerDtcStorage(kvs)` saves after every change and reloads on start (`per_dtc_storage.h`) |

## Remaining gaps

1. `DiagOfferErrc`, `DiagReportingErrc`, `DiagUdsNrcErrc` are not created: their enumerators and values could not be
   verified and inventing them would be misleading. `DiagErrc` numeric values and domain id stay **Unverified**.
2. SIDs 0x2A, 0x2C, 0x38, 0x86, 0x87 and ReadDTCInformation sub-functions beyond the six above (severity, mirror
   memory, user-defined memory) are not implemented.
3. DTC aging and debouncing state are not persisted; only status, occurrence counter, snapshots and extended data are.
4. The facade uses `ara::core::Result` and byte vectors instead of the SWS `Future`, `OperationOutput` and
   `MetaInfo` types.
5. No ISO-TP / DoIP transport: `Dispatch` is the integration point.

## Known deviations

1. Status-of-DTC availability mask is fixed (0x2B).
2. Built-in SIDs cannot be overridden through `RegisterService` (returns `kAlreadyOffered`); use
   `RestrictBuiltinService` to change which sessions may call one.

## Demo

`App/demo/uds_tester` builds a small body-control ECU on top of the server and a tester that runs 35 checks:
default session, identification DIDs, functional TesterPresent, extended session and write, security access
(wrong key, then right key), routine, DTC count/list/snapshot/extended data/clear, memory read/write, firmware
download with a block sequence error, communication control and reset. Run `uds_tester` for the trace, or
`uds_tester --repl` to type hex requests (`f 3E 80` = functional, `wait 6000` advances the S3 clock).

## Tests

`Testing/diag`: `ara_diag_ut` (existing 24 tests plus 21 gap tests: functional addressing, partial multi-DID,
session restriction, ECUReset 4/5, CommunicationControl, memory access, download/upload with counter and session
cancel, responsePending, DTC snapshot/extended/occurrence, group clear, persistent restart, codec, facade, monitor
debounce) and `uds_tester_ut` (4 tests: the scripted session, hex parsing/description, S3 timeout).
