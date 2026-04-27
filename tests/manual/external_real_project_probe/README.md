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
make real-project-compile-times
make real-project-compile-times TEST_ARGS=lua
make real-project-compile-times TEST_ARGS=mujs
```

- `lua_smoke.sh` reuses an already-built SysyCC Lua binary and runs the current
  behavior smoke only: table operations, closures, coroutines, `io.tmpfile`,
  `string.pack/unpack`, `string.dump/load`, and `-0.0`.
- `profile_compile_times.sh` wraps `CC` while building Lua and/or MuJS with
  `-j1`, records every SysyCC compile invocation, and writes sorted Markdown
  reports under `build/external-real-project-probe/reports/`.

This probe is intentionally manual instead of tier1/tier2: it depends on Docker,
network access, package installation, and external repositories.
