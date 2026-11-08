# Contributing

## Build and test
```bash
cmake --preset ci && cmake --build --preset ci -j && ctest --preset ci -j4
```
Other presets: `debug`, `release`, `asan`, `coverage`. New code builds with `-Werror` (`ap::warnings`); legacy code uses `ap::legacy`.

## Conventions
- New `ara::` code returns `ara::core::Result<T, E>` (no exceptions across the public API) and has unit tests under `Testing/<cluster>`.
- When an API changes, update its report in `doc/spec-conformance/`. Mark an item *Unverified* if the specification text could not be read; never invent requirement IDs.
- User-visible features get a dashboard card, one step in `Tools/demo/record_demo.py` and a screenshot (see `doc/demo.md`).
- Work on a `feature/*` branch and open a pull request into `master`; CI must be green.
