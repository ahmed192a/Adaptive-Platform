<div align="center">

# Adaptive Platform

**An open implementation of the [AUTOSAR Adaptive Platform](https://www.autosar.org/standards/adaptive-platform) on Linux** —
functional clusters (`ara::*`), platform processes, tools, and a live web console.

[![CI](https://github.com/ahmed192a/Adaptive-Platform/actions/workflows/master.yml/badge.svg)](https://github.com/ahmed192a/Adaptive-Platform/actions/workflows/master.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![CMake](https://img.shields.io/badge/CMake-%E2%89%A5%203.16-green)
![AUTOSAR](https://img.shields.io/badge/AUTOSAR%20AP-R25--11%20(partial)-orange)

<img src="doc/img/01-overview.png" alt="Adaptive Platform Console" width="900">

</div>

## Contents

[Demo](#demo) · [Quick start](#quick-start) · [The console](#the-console) · [Architecture](#architecture) · [Cluster status](#cluster-status) ·
[Building](#building) · [Testing](#testing) · [Repository layout](#repository-layout) · [Legacy scripts](#legacy-quick-test-scripts)

## Demo

[![Platform demo](doc/img/platform-demo.gif)](doc/img/platform-demo.mp4)

*Click the animation for the full-quality [mp4](doc/img/platform-demo.mp4) (about 90 s, captioned). It is recorded by an
end-to-end script against the real platform: [doc/demo.md](doc/demo.md) explains how to run and re-record it.*

| Execution Management | Health Management | Diagnostics | Persistency |
|---|---|---|---|
| <img src="doc/img/02-execution-management.png" width="230"> | <img src="doc/img/03-phm.png" width="230"> | <img src="doc/img/04-diagnostics.png" width="230"> | <img src="doc/img/05-persistency.png" width="230"> |

| Cryptography | Network Management | Time Synchronization | Diagnostics lab |
|---|---|---|---|
| <img src="doc/img/06-crypto.png" width="230"> | <img src="doc/img/07-network-management.png" width="230"> | <img src="doc/img/08-time-sync.png" width="230"> | <img src="doc/img/09-diagnostics-lab.png" width="230"> |

| Software Update | Key slots | Demo apps | |
|---|---|---|---|
| <img src="doc/img/10-software-update.png" width="230"> | <img src="doc/img/11-key-slots.png" width="230"> | <img src="doc/img/12-demo-apps.png" width="230"> | |

### Demo applications

Each app is a small stand-alone program on top of the real clusters; all of them can also be started from the console's
*Demo applications* card. Build directory shown for the `release` preset.

| App | Shows | Run |
|---|---|---|
| `supervised_service` | alive supervision, hang, expiry, recovery (`phm`, `exec`) | `build/release/App/demo/supervised_service/supervised_service --scenario` |
| `uds_tester` | scripted diagnostic session against a demo ECU (`diag`) | `build/release/App/demo/uds_tester/uds_tester` |
| `nm_bus_monitor` | 3 ECUs, partial networks, state timeline (`nm`) | `build/release/App/demo/nm_bus_monitor/nm_bus_monitor` |
| `time_sync_clock` | slew vs step, rate correction, timeout (`tsync`) | `build/release/App/demo/time_sync_clock/time_sync_clock` |
| `ota_updater` | package update with rollback (`ucm`, `sm`); `--tamper`, `--fail-verify`, `--reject-session` | `build/release/App/demo/ota_updater/ota_updater` |
| `persist_logger_app` | logging levels, boot counter, file recovery (`log`, `per`) | `build/release/App/demo/persist_logger_app/persist_logger_app --dir /tmp/pla` |

## Quick start

```bash
git clone https://github.com/ahmed192a/Adaptive-Platform && cd Adaptive-Platform
cmake --preset debug                 # needs CMake ≥ 3.21 for presets; or: cmake -S . -B build
cmake --build --preset debug -j
ctest --preset debug                 # ~170 tests

# run the platform with its web console
./build/debug/App/dashboard/ap_dashboard --port 8080      # then open http://127.0.0.1:8080
```

Requirements: a C++17 compiler (GCC ≥ 9 or Clang ≥ 10), CMake, Ninja (for presets) and OpenSSL
development files (optional — `ara::crypto` is skipped without them). GoogleTest, nlohmann/json, spdlog and
CLI11 are fetched automatically (`-DAP_USE_SYSTEM_DEPS=ON` prefers installed copies).

## The console

`ap_dashboard` hosts the new clusters in one process, drives the real **Execution Management** (which starts the
SD / SM / UCM / OTA processes from the manifests) and serves a single-page console.
See **[doc/dashboard.md](doc/dashboard.md)** for the walkthrough, screenshots and the demo video.

Cards: Execution Management, Health Management, Diagnostics, Persistency, Cryptography, Network Management, Time
Synchronization, a diagnostics lab (security access, DTC memory), Software Update, Key slots and the demo-app launcher.
Run and record instructions: **[doc/demo.md](doc/demo.md)**.

## Architecture

```mermaid
flowchart TB
  subgraph Apps["Applications & tools"]
    DASH["ap_dashboard<br/>(web console)"]
    TOOL["ap_per_tool<br/>(persistency CLI)"]
    DEMO["phm_demo"]
    PROC["EM · SM · SD · UCM · OTA<br/>(platform processes)"]
  end
  subgraph AP["Functional clusters  (src/ara)"]
    EXEC["exec"] --- PHM["phm"] --- DIAG["diag"]
    PER["per"] --- CRYPTO["crypto"] --- LOG["log"]
    COM["com (SOME/IP)"] --- SM["sm"] --- UCM["ucm"]
  end
  CORE["ara::core<br/>Result · Future · ErrorCode · InstanceSpecifier"]
  DASH --> PHM & DIAG & PER & CRYPTO & LOG
  TOOL --> PER
  DEMO --> PHM
  PROC --> EXEC & COM & SM & UCM
  AP --> CORE
```

Design rules for the new clusters: exception-free `ara::core::Result` APIs, injected clocks for deterministic
tests, no global state beyond what the spec requires (lifecycle hooks run in `ara::core::Initialize()`),
strict warnings (`-Werror`) and unit tests with every cluster.

## Cluster status

The *State* column describes implementation maturity only. Whether individual requirements are met is decided per item
in the [spec-conformance reports](#spec-conformance); *Unverified* items there are not claims of compliance.

| Cluster | State | Highlights |
|---|---|---|
| `ara::core` | ✅ rewritten | `Result`, `Future/Promise`, `ErrorCode/Domain`, `InstanceSpecifier`, init/deinit hooks |
| `ara::log` | ✅ new | `Logger`, `LogStream`, `Hex/Bin`, console/file sinks |
| `ara::per` | ✅ new | key-value + file storage, CRC-32, atomic persist with backup, quotas, JSON deployment config |
| `ara::phm` | ✅ new | alive / deadline / logical supervision, global supervision, recovery actions, watchdog escalation |
| `ara::diag` | 🟡 UDS core | session layer, S3 timer, `0x10 0x11 0x22 0x2E 0x3E`, DIDs, custom services |
| `ara::nm` | ✅ new | NM state machine, partial networks, NM PDU codec, injectable clock ([doc](doc/nm.md)) |
| `ara::crypto` | 🟡 primitives + key slots | RNG, SHA-2, HMAC, AES-GCM, Ed25519, ECDSA-P256, zeroizing keys, usage-enforced key slots persisted (encrypted) via `ara::per` |
| `ara::tsync` | 🟡 time base | synchronized time base with injectable clock, rate deviation, offset slew/step, timeout detection, status flags, update callback |
| `ara::exec` | 🟡 hardened | safe manifest parsing, client error reporting; EM process uses it |
| `ara::com` | 🟠 legacy | SOME/IP + service discovery |
| `ara::sm`, `ara::ucm` | 🟠 legacy | state management, update & configuration management |
| IDSM, Firewall, … | ⬜ planned | see [doc/ROADMAP.md](doc/ROADMAP.md) |

### Spec conformance

Every cluster has a requirement-by-requirement report in [`doc/spec-conformance/`](doc/spec-conformance/)
([core](doc/spec-conformance/core.md), [log](doc/spec-conformance/log.md), [per](doc/spec-conformance/per.md),
[phm](doc/spec-conformance/phm.md), [exec](doc/spec-conformance/exec.md), [diag](doc/spec-conformance/diag.md),
[crypto](doc/spec-conformance/crypto.md), [nm](doc/spec-conformance/nm.md), [tsync](doc/spec-conformance/tsync.md),
[sm](doc/spec-conformance/sm.md), [ucm](doc/spec-conformance/ucm.md), [com](doc/spec-conformance/com.md)). Each item is marked *Implemented*, *Partial*,
*Missing*, *Deviation* or *Unverified*. **Unverified** means the AUTOSAR text for that item (often the API-signature
chapter or numeric error values) could not be read when the audit was done; it is not a claim of compliance.

The detailed gap analysis against AUTOSAR R25-11, known deviations and next steps are in
**[doc/ROADMAP.md](doc/ROADMAP.md)**.

## Building

| Preset | Purpose |
|---|---|
| `debug` / `release` | everyday builds |
| `asan` | AddressSanitizer + UBSan |
| `coverage` | gcov instrumentation (used by CI) |
| `ci` | Release with warnings as errors |

| Option | Default | Meaning |
|---|---|---|
| `AP_BUILD_TESTS` / `APPS` / `TOOLS` / `EXAMPLES` | ON (top level) | what to build |
| `AP_BUILD_CRYPTO` | ON | build `ara::crypto` when OpenSSL is found |
| `AP_BUILD_SHARED` | ON | shared vs static cluster libraries |
| `AP_ENABLE_SANITIZERS` / `AP_ENABLE_COVERAGE` | OFF | instrumentation |
| `AP_WARNINGS_AS_ERRORS` | ON | `-Werror` for new/migrated clusters |
| `AP_USE_SYSTEM_DEPS` | OFF | prefer `find_package` over downloads |
| `AP_INSTALL` | ON (top level) | generate install rules and the CMake package |

Use it from another project:

```cmake
find_package(AdaptivePlatform REQUIRED)
target_link_libraries(my_app PRIVATE AdaptivePlatform::core AdaptivePlatform::phm AdaptivePlatform::per)
```

Add a new cluster: create `src/ara/<name>/CMakeLists.txt` containing
`ap_add_cluster(NAME <name> DEPENDS ara_core_lib)`, tests in `Testing/<name>/` with `ap_add_test(...)`.

## Testing

```bash
ctest --preset debug                    # everything
ctest --preset debug -L phm             # one cluster (labels: core log per phm diag nm tsync crypto exec dashboard legacy)
ctest --preset asan                     # sanitizers
```

CI (`.github/workflows/master.yml`) builds with GCC and Clang, runs the sanitizer and coverage jobs, runs the
end-to-end console scenario in a browser (screenshots and video are uploaded as artifacts) and publishes the
Doxygen documentation from `master`.

## Repository layout

```
src/ara/<cluster>/    functional clusters (core log per phm diag crypto exec com sm ucm …)
App/                  platform processes (EM, SM, SD, UCM, OTA), examples, dashboard
Tools/                command line tools (ap_per_tool), generators, demo recorder
Testing/              unit tests per cluster
cmake/                build system modules (options, warnings, dependencies, clusters)
doc/                  roadmap, dashboard guide, screenshots, Doxygen configuration
```

## Legacy quick-test scripts

The original helper scripts still work (`quick_test/`): `./configure.sh`, `./build.sh`, `./run.sh`, `./test.sh`,
`./sd_test.sh`. They use the same CMake project underneath.
