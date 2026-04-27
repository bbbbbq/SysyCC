# External Real-Project Probe

This manual probe checks whether SysyCC can act as `CC` for two real C
projects:

- Lua, cloned from `https://github.com/lua/lua.git`
- MuJS, cloned from `https://github.com/ccxvii/mujs.git` when that mirror has
  sources, otherwise from the upstream Codeberg repository referenced by the
  GitHub mirror

Run it manually with:

```bash
tests/manual/external_real_project_probe/verify.sh
```

By default the script reuses the existing `qemu_dev` container and configures a
dedicated `/code/SysyCC/build-qemu-real-project-probe` Ninja build tree. If that
container is not present, it can fall back to creating a disposable Ubuntu 24.04
image with the required build tools. Inside the container, it builds SysyCC,
clones the external projects under `build/external-real-project-probe/repos`,
and then uses the container-built SysyCC as `CC`.

Useful overrides:

```bash
SYSYCC_REAL_PROJECT_DOCKER_CONTAINER=qemu_dev \
SYSYCC_REAL_PROJECT_DOCKER_PROJECT_ROOT=/code/SysyCC \
SYSYCC_REAL_PROJECT_DOCKER_BUILD_DIR=/code/SysyCC/build-qemu-real-project-probe \
SYSYCC_REAL_PROJECT_LLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm \
tests/manual/external_real_project_probe/verify.sh
```

Expected checks:

- Lua builds with its Linux Makefile defaults and runs `print("lua-ok", 21 + 12)`.
- MuJS builds its `release` target with `HAVE_READLINE=no` and runs a small
  JavaScript smoke that prints `mujs-ok 33`.

Fast iteration helpers:

```bash
make lua-smoke
make lua-incremental
make lua-incremental TEST_ARGS="lvm.c"
make lua-incremental TEST_ARGS="lapi.c ltable.c"
make pass-report-diff TEST_ARGS="before.md after.md"
make real-project-compile-times
make real-project-compile-times TEST_ARGS=lua
make real-project-compile-times TEST_ARGS=mujs
```

- `lua_smoke.sh` reuses an already-built SysyCC Lua binary and runs the current
  behavior smoke only: table operations, closures, coroutines, `io.tmpfile`,
  `string.pack/unpack`, `string.dump/load`, and `-0.0`.
- `incremental_lua.sh` reuses the existing Lua worktree and does not run
  `make clean`. It removes only the requested source objects, rebuilds/relinks
  Lua through SysyCC, and then runs `lua_smoke.sh`. If no source is passed, it
  first uses dirty Lua `.c` files; if none are dirty, it defaults to `lvm.c`
  because that is the current compile-time hotspot. Set
  `SYSYCC_LUA_INCREMENTAL_DEFAULT_SOURCE=` to let the Lua Makefile rebuild only
  sources it already considers out of date. When a previous pass report exists
  for the same TU, it also writes before/after diffs under
  `lua_incremental_pass_report_diffs/`.
- The compile-time helpers default to `RelWithDebInfo` via
  `SYSYCC_REAL_PROJECT_BUILD_TYPE=RelWithDebInfo`, so reported timings reflect
  an optimized SysyCC binary while preserving symbols for CPU profilers such as
  gperftools, `perf`, or VTune-style sampling tools. Override the variable only
  when intentionally comparing Debug/Release build modes.
- `diff_pass_reports.py` compares two per-TU pass reports and summarizes
  pipeline wall time, final IR size, top pass time changes, and fixed-point
  iteration changes as Markdown.
- `profile_compile_times.sh` wraps `CC` while building Lua and/or MuJS with
  `-j1`, records every SysyCC compile invocation, and writes sorted Markdown
  reports under `build/external-real-project-probe/reports/`. It also enables
  `SYSYCC_PASS_REPORT_DIR` so each translated `.c` gets a pass-level Markdown
  report under `lua_pass_reports/` or `mujs_pass_reports/`.

This probe is intentionally manual instead of tier1/tier2: it depends on Docker,
network access, package installation, and external repositories.
