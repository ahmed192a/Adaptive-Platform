# ara::core — conformance to AUTOSAR Adaptive Platform R25-11 (SWS_Core)

Audit of `src/ara/core` against `AUTOSAR_AP_SWS_Core`.

## Source of truth and its limits

| What | Status |
|---|---|
| R25-11 `AUTOSAR_AP_SWS_Core.pdf` | **Not reachable** from the audit environment (host not on the fetch allowlist). |
| R23-11 `AUTOSAR_AP_SWS_Core.pdf` | Reachable, but only the text up to chapter 7 (section 7.2.5) was returned. **Chapter 8 (the API signatures and the `CoreErrc`/`future_errc` tables) was not available.** |

Therefore:

* **Verified** below means the fact is stated in the R23-11 chapter 7 text that was read:
  the domain ids `0x8000'0000'0000'0014` (Core, SWS_CORE_00014) and `0x8000'0000'0000'0013`
  (Future, SWS_CORE_00013), `ErrorDomain::IdType` is 64 bit (SWS_CORE_10303), the shape of
  `MakeErrorCode(<SN>Errc, SupportDataType)` (SWS_CORE_10991) and the existence/section of the
  type families (e.g. Optional 11000, Variant 11600, Vector 11300, Map 11400, Array 11200,
  Span 11900, String 12000, StringView 12200, SteadyClock 11800, MemoryResource 11950-11952,
  Abort 12402-12407, InstanceSpecifier 10200-10203, Initialize/Deinitialize 90021/90022).
* **Unverified** means the item was compared against the author's knowledge of the SWS, not
  against the R25-11 text. No requirement id is claimed for such items. They should be
  re-checked once the R25-11 PDF is available.
* The `SWS_CORE_xxxxx` numbers in older header comments (e.g. `SWS_CORE_01901` on both `Vector`
  and `Span`, `SWS_CORE_00701 ff.` in `result.h`) come from an earlier, older numbering and are
  **stale/unreliable**. They were not changed.

Status legend: **Implemented**, **Partial**, **Missing**, **Deviation** (intentional or accepted
difference), **Unverified** (cannot be judged without chapter 8).

## Error handling

| API | Status | Notes |
|---|---|---|
| `ErrorDomain` (`IdType` = `uint64_t`, `CodeType`, `SupportDataType`, `Name()`, `Message()`, `ThrowAsException()`, `Id()`, `==`/`!=`, deleted copy/move, protected ctor/dtor) | Implemented | `IdType` width verified (SWS_CORE_10303). |
| `ErrorCode` (`Value()`, `Domain()`, `SupportData()`, `Message()`, `UserMessage()`, `ThrowAsException()`, `==`/`!=`, enum and `(value, domain)` constructors) | Implemented | `==` compares domain and value only (as the SWS). |
| `ErrorCode::Message()` / `UserMessage()` return type | **Deviation** | Return `char const *`; the SWS uses `StringView`. Kept: ~25 call sites (`std::string + Message()`, `EXPECT_STREQ`) rely on C strings, and `StringView` converts from `char const *` at every spec-style use site. Unverified against chapter 8. |
| `ErrorCode::UserMessage` constructor argument, `operator<<` | Deviation (extension) | Not in the SWS. Harmless additions. |
| `MakeErrorCode(CoreErrc, SupportDataType)` | Implemented | Superset of the verified shape (SWS_CORE_10991): `data` and `message` have defaults. |
| `Get<SN>ErrorDomain()` accessor | **Deviation** | Named `GetCoreErrorDomain()` and `GetFutureDomain()`; the SWS example form is `Get<SN>ErrorDomain()` (SWS_CORE_10980-10982). `GetFutureDomain` is the name used by the SWS for the Future domain (unverified). |
| `CoreErrorDomain` id | Implemented (verified) | `0x8000'0000'0000'0014`. |
| `CoreErrc` values 22 / 137 / 138 | Unverified | Match the SWS as recalled. Extensions 256-258 (`kAlreadyInitialized`, `kNotInitialized`, `kInitializationFailed`) are not in the SWS and are documented as such in the header. |
| `Exception` | **Fixed** | Constructor now takes `ErrorCode` by value (`noexcept`), and `what()` returns `Error().Message()`. Before, `what()` returned the generic `std::exception` text. `toString()` and `operator<<` are non-SWS extensions. |
| `CoreException` | Implemented | Constructor made `noexcept`. |
| `FutureErrorDomain`, `FutureErrc`, `FutureException` | **Fixed (was Missing)** | New `future_error_domain.h/.cpp`. Id `0x8000'0000'0000'0013` verified; numeric values 101-104 (`broken_promise`, `future_already_retrieved`, `promise_already_satisfied`, `no_state`) are **unverified**. |
| `ApplicationError` enum in `exception.h` | Deviation (non-SWS) | Not part of ara::core; left untouched. |

## Result / Future / Promise

