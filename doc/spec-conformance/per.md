# ara::per — conformance with AUTOSAR AP SWS Persistency

**References actually retrieved during this audit** (autosar.org):

* `AUTOSAR_AP_SWS_Persistency` **R24-11** — chapters 1–7.2.4 only (error handling, `PerErrc` table
  with requirement IDs); section 8 (API signatures) was *not* in the retrieved excerpt.
* NeuSAR API reference (R-series Doxygen mirror) for the free functions, `KeyValueStorage` and
  `ReadAccessor` signatures.
* `AUTOSAR_AP_SWS_Persistency` **R19-03** — `FileStorage` lifecycle methods only (older release).

The R25-11 Persistency PDF itself could not be fetched from this environment, so **no item below is
claimed verified against R25-11**; items taken from R24-11 or the mirror are noted. Requirement IDs
listed are only those that appeared in the retrieved text. Status legend: **Implemented** ·
**Partial** · **Missing** · **Extension** · **Unverified**.

## Free functions (`ara::per`)

| API | Status | Notes |
|---|---|---|
| `OpenKeyValueStorage(InstanceSpecifier) noexcept` → `Result<SharedHandle<KeyValueStorage>>` | Implemented | `noexcept` added in this audit. Same specifier ⇒ same storage. |
| `OpenFileStorage(InstanceSpecifier) noexcept` (SWS_PER_00116 in R19-03) | Implemented | `noexcept` added. |
| `RecoverKeyValueStorage`, `ResetKeyValueStorage` | Implemented | Require the storage to be closed (`kResourceBusy`). `noexcept` added. |
| `RecoverAllFiles`, `ResetAllFiles` | Partial | `RecoverAllFiles` always `kValidationFailed` (no redundancy kept for files). |
| `GetCurrentKeyValueStorageSize`, `GetCurrentFileStorageSize` | Implemented | `noexcept` added. |
| `ResetPersistency() noexcept` | Implemented | Resets every configured storage. |
| `UpdatePersistency() noexcept` | **Partial** | Added. There is no manifest/update source: returns `kNotInitialized` if unconfigured, `kResourceBusy` while a storage is open, otherwise success. Update semantics **Unverified**. |
| `RegisterRecoveryReportCallback(std::function<…>) noexcept` | **Partial** | Added. Invoked (outside internal locks) after a successful Recover/Reset of a KVS or file storage with one reported element. The `RecoveryReportKind` enumerators (`kRecovered`, `kReset`) are this implementation's choice — **Unverified**. `reportedInstances` is always empty. |
| `RegisterApplicationDataUpdateCallback(std::function<…>) noexcept` | **Partial** | Added; stored but never invoked (no update source). |
| `GetPersistencySize` | n/a | Named in the audit request but **not an API in the retrieved spec**; the spec has `GetCurrentKeyValueStorageSize` / `GetCurrentFileStorageSize`, which exist. |
| `Configure`, `ConfigureFromFile`, `ConfigureFromEnvironment` | Extension | Stand-in for the manifest (`ARA_PER_CONFIG` JSON). |
| `OpenMode` `operator\|`, `operator\|=` (and `&`, R19-03 SWS_PER_00144/00145) | Implemented | `OpenMode` enumerator values **Unverified**. |

## KeyValueStorage

| API | Status | Notes |
|---|---|---|
| `GetAllKeys() const noexcept` | Implemented | `noexcept` added to all methods below. |
| `KeyExists(StringView) const noexcept` | Implemented | |
| `GetValue<T>(StringView) const noexcept` → `Result<T>` | Implemented | |
| `GetValue<T>(StringView, T&) const noexcept` → `Result<void>` | Implemented | **Added.** Output parameter is untouched on error. |
| `SetValue<T>(StringView, T const&) noexcept` | Implemented | Non-JSON-representable values return `kDataTypeMismatch` instead of throwing. |
| `RemoveKey`, `RemoveAllKeys`, `SyncToStorage`, `DiscardPendingChanges` | Implemented | Atomic w.r.t. each other (mutex). Sync is temp-file + fsync + rename with CRC-32. |
| `RecoverKey`, `ResetKey` | Implemented | |
| Deleted copy/move/default construction | Implemented | Copy deleted; type is non-movable via the shared handle. |
| Storage-level redundancy, encryption, authentication (`kEncryptionFailed`, `kAuthenticationFailed`) | Partial | Error codes exist; no encryption/MAC of the KVS file itself (key slots are encrypted separately in `ara::crypto`). |

