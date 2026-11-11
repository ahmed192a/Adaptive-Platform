# Adaptive Platform: development plan

> Written 2026-11-11 from a read-through of `master` at `338d679`. It complements
> [doc/ROADMAP.md](doc/ROADMAP.md) (status and deviations) and the per-cluster reports in
> [doc/spec-conformance/](doc/spec-conformance/). This file answers a different question:
> **what to build next, in which order, and which applications to extend.**
>
> Basis: source, tests, docs and CI configuration were read; the project was not built or run for this analysis.

## 1. Where the project stands

| Area | Size (src LOC) | Tests (`TEST*` macros) | Assessment |
|---|---|---|---|
| `core`, `log`, `per`, `phm`, `nm`, `tsync` | 0.9k to 1.9k each | 21 to 65 each | New code, `Result` based, injected clocks, strict warnings. Solid as libraries. |
| `diag` | 1.7k | 45 | UDS server, facade, DTC memory persisted via `per`. No transport. |
| `crypto` | 2.5k | 72 | Primitives, KDF, RSA, key wrap, key slots. Not used by any other cluster. |
| `exec` | 2.5k | 25 (+10 EM) | Client side hardened. The EM itself is one 371-line `App/exec_main.cpp`. |
| `com` | 7.0k | 53 | Largest and oldest cluster. New pieces (event buffer, session ids, reboot detector) exist but are not wired into the transport. |
| `sm`, `ucm` | 1.6k / 3.7k | 36 / 61 | New state-machine and UCM core logic sit beside legacy SOME/IP skeletons. |
| Apps | dashboard 2.5k, 7 demo apps 1.6k, EM/SM/SD/UCM/OTA 2.1k | 19 + 8 | Dashboard and demos are in good shape; platform processes are legacy. |

### The main structural finding

The project has **two halves that barely touch**:

1. **New clusters** are well-tested libraries, but each runs *in-process with simulated inputs*: NM has no bus,
   TSync has no time source, Diag has no transport, PHM supervises only its own process, Log has no remote sink.
2. **Legacy platform processes** (EM, SM, SD, UCM, OTA) really run as separate processes over SOME/IP, but they
   use none of the new clusters: `std::cout` instead of `ara::log`, a CSV file as the service registry
   (`App/SD/sd.cpp`), FIFOs for EM/SM, a UART driver compiled into the UCM skeleton.

So the highest-value work is not another isolated cluster. It is **connecting the halves**: give the new clusters real
transports, and move the platform processes onto the new clusters. Sections 2 and 3 are ordered with that in mind.

### Smaller findings

* `doc/ROADMAP.md` is behind the code: it lists SecurityAccess, routines and DTC as missing in `diag`, and KDF, RSA,
  CBC/CTR and X.509 as missing in `crypto`, while the conformance reports, `extras.h` and the demos show them present.
  It also gives the tsync domain id as `...0501`; the code uses `...0502` (`...0501` is `nm`).
* No `LICENSE` file, although the README calls the project "open".
* Binary and stale files in the tree: `Tools/lib` (17 MB of JavaFX jars), two `.exe` tools, `Tools/jar`,
  `src/ara/com/SOMEIP/Message.zip`, `src/ara/ucm/pkgmgr/test.zip`, `doc/doxygen.conf.bak`, `Cloud/package.zip`.
* No `.clang-format` / `.clang-tidy`, no fuzzing, no thread-sanitizer preset, no coverage threshold.
* Legacy targets still build with `ap::legacy` warnings; `test_partial_processes/` and `quick_test/` duplicate what `Testing/`
  and the presets now do.
* `Cloud/Dockerfile` uses `ubuntu:latest`, the deprecated `MAINTAINER`, and a raw TCP socket on port 8888 with no
  TLS or authentication. `src/OTA` (880 LOC) has no tests.
* Most error-domain ids and `*Errc` numeric values are still *Unverified* against the SWS tables.

## 2. Features that would help the platform

Effort: **S** up to 2 days, **M** up to a week, **L** more than a week. Value is judged by how much other work it unblocks.

### 2.1 Communication (`ara::com`): the backbone  *(highest value)*

