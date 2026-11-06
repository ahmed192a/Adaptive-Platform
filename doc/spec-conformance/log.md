# ara::log — conformance with AUTOSAR AP SWS Log and Trace

**Reference:** `AUTOSAR_AP_SWS_LogAndTrace` **R25-11** (fetched from autosar.org; the
excerpt available to this audit omitted the enumerator tables of chapter 8).
Requirement IDs below are only those that appeared in the retrieved text.
Anything not confirmed there is marked **Unverified** and is *not* claimed as
conformant.

Status legend: **Implemented** · **Partial** · **Missing** · **Extension**
(implementation-specific, not in the SWS) · **Unverified** (could not be
checked against the R25-11 text).

## Initialization and logger creation

| Spec item | Requirement | Status | Notes |
|---|---|---|---|
| Logging framework initialized via manifest `LogAndTraceInterface`, `ara::core::Initialize()` | SWS_LOG_00004 | Partial | Initialization is lazy on first `CreateLogger`; `ara::core::Deinitialize()` resets state. No manifest reader — console sink is the default. |
| `CreateLogger(InstanceSpecifier const&)` | SWS_LOG_00251 | Implemented | Context id = last path element of the specifier; threshold `kWarn`. Added in this audit. |
| `CreateLogger(StringView ctxId, StringView ctxDescription)` | SWS_LOG_00253 | Implemented | Default threshold `kWarn` (no `DltLogSink.defaultLogThreshold` manifest source). Added as an explicit overload. |
| `CreateLogger(StringView, StringView, LogLevel)` | SWS_LOG_00006 | Implemented | Same context id returns the same `Logger&`. |
| Context id limited to 4 characters | — | Unverified | Truncation to 4 chars is applied (DLT convention); not confirmed in R25-11 text. |
| `InitLogging(appId, desc, level, mode, dir)` | — | **Extension** | **Not part of R25-11** (the spec has no such function; it existed in R18-03–R20-11 era drafts). Kept for backward compatibility, documented as non-standard. |
| `GetClientState()` | — | Extension | `ClientState` enum exists in the SWS, but no free function was found in the retrieved text. Always `kNotConnected`. |

## Logger

| Spec item | Requirement | Status | Notes |
|---|---|---|---|
| `Logger::IsEnabled(LogLevel)` | SWS_LOG_00007 | Implemented | `kOff` is never enabled. |
| `LogFatal/LogError/LogWarn/LogInfo/LogDebug/LogVerbose` | SWS_LOG_00008–00013 | Implemented | Return `LogStream`, `noexcept`. |
| `WithLevel(LogLevel)` | SWS_LOG_00130 | Implemented | |
| `SetThreshold(LogLevel)` | SWS_LOG_00254 | Implemented | Added in this audit. `SetLevel` kept as an alias. |
| `Log(MsgId, Params...)`, `LogWith(attributes, MsgId, Params...)` | SWS_LOG_00240/00241 | **Missing** | Modeled-message API needs generated `MsgId` types from the Log and Trace model; out of scope until the model generator exists. |
| All `Log*()` are no-throw and return no error values | SWS_LOG_00002 | Implemented | Sink exceptions are swallowed in `Flush()`. |

## LogStream

| Spec item | Requirement | Status | Notes |
|---|---|---|---|
| `Flush()` | — | Implemented | Emits now; later insertions start a new record. |
| `WithLocation(file, line)` | — | Implemented | Added; recorded in `LogRecord::file/line` and appended by `FormatRecord`. |
| `WithTag(StringView)` | — | Implemented | Added; recorded in `LogRecord::tag`. |
| `WithPrivacy(...)` | — | **Missing** | The privacy enum is not in the retrieved text. |
| `operator<<` bool, integers (8–64 bit), float, double, `const char*`, `StringView`, `std::string`, `LogLevel` | SWS_LOG_00227–00238 (binding rules) | Implemented | |
| `operator<<` `const void*` | — | Implemented | Rendered as `0x` + full-width hex. Added. |
| `operator<<` `core::ErrorCode` | — | Implemented | Rendered as `Domain:value (message)`. Added. |
| `operator<<` `InstanceSpecifier` | — | Implemented | Added. |
| `operator<<` `std::chrono::duration` | — | Partial | Rendered as integer nanoseconds + `ns`; the SWS wire format is **Unverified**. Added. |
| `operator<<` `Span<const Byte>` | — | **Fixed (was Missing)** | `ara::core::Span` now compiles and is complete; a byte span is rendered as space-separated two-digit lowercase hex (`0a ff 10`). The SWS wire format of a byte span is **Unverified**. Test `LogTest.ByteSpanIsLoggedAsHex`. |
| `operator<<` `Argument<T>` / `Arg(value, Format)` | — | Implemented | Added. |

## Formatting modifiers

| Spec item | Status | Notes |
|---|---|---|
| `Fmt` enum (`kDefault`=0 … `kAutoFloat`=8) | Implemented | Values as published in the R25-11 tooling reference; added. |
| `Format { fmt, precision }` | Implemented | Added. |
| `Hex()`, `Hex(uint16_t)`, `Bin`, `Dec`, `Oct`, `DecFloat`, `EngFloat`, `HexFloat`, `AutoFloat` (+`…Max`) | Implemented | Added. The `…Max` variants use 17 significant digits (**Unverified** exact value). Integer prefixes `0x`/`0b`/`0o` and the meaning of `precision` (minimum digits / fractional digits) are implementation choices — **Unverified**. |
| `Dflt()` | Implemented | |
| `LogHex8/16/32/64`, `LogBin8/16/32/64` | Implemented | Fixed-width structs. **Deviation fixed:** previously the free functions `Hex(T)`/`Bin(T)` took a *value*; the SWS uses `Hex(...)` as a *modifier*. They were replaced by the `LogHexN`/`LogBinN` types. |

## Enums and error handling

| Spec item | Status | Notes |
|---|---|---|
| `LogLevel` kOff=0 … kVerbose=6 | Implemented | Values match the published enumerator table. |
| `ClientState` kUnknown=-1, kNotConnected=0, kConnected=1 | Implemented | Matches the published table. |
| `LogMode` | **Extension** | R25-11 text defines no `LogMode`; sink selection is by manifest `DltLogSink`. Kept with `InitLogging`. |
| `LogErrc` / `LogErrorDomain` | **Extension** | R25-11: *"interfaces neither return error values nor throw exceptions"*; no error domain is defined. Domain id `0x8000000000000403` is an implementation choice. |
| Manifest `DltLogSink` categories (Remote/Dlt/File/Console) | Missing | No manifest binding; console and file sinks are selected via `InitLogging` or environment variables. |

## Tests

`Testing/log/log_test.cpp` — 20 tests, label `log`.
