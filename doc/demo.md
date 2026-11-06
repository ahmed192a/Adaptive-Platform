# Demo: the Platform Console and the demo applications

The console (`ap_dashboard`) is a small HTTP node that drives the **real in-process `ara::` libraries**
and the real demo binaries. Nothing on screen is mocked.

## Run it

```bash
cmake --preset release && cmake --build --preset release -j
build/release/App/dashboard/ap_dashboard --port 8080 --data-dir /tmp/ap
# open http://127.0.0.1:8080   (binds to loopback by default)
```

Useful flags: `--bind ADDR`, `--platform-dir DIR` (where `Adaptive_Platform` lives), `--demo-dir DIR`
(where `<name>/<name>` demo binaries live; default `<platform-dir>/demo`), `--web-root DIR`.

## Cards

| Card | Library behind it | API |
|---|---|---|
| Execution Management, PHM, Diagnostics, Persistency, Crypto | `exec`, `phm`, `diag`, `per`, `crypto` | `/api/em`, `/api/phm`, `/api/diag`, `/api/per`, `/api/crypto` |
| Network Management (3 virtual ECUs, partial networks, virtual clock) | `ara::nm` | `/api/nm/{request,release,advance,reset}` |
| Time Synchronization (slew, step, rate correction, timeout) | `ara::tsync` | `/api/tsync/{sync,advance,reset}` |
| Diagnostics lab (security access, routines, DTC memory) | `ara::diag` demo ECU | `/api/udsx/{request,unlock,fault,advance,reset}` |
| Software Update (block transfer, rollback, tamper) | `ara::ucm` + `ara::sm` | `/api/update/run` |
| Key slots (generate, use, tamper, reload) | `ara::crypto` KeyStorageProvider | `/api/keys/*` |
| Demo applications launcher | whitelisted binaries only | `/api/apps/run` |

Every POST endpoint is unit tested in `Testing/dashboard`. The node is hardened: loopback by default, request body limit,
no path traversal, and the launcher only runs a fixed whitelist of binaries with fixed argument sets.

## Record the demo

```bash
build/release/App/dashboard/ap_dashboard --port 8099 --data-dir /tmp/ap &
pip install playwright imageio-ffmpeg          # plus a system Chrome/Chromium; ffmpeg on PATH
python3 Tools/demo/record_demo.py --url http://127.0.0.1:8099 --out doc/img --gif
```

This regenerates the screenshots, `doc/img/platform-demo.mp4` (about 75 s with captions) and
`doc/img/platform-demo.gif` (at most 8 MB). `--steps nm,tsync` runs a subset, `--list` shows the step names. The script
asserts the behaviour of every step and exits non-zero if the platform misbehaves, so CI uses it as the end-to-end test.

## Rule: when you add a feature

1. Add a **card** (HTML in `App/dashboard/web`, backend in `App/dashboard/dash_features.cpp`).
2. Add **one scenario step** (a `step_*` function in `Tools/demo/record_demo.py`) that exercises it and asserts the outcome.
3. Add **one screenshot** (`d.card("NN-name.png", "#cardX")`) and reference it in the README gallery.
4. Add unit tests for the new endpoints, then re-record the demo.