| # | Feature | Effort | Why |
|---|---|---|---|
| C1 | Wire `ProxyEventBuffer`, `RequestTracker` and `SdRebootDetector` into the socket transport | M | Already written and tested; currently dead code on the real path. |
| C2 | `ara::core::Future` based method calls, `Result` returns, `ComErrc` on every failure path | M | Removes the last exception/blocking API in the platform; needed by SM/UCM facades. |
| C3 | In-memory SD registry with TTL and offer/find/subscribe state machines, replacing `csv.h` | M | CSV file is racy and survives crashes; this also gives the dashboard SD traffic to show. |
| C4 | Local IPC binding over Unix domain sockets behind the same proxy/skeleton API | M | Lets EM/SM/PHM drop FIFOs; gives PHM a cross-process checkpoint channel. |
| C5 | SOME/IP serialization per the PRS (alignment, length fields, strings, optionals/TLV) with golden-vector tests | L | Interoperability with vsomeip / CommonAPI peers. |
| C6 | E2E protection (profile 4 or 7 first) as a transformer on events | M | Safety relevance; small, self-contained, very testable. |
| C7 | SOME/IP-TP segmentation | M | Payloads above the UDP limit (UCM transfer blocks). |
| C8 | Interop test job in CI against vsomeip | M | Proves C1 to C5 against an independent implementation. |

### 2.2 Execution and health

| # | Feature | Effort | Why |
|---|---|---|---|
| E1 | Move EM logic out of `exec_main.cpp` into a tested `ara_exec_em` library (process table, FG state machine, transition engine) | M | Makes the "Terminate" noise, undefined-FG state and cancelled transitions fixable and testable. |
| E2 | Manifest schema: JSON-schema validation, process dependencies, startup/termination timeouts, restart policy, arguments, environment | M | Current manifests carry only name and FG modes. |
| E3 | Resource configuration: scheduling policy/priority, CPU affinity, cgroup memory/CPU limits, uid/gid | M | Core EM duty in the SWS; cheap on Linux. |
| E4 | `ExecutionErrorEvent` to SM, undefined FG state, reporting-process tracking (SWS_EM_01309, 02241, 01403) | M | Closes the open exec gaps. |
| E5 | PHM daemon: supervised entities in other processes report checkpoints over C4; process-identity check (SWS_PHM_01229) | L | Turns PHM from a library demo into platform health management. |
| E6 | PHM recovery through SM (`RecoveryHandler`) to EM (restart process / switch FG) | M | The complete supervision, recovery, restart chain end to end. |

### 2.3 Diagnostics

| # | Feature | Effort | Why |
|---|---|---|---|
| D1 | **DoIP** transport (ISO 13400: vehicle announcement, routing activation, diagnostic message, alive check) on TCP/UDP 13400 | L | Real testers (`python-doipclient` + `udsoncan`, CANoe) can connect. Biggest single demo upgrade. |
| D2 | ISO-TP over SocketCAN (`vcan0`) as second transport | M | Kernel does the segmentation (`CAN_ISOTP`); shows transport independence. |
| D3 | Persist DTC aging and debounce counters | S | Listed gap; `per_dtc_storage` already exists. |
| D4 | SIDs 0x2A, 0x2C, 0x86, 0x87 and remaining 0x19 sub-functions | M | Completeness. |
| D5 | SWS types (`OperationOutput`, `MetaInfo`, `Future`) on the facade; `DiagUdsNrcErrc` once values are verified | M | Conformance. |

### 2.4 Logging

| # | Feature | Effort | Why |
|---|---|---|---|
| L1 | **DLT remote sink** (DLT protocol v1 over TCP 3490, verbose mode) | M | `LogMode::kRemote` is accepted and ignored today; with it DLT Viewer works out of the box. |
| L2 | Runtime log-level control (DLT control messages + dashboard) and per-context levels from the manifest | S | |
| L3 | Migrate EM/SM/SD/UCM/OTA from `std::cout` to `ara::log`; delete `Log_trace` and the P7 legacy library | M | One log stream for the whole platform. |

### 2.5 Security

| # | Feature | Effort | Why |
|---|---|---|---|
| S1 | **`ara::idsm`** (new cluster): security event definitions, reporting API, filter chain (state, sampling, aggregation, threshold), qualified events to log/per sinks | M | Already first in the roadmap order; small and well specified. |
| S2 | Emit security events from existing clusters: diag SecurityAccess failures, key-slot tamper, UCM authentication failure, tsync leap, com SD reboot | S | Makes IDSM meaningful immediately. |
| S3 | **Signed software packages**: UCM authentication hook verifies an Ed25519/ECDSA signature over the package manifest using an `ara::crypto` key slot | M | `crypto` and `ucm` do not reference each other today; this is the natural first consumer. |
| S4 | Encrypted persistency: optional AES-GCM at rest for a key-value/file storage, keyed from a key slot | M | Second crypto consumer; `per` redundancy already exists. |
| S5 | SWS object model for crypto (`CryptoProvider`, contexts) and an X.509 provider facade over the existing PKI code | L | Conformance. |
| S6 | Diag authentication service 0x29 with certificates | M | Builds on S5. |
| S7 | `ara::fw` firewall rule model + nftables back-end | L | Later; needs C3/C5 for service-aware rules. |