## FileStorage / accessors

| API | Status | Notes |
|---|---|---|
| `GetAllFileNames`, `FileExists`, `DeleteFile`, `RecoverFile` `noexcept` (R19-03 SWS_PER_00110/00111/00112/00337) | Implemented | `noexcept` added. |
| `ResetFile`, `GetCurrentFileSize`, `GetFileInfo`, `GetCurrentStorageSize` | Implemented | |
| `OpenFileReadOnly` / `OpenFileReadWrite` / `OpenFileWriteOnly` | Implemented | Spec signatures **Unverified** (not in retrieved text). One open handle per file (`kResourceBusy`). |
| `ReadAccessor`: `PeekChar`, `PeekByte`, `GetChar`, `GetByte`, `ReadText(n)`, `ReadText()`, `ReadBinary(n)`, `ReadBinary()`, `ReadLine(char)`, `GetPosition`, `SetPosition`, `IsEof`, `GetSize` | Implemented | `noexcept` added to the `Result`-returning ones. `ReadText()/ReadBinary()` are the defaulted-argument forms. |
| `ReadAccessor::MovePosition(Origin, int64) → Result<uint64_t>` | Implemented | **Deviation fixed:** previously returned `Result<void>`; the spec returns the new position. |
| `ReadAccessor::Origin` enumerators | Unverified | `kBeginning/kCurrent/kEnd` — names not confirmed. |
| `ReadWriteAccessor`: `SyncToFile`, `SetFileSize`, `WriteText`, `WriteBinary` | Partial | Names exist in the spec text; exact signatures **Unverified** (class page not retrieved). R19-03 stream helpers `endl`/`flush`/`operator<<` exist only as `operator<<(StringView)`. |
| `FileInfo` | Unverified | Fields `modificationTime`, `fileSize`. |

## Errors

| Item | Status | Notes |
|---|---|---|
| `PerErrc` enumerator *names*: `kStorageNotFound`, `kKeyNotFound`, `kIllegalWriteAccess`, `kPhysicalStorageFailure`, `kIntegrityCorrupted`, `kValidationFailed`, `kEncryptionFailed`, `kAuthenticationFailed`, `kDataTypeMismatch`, `kInitValueNotAvailable`, `kResourceBusy`, `kOutOfStorageSpace`, `kEof`, `kTooManyFiles`, `kQuotaExceeded` | Implemented | IDs seen in R24-11: SWS_PER_00536 (`kPhysicalStorageFailure`), 00537 (`kIllegalWriteAccess`), 00538 (`kStorageNotFound`), 00539 (`kIntegrityCorrupted`), 00540 (`kEncryptionFailed`), 00564 (`kAuthenticationFailed`), 00541 (`kEof`, renamed from `kIsEof` in R24-11), 00542 (`kTooManyFiles`), 00543 (`kQuotaExceeded`), 00544 (`kOutOfStorageSpace`), 00545/00546 (`kResourceBusy`). |
| `PerErrc` **numeric values** | **Unverified** | The retrieved text lists names and requirement IDs but not the numeric codes; ours (1–21) are an implementation choice. Vendor codes are specified above 255 (SWS_PER_00472) — ours stay below. |
| Extra enumerators `kFileNotFound`, `kInvalidPosition`, `kInvalidOpenMode`, `kInvalidSize`, `kNotInitialized`, `kUnsupported` | Extension | Not in the retrieved `PerErrc` table. `kFileNotFound` may exist in the spec (file storage page not retrieved). |
| `GetPerDomain`, `MakeErrorCode`, `PerErrorDomain::Name() == "Per"`, `Message`, `ThrowAsException` → `PerException` (SWS_PER_00473–00476, 00353) | Implemented | Domain id `0x8000000000000406` is an implementation choice (**Unverified**). |
| Manifest inconsistency ⇒ `ara::core::Abort` (SWS_PER_00571) | **Deviation** | A bad/missing configuration returns an error (`kStorageNotFound` / `kNotInitialized`) rather than aborting. Kept deliberately for testability. |

## Tests

`Testing/per` — 39 tests (label `per`), including compile-time `noexcept` checks for the public API, the
out-parameter `GetValue`, `MovePosition` return value, recovery-report callbacks (including
re-entrancy and a throwing callback) and `UpdatePersistency`.