| API | Status | Notes |
|---|---|---|
| `Result<T,E>`, `Result<void,E>`: ctors, `FromValue`, `FromError`, `EmplaceValue/Error`, `Swap`, `HasValue`, `operator bool`, `operator*`, `->`, `Value`, `Error`, `Ok`, `Err`, `ValueOr`, `ErrorOr`, `CheckError`, `ValueOrThrow`, `Resolve`, `Bind`, comparison operators, free `swap` | Implemented | Unverified in detail (chapter 8). |
| `Result::FromValue(Args...)` / `FromError(Args...)` | **Fixed** | Used brace-initialisation (`T{args...}`), so `Result<vector<int>>::FromValue(3, 7)` produced `{3, 7}` instead of three sevens. Now direct-initialises (`T(args...)`) and falls back to braces only for aggregates. |
| `Result::Value()` on an error | Deviation | Throws `std::bad_variant_access` (via `std::get`) instead of being a pure precondition violation. Safe superset. |
| Mutable `Value()`/`operator*` | Deviation (extension) | Not in the SWS; convenient and harmless. |
| `Future<T,E>`: `get`, `GetResult`, `valid`, `wait`, `wait_for`, `wait_until`, `then`, `is_ready`, `future_status` | Implemented | |
| `Future` operations on an empty Future | **Fixed** | `wait*`, `then`, `get`, `GetResult` dereferenced a null state (undefined behaviour). They now fail with `FutureErrc::no_state`. |
| `Promise<T,E>`: `get_future`, `set_value`, `SetError`, `SetResult` | Implemented | |
| Second `get_future()` | **Fixed** | Returned a silent invalid Future; now `FutureErrc::future_already_retrieved`. |
| Second `set_value`/`SetError`/`SetResult` | **Fixed** | Was silently ignored; now `FutureErrc::promise_already_satisfied`. |
| Broken promise | **Fixed (was Missing)** | A Promise destroyed (or move-assigned over) while unfulfilled now delivers `FutureErrc::broken_promise` to its Future, so waiters no longer block forever. Only available when `E` is constructible from `ErrorCode` (the default). |
| `Promise`/`Future` `Swap`, `Promise::set_error` aliases | Unverified / Missing | Not implemented; could not confirm they exist in R25-11. |
| `Future::then` continuation thread | Deviation | Runs on the fulfilling thread (or inline); the SWS leaves the execution context unspecified. |

## Initialization

| API | Status | Notes |
|---|---|---|
| `Initialize()`, `Deinitialize()` returning `Result<void>` | Implemented | Presence and sections verified (SWS_CORE_90021/90022, SWS_CORE_15002 listed in chapter 7). Error codes returned (`kAlreadyInitialized`, `kNotInitialized`, `kInitializationFailed`) are implementation specific. |
| `IsInitialized()`, `ARA_REGISTER_LIFECYCLE_HOOK` | Deviation (extension) | Not in the SWS. |

## InstanceSpecifier

| API | Status | Notes |
|---|---|---|
| `Create(StringView)`, explicit constructor (throws), `ToString()`, `==`/`!=` vs `InstanceSpecifier` and `StringView`, `<`, `std::hash` | Implemented | Presence verified (SWS_CORE_10200-10203). |
| `operator==`/`!=(StringView, InstanceSpecifier)` | **Fixed (was Missing)** | Only the `InstanceSpecifier == StringView` direction existed. |
| Accepted syntax | Unverified | Non-empty `/`-separated shortnames `[A-Za-z][A-Za-z0-9_]*`; errors `kInvalidMetaModelShortname` / `kInvalidMetaModelPath`. |

## Type aliases and utilities

| API | Status | Notes |
|---|---|---|
| `Optional` (+ `nullopt`, `nullopt_t`, `bad_optional_access`) | Implemented | Alias of `std::optional`. `make_optional` etc. not exported (unverified whether required). |
| `Variant`, `monostate`, `get`, `get_if`, `holds_alternative`, `visit` | Implemented | |
| `bad_variant_access`, `variant_npos`, `variant_size(_v)`, `variant_alternative(_t)` | **Fixed (was Missing)** | Added to `variant.h`. |
| `Vector`, `Map`, `Array`, `String`, `StringView`, `Byte` | Implemented | Aliases of the `std` types (C++17). `BasicString`/`to_string` helpers are not exported (unverified). `Byte` is `std::byte`; the SWS specifies a class with the same operations. |
| `Span`, `dynamic_extent` | Implemented | Own implementation in `span.h` (C++17 has no `std::span`). Not re-audited in detail. |
| `SteadyClock` | Implemented | `now()`, `is_steady`, `time_point<SteadyClock>`. |
| `in_place*`, `data`, `empty`, `size` | Implemented | |
| `MemoryResource`, `GetDefaultResource`, `SetDefaultResource`, `NewDeleteResource`, `NullMemoryResource` | **Fixed (was Missing)** | New `memory_resource.h`, mapped to `std::pmr`. The SWS family has more members (monotonic/pool resources); only the above are provided. Signatures unverified. |
| `Abort`, `AbortHandler`, `SetAbortHandler` | **Fixed (was Missing)** | New `abort.h/.cpp`. Existence verified (SWS_CORE_12402-12407); signatures unverified. |
| `SetTerminateHandler`/other process-level helpers | Missing | Not implemented; not confirmed required. |

## Tests

New tests are in `Testing/core/spec_conformance_test.cpp` (label `core`). `Testing/core/future_test.cpp`
was updated where it encoded the old behaviour (second `get_future`, repeated `set_value`).

## Open items

1. Re-run this audit against the R25-11 PDF (chapter 8) and replace every "Unverified" with a
   requirement id.
2. Decide whether to move `ErrorCode::Message()` to `StringView` (a breaking change for callers).
3. Complete the `MemoryResource` family and `Promise`/`Future` `Swap` if the R25-11 text requires them.