### 2.6 Network management and time sync

| # | Feature | Effort | Why |
|---|---|---|---|
| N1 | NM bus drivers: UDP multicast (UdpNm) and SocketCAN `vcan` (CanNm), behind a `NmTransport` interface | M | Lets the three demo ECUs run as three processes. |
| N2 | NM coordinator | M | Listed gap. |
| T1 | Time providers: system clock / PHC (linuxptp `ptp4l` via the PTP hardware clock) and an NTP-style UDP provider | M | First real time source. |
| T2 | SWS facade naming (`SynchronizedTimeBaseConsumer`, `OffsetTimeBase`), multi time-base registry | M | Conformance. |

### 2.7 Persistency and update

| # | Feature | Effort | Why |
|---|---|---|---|
| P1 | Persistency update handling: data migration and rollback of storages on a UCM update | M | Ties `per` to the update flow; currently independent. |
| U1 | `GetSwClusterChangeInfo`, `GetSwClusterManifestInfo`, cancelling (00297/00298), delta packages | M | Listed UCM gaps. |
| U2 | Put the UART/Classic-ECU flashing behind an interface outside `packagemanagement_skeleton.cpp` | S | Decouples UCM from one piece of hardware. |
| U3 | A/B install slots with boot-count based automatic rollback | M | Robust rollback story for the demo. |
| U4 | `ara::vucm` light: campaign across two UCM instances | L | After C1 to C3 and S3. |

### 2.8 Small new clusters

* **RawDataStream** (S/M): thin, spec is short, useful for sensor-stream demos.
* **Remote Persistency**, **OSI**: defer until com is modernised.

## 3. Applications to extend

### 3.1 Platform processes (largest gap)

| App | Extension |
|---|---|
| `Adaptive_Platform` (EM) | E1 to E4; `--validate` mode for manifests; structured status socket so tools need not parse log files. |
| `sm_process` | Use the new `StateMachine`/`StateManager` model as the only logic; instantiate the `UpdateRequest` skeleton as a SOME/IP service (listed gap); `ResetMachineNotifier`; receive PHM recovery requests (E6). |
| `SD` | C3 registry; structured SD event stream for the dashboard. |
| `UCM_SERVER` | S3 signature check, U1 to U3, persist state through `ara::per` instead of ad-hoc files. |
| `ota_process` + `Cloud/server` | Replace the raw socket protocol with HTTPS + a signed JSON package index, resume of interrupted downloads, version/dependency check; modern Dockerfile; unit tests for `src/OTA`. |

### 3.2 `ap_dashboard`

* Server-sent events (or WebSocket) instead of interval polling; the HTTP server is currently one thread per
  connection with `Connection: close`.
* New cards as features land: **SOME/IP service discovery** (open item), **live log** with level control (L1/L2),
  **security events** (S1), **function-group graph** rendered from the manifests, **DoIP tester sessions** (D1).
* Optional bearer token so `--bind 0.0.0.0` is safe on a lab network.
* Split `dash_features.cpp` (746 lines) into one file per card; keep the rule of card + scenario step + screenshot.

### 3.3 Demo applications

| App | Today | Extension |
|---|---|---|
| `supervised_service` | PHM inside its own process | Start it from the EM via manifest, supervise through the PHM daemon (E5), let SM restart it (E6). |
| `uds_tester` | In-process ECU | `--doip HOST` mode against a separate `diag_ecu` process (D1); `--isotp vcan0` (D2). |
| `nm_bus_monitor` | 3 ECUs in one process, virtual clock | `--bus udp|vcan` with one process per ECU (N1); keep the simulated mode for CI. |
| `time_sync_clock` | Simulated clock | `--source system|ptp|ntp` (T1); print offset against `CLOCK_REALTIME`. |
| `ota_updater` | Local package file | Fetch from the cloud server, verify signature (S3), A/B slots (U3). |
| `persist_logger_app` | Console/file log, plain storage | DLT output (L1), encrypted storage (S4), survives an OTA with migrated data (P1). |
| `secure_vault` | Passphrase-wrapped slots, PKI demo | Provision the UCM package-signing key and the diag authentication certificate (S3, S6). |

### 3.4 New applications

1. **Reference vehicle application** (M/L): three small processes started by the EM from manifests, for example a
   *speed sensor* skeleton (event + field), a *cruise control* proxy/skeleton with a method, and an *instrument cluster*
   consumer. Each uses `log`, `per`, PHM supervision and exposes DIDs/DTCs through `diag`. This is the missing
   end-to-end showcase where every cluster is used the way an integrator would use it, and the natural integration test.
2. **`apctl`** (M): one CLI replacing ad-hoc scripts and absorbing `ap_per_tool`:
   `apctl em status|fg set`, `apctl per …`, `apctl diag send`, `apctl keys …`, `apctl log level`, `apctl sd list`.
