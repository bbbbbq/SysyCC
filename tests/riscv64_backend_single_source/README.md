# RISC-V64 Imported SingleSource Suite

This directory hosts a first RISC-V64 differential harness built from the
vendored `llvm-test-suite/SingleSource` snapshot that already lives under
[tests/aarch64_backend_single_source/upstream](/Users/caojunze424/code/SysyCC/tests/aarch64_backend_single_source/upstream).

## Scope

- source pool: single-file C cases discovered from the vendored `SingleSource`
  snapshot by default, with an optional pinned manifest fallback
- batching: default `10` cases per batch
- batching: default `100` cases per batch
- pipeline:
  `clang source -> riscv64 baseline binary`
  and
  `clang source -> .ll -> sysycc-riscv64c -> riscv64 object -> riscv64 binary`
- comparison: stdout, stderr, and exit status

## Entry Point

```bash
bash tests/riscv64_backend_single_source/batch/run.sh
```

Useful overrides:

```bash
SYSYCC_RISCV64_SINGLE_SOURCE_BATCH_INDEX=2 \
SYSYCC_RISCV64_SINGLE_SOURCE_BATCH_SIZE=100 \
bash tests/riscv64_backend_single_source/batch/run.sh
```

List the selected batch without running it:

```bash
SYSYCC_RISCV64_SINGLE_SOURCE_LIST_ONLY=1 \
bash tests/riscv64_backend_single_source/batch/run.sh
```

## Discovery And Manifest

- By default the batch runner auto-discovers candidate cases from the vendored
  `SingleSource` tree:
  - keep `.c` files
  - skip `SingleSource/Support`
  - skip `*-lib.c`
  - skip sources that have a sibling `-lib.c`
  - skip architecture-specific AArch64 / ARM directories
- `manifest.txt` remains available as a pinned fallback list.
- line format:
  `c_std|source_rel|argv_text`
- set `SYSYCC_RISCV64_SINGLE_SOURCE_DISCOVER=0` to force the runner to consume
  `manifest.txt` instead of scanning the vendored tree.

## Environment

- `SYSYCC_HOST_CLANG`
  Override the host clang used for baseline binaries and `.ll` emission.
  The runner prefers `/opt/homebrew/opt/llvm/bin/clang` automatically when it
  exists, because Apple clang does not provide the RISC-V targets we need.
- `SYSYCC_RISCV64_SYSROOT`
  Point clang/qemu at a Linux RISC-V64 sysroot.
- `SYSYCC_RISCV64_GCC_TOOLCHAIN`
  Optional clang `--gcc-toolchain=...` override.
- `SYSYCC_QEMU_RISCV64`
  Override the `qemu-riscv64` user-mode runner.
- `SYSYCC_RISCV64_DOCKER_IMAGE`
  Override the Docker image used for the Linux `qemu-riscv64` fallback path.
- `SYSYCC_RISCV64_SINGLE_SOURCE_DISCOVER=0`
  Disable auto-discovery and use the pinned manifest instead.
- `SYSYCC_RISCV64_SINGLE_SOURCE_BATCH_INDEX`
  1-based batch selector.
- `SYSYCC_RISCV64_SINGLE_SOURCE_BATCH_SIZE`
  Batch size, defaults to `100`.
- `SYSYCC_RISCV64_SINGLE_SOURCE_START_INDEX`
  1-based absolute start position used before batch slicing. This is useful for
  continuing from a previously verified prefix without re-running overlap.
- `SYSYCC_RISCV64_SINGLE_SOURCE_LIST_ONLY=1`
  Print the selected cases and exit.

## Notes

- The harness deliberately reuses the vendored upstream snapshot and source
  compatibility rewrites from the AArch64 suite instead of copying a second
  upstream tree into the repository.
- The preferred runtime path is local `qemu-riscv64` user-mode plus a Linux
  RISC-V64 sysroot.
- On macOS hosts where local linux-user QEMU is unavailable, the runner now
  auto-builds [Dockerfile.user](/Users/caojunze424/code/SysyCC/tests/riscv64_backend_single_source/Dockerfile.user)
  and uses containerized `clang + qemu-riscv64 + riscv64-linux-gnu` packages as
  a fallback execution environment.
- The current proven baseline is:
  - batch 1: 10/10 pass
  - batch 2: 10/10 pass
