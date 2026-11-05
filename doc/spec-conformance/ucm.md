# ara::ucm - conformance to AUTOSAR AP R25-11 (SWS Update and Configuration Management)

Audited against
[AUTOSAR_AP_SWS_UpdateAndConfigurationManagement.pdf (R25-11)](https://www.autosar.org/fileadmin/standards/R25-11/AP/AUTOSAR_AP_SWS_UpdateAndConfigurationManagement.pdf)
and, for requirement text that the R25-11 extract did not contain, the
[R22-11 edition](https://www.autosar.org/fileadmin/standards/R22-11/AP/AUTOSAR_SWS_UpdateAndConfigurationManagement.pdf).

**Verification limits.** The readable part of each PDF stops before chapter 9, so the formal interface block
(argument names/types, return types), the enum literal *values* and the numeric `UCMErrorDomain` values were
not available - they are *Unverified*. Method names, the per-method error lists, the check order and the
requirement ids are copied from the readable text. R22-11 ids are marked (R22).

Status legend as in [sm.md](sm.md).

## PackageManagement methods

R25-11 lists: Activate, Cancel, DeleteTransfer, Finish, GetHistory, GetId, GetProgress,
GetSwClusterChangeInfo, GetSwClusterInfo, GetSwClusterManifestInfo, GetSwPackages, ProcessSwPackage,
RegisterSoftwarePackage, RevertProcessedSwPackages, Rollback, TransferData, TransferExit, TransferStart.

| Method | Status | Notes |
|---|---|---|
| TransferStart | Partial | creates the transfer id and returns block size 1024; `InsufficientMemory` (00140) for size 0 or > 512 MiB. The wire format has no error channel, so a failure is reported as `BlockSize == 0` (**Deviation**) |
| TransferData | Partial | checks in spec order: 00203 InvalidTransferId, 00208 OperationNotPermitted (transfer already closed), 00204 IncorrectBlock, 00243 IncorrectBlockSize, 00205 IncorrectSize. Missing: 00206 (disk), 00289 TransferFailed, 00207 BlockInconsistent, 00294 / 00098 / 00092 / 00245 / 00103 (package format, authentication, consistency, removal, version) |
| TransferExit | Partial | 00148 OperationNotPermitted, 00212 InvalidTransferId, 00087 InsufficientData; sets *kTransferred* (00010). Missing: 00294, 00098, 00092, 00161, 00213, 00245, 00103 |
| DeleteTransfer | Implemented | 00214, 00215; also removes the stored files |
| ProcessSwPackage | Partial | 00218 InvalidTransferId, 00017 ServiceBusy, 00219 OperationNotPermitted; manifest problems map to `InvalidPackageManifest` (00029); cancellation checked at one point (00150). Missing: 00098, 00161, 00285, 00231, 00217, 00267, 00104, 00245, 00103. Unzipping still shells out to `unzip` (the path is the 32-hex id, so no injection) |
| Cancel | Partial | 00234, 00235; sets a flag that `ProcessSwPackage` honours at one checkpoint (spec: processing moves to *kProcessingFailed*, 00003) |
| RevertProcessedSwPackages | Partial | 00237 (only in kReady / kProcessing); processed packages become plain transfers again, UCM goes to kIdle. Missing: 00236 NotAbleToRevertPackages |
| Activate | Partial | 00241 (only in kReady) and the kActivating -> kActivated transition. Missing: 00026 MissingDependencies, 00258 UpdateSessionRejected, 00242 PrepareUpdateFailed, 00280 VerificationFailed - Activate does not talk to State Management; the flashing-adapter hand-over is stubbed and always succeeds |
| Rollback | Partial | 00239 (only in kActivated / kVerifying). Missing: 00110 (PrepareRollback + ResetMachine on State Management) |
| Finish | Partial | allowed after activation or rollback, runs kCleaningUp -> kIdle, forgets processed packages. The valid source states are *Unverified* |
| GetProgress (code name `GetSwProcessProgress`) | Partial | R25-11 renamed it (the TOC still shows the old name); returns the last progress value. 00220 InvalidTransferId is implemented in the core but the output struct has no error field, so it is not reported |
| GetSwPackages | Partial | reads `sw_packages.json` |
| GetSwClusterInfo | Partial | reads `sw_packages.json`; the `State` string ("0x01") is now parsed correctly (it used to be read as the single character `'0'` = 48) |
| GetHistory, GetId, GetSwClusterChangeInfo, GetSwClusterDescription | Missing | stubs returning empty structures |
| GetSwClusterManifestInfo, RegisterSoftwarePackage | Missing | not present |

## CurrentStatus field and state machine

| Item | Status | Notes |
|---|---|---|
| Field `CurrentStatus` | Implemented | now updated (and notified) on every transition; before, it stayed at 0 forever |
| Values kIdle 0, kReady 1, kProcessing 2, kActivating 3, kActivated 4, kRollingBack 5, kRolledBack 6, kCleaningUp 7, kVerifying 8 | Unverified | R22 text names all of these except kCleaningUp; the numeric values could not be checked. `kVerifying` is defined but never entered (no State Management hook) |
| `kCancelling` (campaign cancelled after repeated ServiceBusy / UpdateSessionRejected, 00297, 00298) | Missing | belongs to the campaign handling |
| Package states kTransferring -> kTransferred -> kProcessing -> kProcessed | Implemented | 00088, 00010, 00391; `kProcessingFailed` (00392) is Missing (a failed package returns to kTransferred) |
| Software Cluster states kPresent / kAdded / kUpdating / kRemoved | Partial | constants added (the legacy comment said *kUpdated*); the life cycle (Fig. 7.1, 00191-00197, 00286) is not driven - states are only read from `sw_packages.json` |
| Persisting UCM state across a reboot | Missing | 00184, 00273, 00305, 00306, 00329 |

## Error domain

| Item | Status | Notes |
|---|---|---|
| Names | Deviation | the code uses the R22 spelling (`InsufficientMemory`, `InvalidTransferId`, `IncorrectBlock`...). R25-11 uses `kMemoryInsufficient`, `kTransferIdInvalid`, `kBlockIncorrect`, ... Renaming would break the OTA client; not done |
| Numeric values | Unverified | |
| Errors on the wire | Deviation | outputs carry `int error`, not an ApplicationError |
| Missing codes | Missing | `kTransferFailed`, `kPackageFormatUnsupported`, `kPackageManifestInvalid`, `kPackageVersionIncompatible`, `kSwclRemovalDenied`, `kPersistencyAllocationFailed`, `kPackageInconsistent` (partly), ... |

## Out of scope

* UCM Master / Vehicle UCM - split into a separate specification in R23-11
  (`SWS_VehicleUpdateAndConfigurationManagement`); the R25-11 UCM text contains no master API.

## What changed in this audit

* `ucm_core.h` (new, header-only): the CurrentStatus state machine and the transfer bookkeeping with the SWS
  guards and error codes; thread-safe; 33 tests in `Testing/ucm`. The skeleton delegates to it.
* Bugs fixed on the way:
  * the last block was silently dropped when the package size is a multiple of the block size;
  * `TransferData` never returned an error (and `TransferDataOutput` was uninitialised);
  * `TransferExit`, `ProcessSwPackage` and `Finish` built the package file name three different ways, so
    `Finish` never found the file - all now use the 32-hex transfer id; transfers no longer share one global
    buffer, so parallel transfers do not corrupt each other;
  * `CurrentStatus` never changed;
  * `ActionType`, `ResultType`, `SoftwarePackageStepType`, `CampaignFailureType`, `CampaignStateType`,
    `TransferStateType` were aliases to `uint8_t &`, which made the structs that use them unusable;
  * missing/garbled `sw.conf`, a missing binary or one shorter than the 52-byte header could crash
    `ProcessSwPackage`; they now return `InvalidPackageManifest` / `ProcessedSoftwarePackageInconsistent`.

## Suggested next steps

1. Add an error channel to the service outputs (or switch to `ara::core::Result` with the R25-11 names).
2. Connect Activate / Rollback to State Management's UpdateRequest (`PrepareUpdate`, `VerifyUpdate`,
   `ResetMachine`) - the `UpdateSession` rules now exist on the SM side.
3. Package validation (format, authentication, manifest, version) and persistence of the UCM state.
4. Obtain the complete PDF to confirm chapter 9 (signatures, literal and error values).