3. **`ap_manifest`** (S/M): validate, lint and generate execution/machine manifests from the command line; replaces the
   JavaFX Manifest Builder, which lets `Tools/lib`, `Tools/jar` and the `.exe` files leave the repository.
4. **`ap_gen`** service generator (M/L): Python + templates, from a JSON/YAML service description (later an ARXML
   subset) to proxy, skeleton and type headers; replaces the JavaFX Service Generator and the hand-written
   `*_proxy.h` / `*_skeleton.h` files in `sm` and `ucm`.
5. **`diag_ecu`** (S, with D1): standalone DoIP ECU process built from the existing demo ECU.

## 4. Engineering hygiene (cheap, do alongside)

| # | Item | Effort |
|---|---|---|
| H1 | Add a `LICENSE`; refresh `doc/ROADMAP.md` to match the code and conformance reports | S |
| H2 | Remove binaries and stale files listed in section 1 (after 3.4 items 3 and 4 for the Java tools) | S |
| H3 | `.clang-format`, `.clang-tidy`, a format check in CI | S |
| H4 | libFuzzer targets for every parser: SOME/IP + SD messages, UDS requests, NM PDU, manifests, persistency files, key-slot blobs | M |
| H5 | `tsan` preset and CI job (PHM, com, key store and the dashboard are multi-threaded) | S |
| H6 | Coverage threshold per cluster + badge; upload to Codecov or publish the HTML with the docs | S |
| H7 | Move the last `ap::legacy` targets to `ap::warnings`; fix the listed legacy warnings | M |
| H8 | Fold `test_partial_processes/` and `quick_test/` into `Testing/` and presets, then delete | M |
| H9 | Dev container / Dockerfile for a one-command build; tagged releases with a `CHANGELOG.md` | S |
| H10 | One pass over the SWS error tables to settle every *Unverified* domain id and `*Errc` value | M |

## 5. Suggested order

Each phase ends with a dashboard card, a demo scenario step and a re-recorded video, as `doc/demo.md` requires.

| Phase | Theme | Items | Done when |
|---|---|---|---|
| **0** | Hygiene | H1, H2 (non-Java part), H3, H5, H9 | CI has format + TSan jobs; roadmap matches code. |
| **1** | Visible wins on new clusters | L1, L2, S1, S2, D3 | DLT Viewer shows platform logs; security events appear on a dashboard card. |
| **2** | Real diagnostics | D1, `diag_ecu`, `uds_tester --doip`, D2 | A stock Python UDS client completes the 35-check session over DoIP in CI. |
| **3** | Com backbone | C1, C2, C3, C4, SD card | SD has no CSV file; method calls return `Future`; EM/SM talk over the IPC binding. |
| **4** | Execution and health chain | E1 to E6, L3, `sm_process` rework | Killing or hanging a supervised process leads to a PHM expiry, an SM recovery and an EM restart, visible in the console. |
| **5** | Secure update | S3, S4, P1, U1 to U3, OTA + cloud rework | A tampered or unsigned package is rejected over the real cloud path; data survives an update and a rollback. |
| **6** | Reference application and tools | New apps 1 to 4, H2 (Java tools), H8 | Three-process vehicle app runs from manifests; generators replace the jars. |
| **7** | Interop and breadth | C5 to C8, N1, N2, T1, T2, H4, H10 | vsomeip interop job is green; NM and TSync run across processes. |
| **8** | Later | S5 to S7, D4, D5, U4, RawDataStream, Remote Persistency | |

Phases 1 and 2 are independent of each other and of phase 3, so they can run in parallel with it. Phase 4 depends on
C4; phase 5 depends on nothing in phase 4 except L3 for readable logs; phase 6 depends on phases 3 and 4.

## 6. First five pull requests

1. `docs/plan-and-roadmap-refresh`: this file, roadmap corrections, `LICENSE` (H1).
2. `feature/log-dlt-sink`: L1 + L2 with a live-log card.
3. `feature/idsm`: S1 + S2 with a security-events card.
4. `feature/diag-doip`: D1 + `diag_ecu` + `uds_tester --doip` and a CI job using an external Python tester.
5. `feature/com-wire-buffers-and-sd-registry`: C1 + C3 with the SD traffic card.

## 7. Open decisions for the project owner

* **License** to publish under (Apache-2.0 and MIT are the common choices for this kind of project).
* **Java tools**: retire in favour of CLI/Python generators (assumed above), or keep and move to a separate repository.
* **Classic-ECU UART flashing** in UCM: keep as an optional plug-in (assumed above) or drop.
* **Interop target** for `ara::com`: vsomeip is assumed.
