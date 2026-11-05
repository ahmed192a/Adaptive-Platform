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
* UDS behaviour is checked against ISO 14229-1 knowledge, which is what the SWS defers to; status "Implemented"
  below means "behaves as ISO 14229-1 describes and is covered by a test", not "SWS-certified".

## What ara::diag is today

`DiagnosticServer` is a **transport-independent UDS engine** (call `Dispatch(request)` from DoIP/ISO-TP). It is
*not* yet the SWS service-interface facade (`Conversation`, `GenericUDSService`, ...); those are **Missing**.

## Service status (ISO 14229-1)

| SID | Service | Status | Notes |
|---|---|---|---|
| 0x10 | DiagnosticSessionControl | Implemented | default/programming/extended, P2/P2* in response, S3 timeout, relocks security on change |
| 0x11 | ECUReset | Partial | sub 0x01-0x03 via reset handler; 0x04/0x05 (rapid shutdown) -> 0x12 |
| 0x14 | ClearDiagnosticInformation | Partial | groupOfDTC = 0xFFFFFF or one exact DTC; no group-of-DTC tables, no memory selection |
| 0x19 | ReadDTCInformation | Partial | sub 0x01, 0x02, 0x0A only; no snapshot/extended data (0x04/0x06), no severity (0x07/0x08) |
| 0x22 | ReadDataByIdentifier | Implemented | multi-DID; fail-whole on first unsupported DID (see deviations) |
| 0x27 | SecurityAccess | Implemented | seed/key, sequence error, attempt counter, lockout delay; seed/key algorithm is user-provided |
| 0x2E | WriteDataByIdentifier | Implemented | per-DID session mask, length check, security level |
| 0x31 | RoutineControl | Implemented | start/stop/requestResults, session + security gating |
| 0x3E | TesterPresent | Implemented | sub 0x00 only |
| 0x85 | ControlDTCSetting | Implemented | on/off freezes event-memory updates |
| 0x28, 0x23, 0x2A, 0x2C, 0x34-0x38, 0x3D, 0x86, 0x87 | CommunicationControl, memory/transfer, periodic DIDs, ResponseOnEvent, LinkControl | Missing | custom services can be added with `RegisterService` |

## Cross-cutting behaviour

| Topic | Status | Notes |
|---|---|---|
| SID validity (0x01-0x3E, 0x80-0xBE) | Implemented | invalid SIDs rejected at registration |
| Unknown SID -> NRC 0x11 | Implemented | |
| Wrong length -> NRC 0x13, unknown sub-function -> 0x12 | Implemented | per built-in service |
| Service/sub-function not in active session | Partial | custom services -> 0x7F; routines -> 0x7F; built-ins are session-independent |
| suppressPositiveResponse | Implemented | never suppresses negative responses |
| Functional addressing (suppress 0x11/0x12/0x31/0x7E/0x7F) | Missing | the server has no addressing information |
| NRC 0x78 responsePending | Partial | handler may return it; no automatic P2* timer handling |
| Concurrency | Implemented | mutex-protected registries; handlers run outside the lock |

## SWS API surface

| SWS element | Status | Notes |
|---|---|---|
| `Conversation`, `ConversationIdentifierType` | Missing | |
| `GenericUDSService` (Offer/StopOffer/HandleMessage) | Partial | `RegisterService(sid, sessions, handler)` is the equivalent engine hook |
| `GenericDataIdentifier` / data-transfer classes | Partial | `RegisterDataIdentifier` covers read/write; no data transfer (0x34-0x37) |
| `GenericRoutine` | Partial | `RegisterRoutine` (start/stop/results); no Offer()/StopOffer() lifecycle, no routine-specific serialization |
| `Monitor`, `Event`, `DTCInformation` | Partial | `ReportDtc(dtc, failed)` with ISO status bits {testFailed, thisOperationCycle, confirmed, sinceLastClear}; no debouncing, aging, occurrence counters, snapshot data |
| `DiagErrc` enumerators | Partial | names kAlreadyOffered/kConflict/kNotOffered/... are used; **numeric values and domain id are implementation choices (Unverified)**; `DiagOfferErrc`, `DiagReportingErrc`, `DiagUdsNrcErrc` do not exist yet |
| Persistent DTC memory (via ara::per) | Missing | event memory is RAM only |

## Known deviations

1. `ReadDataByIdentifier` with several DIDs fails the whole request with 0x31 if any one DID is unknown; ISO
   14229-1 allows answering the supported ones (requirement text for the SWS not verified).
2. Status-of-DTC availability mask is fixed (0x2B).
3. Built-in SIDs cannot be overridden through `RegisterService` (returns `kAlreadyOffered`).

## Tests

`Testing/diag`: 24 tests (12 engine, 12 added for SecurityAccess, RoutineControl, DTC services and session-relock).
