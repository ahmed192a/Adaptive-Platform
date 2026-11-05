# Roadmap and gap analysis

Reference: AUTOSAR Adaptive Platform R25-11 (SWS documents at https://www.autosar.org/standards/adaptive-platform).

## Cluster status

| Cluster | State | Notes |
|---|---|---|
| ara::core | **rewritten** | `Result` (variant based, was empty), `Future/Promise`, `InstanceSpecifier`, `ErrorCode/Domain`, `Initialize/Deinitialize` with lifecycle hooks. 34 tests. |
| ara::log | **new, spec-aligned API** | `InitLogging`, `CreateLogger`, `LogStream`, `Hex/Bin`; console/file sinks. No DLT remote transport. Old P7 based code kept as `ara_log_legacy_lib`. |
| ara::per | **new** | Key-value + file storage, CRC-32 integrity, atomic persist with backup, quotas, JSON deployment config. CLI: `ap_per_tool`. 32 tests. |
| ara::phm | **new** | Alive / deadline / logical supervision, global supervision, recovery action, watchdog interface. Demo: `phm_demo`. 32 tests. |
| ara::exec | **hardened** | Manifest parsing no longer calls exit()/throws under noexcept; clients report kCommunicationError; StateClient no longer dangles; FunctionGroupState compares group and state; added `GetExecDomain`/`MakeErrorCode`. 7 tests. Exec domain id 0x8000000000000202 is unverified. Still missing: StateClient transition results, DeterministicClient review. |
| ara::com | legacy | SOME/IP + SD present; warnings and tests need work. |
| ara::sm, ara::ucm | legacy | |
| ara::diag | **UDS core (partial)** | Transport-independent `DiagnosticServer`: sessions + S3 timeout, 0x10/0x11/0x22/0x2E/0x3E, suppress-positive-response, DID registry, custom services. 12 tests. Missing: the SWS facade (Conversation, GenericUDSService, DataIdentifier classes), DTC/DEM (0x14/0x19), SecurityAccess (0x27), routines (0x31), DoIP/ISO-TP transport, Monitor/Event interfaces. |
| ara::crypto | **primitive layer (new)** | OpenSSL-backed (optional, `AP_BUILD_CRYPTO`): RNG, SHA-2, HMAC, AES-GCM (random-nonce seal/open), Ed25519 + ECDSA-P256, usage-restricted zeroizing `SecretKey`, thread-safe `KeyStore`. Known-answer tests (SHA-256, RFC 4231 HMAC, NIST GCM). 14 tests. Missing: SWS object model (CryptoProvider, KeySlot, CryptoContext), persistence of key slots via ara::per, X.509/certificates, KDF/key agreement, RSA, CBC/CTR, hardware (HSM) back-ends. |
| Not started | |  Network Management, Time Sync, IDSM, Firewall, RawDataStream, OSI, Remote Persistency, VUCM |

Suggested order: crypto facade + key persistence -> nm/tsync -> idsm -> diag facade/DTC.

## Known deviations (new clusters)

* Numeric values of `PerErrc`/`PhmErrc`/`LogErrc` and the error-domain ids are implementation choices and **must be checked against the SWS tables**.
* PHM reports expiry through `ExpiredSupervision` (not `ara::exec::ExecutionErrorEvent`) until exec provides that type.
* `ara::core::Result` has extra non-const `Value()&` / `operator*()&` overloads.
* A `Promise` destroyed unfulfilled leaves its `Future` pending (no broken-promise error yet).
* `ara::log` has no remote (DLT) sink; `LogMode::kRemote` is accepted and ignored.

## Findings in legacy code (not yet fixed)

* Warnings: `OTA/client.cpp:120`, `SomeipSDMessage.cpp` (range-for copies), `socket_Server.cpp:135`, `packagemanagement_skeleton.cpp` (unused variables), `deserializer.h:84` (memcpy into a vector), `exec_main.cpp:205`, `service_skeleton.h:148`, `deterministic_client.cpp:65` (ignored `read` result).
* Legacy tests rely on the working directory (manifests are copied to the build dir).
* Legacy targets use `ap::legacy` (`-Wall`) instead of the strict `ap::warnings`; migrate cluster by cluster.
