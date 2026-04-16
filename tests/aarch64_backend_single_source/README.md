# AArch64 Imported SingleSource Suite

This directory hosts the AArch64 native-backend regression/import suite built
from vendored `llvm-test-suite/SingleSource` cases.

## Main Entry Points

- `smoke/run.sh`
  Runs a curated 24-case fast lane that covers the main backend risk areas
  without paying for the full imported sweep.
- `imported_suite/run.sh`
  Runs the imported manifest end-to-end:
  `clang -> .ll -> sysycc-aarch64c -> .s -> link -> AArch64 runtime`
- `probe_next_execute_batch.sh`
  Probes the next not-yet-imported `gcc-c-torture/execute` batch without
  editing the manifest.
- `sync_upstream_cases.sh`
  Refreshes vendored snapshots from the upstream llvm-test-suite revision
  recorded in `UPSTREAM_REF.txt`.

## Default Invocation

Quick smoke:

```bash
bash tests/aarch64_backend_single_source/smoke/run.sh
```

or

```bash
make test-aarch64-single-source
```

or

```bash
make test-aarch64-single-source-smoke
```

Full imported manifest:

```bash
env SYSYCC_TEST_SKIP_BUILD=1 \
  bash tests/aarch64_backend_single_source/imported_suite/run.sh
```

or

```bash
make test-aarch64-single-source-full
```

The suite now disables the generic host heavy-tool wrapper internally because
this stage already manages long-lived clang/sysycc/qemu runs and per-case logs
directly. Output is prefixed with `[current/total]` progress.

## Manifest

- `manifest.txt` is the source of truth for imported cases.
- Current status is `1770` imported cases, all marked `PASS`.
- Each line records:
  `EXPECTATION|c_std|source_rel|argv_text|xfail_reason`
- `smoke_manifest.txt` is the fast-lane subset used by `smoke/run.sh`.
- Current smoke status is `24` representative cases, all passing.

## Case Preparation

`common.sh` contains the preparation layer used by the suite:

- source compatibility rewrites for cases that rely on GNU-only constructs not
  accepted by host clang
- builtin compatibility shims
- stdout normalization for known benign pointer/signed-zero differences

Prepared sources are emitted into
`tests/aarch64_backend_single_source/build/<case-id>/`.

## Logs and Build Artifacts

- per-case logs: `build/test_logs/aarch64_backend_single_source_<case-id>.log`
- per-case workdirs:
  `tests/aarch64_backend_single_source/build/<case-id>/`

## Useful Environment Variables

- `SYSYCC_TEST_SKIP_BUILD=1`
  Reuse the existing `build/` tree instead of rebuilding first.
- `SYSYCC_AARCH64_SINGLE_SOURCE_RUN_TIMEOUT_SECONDS`
  Override runtime timeout used for AArch64 execution.
- `SYSYCC_HOST_CLANG`
  Override the host clang binary used to emit `.ll`.
- `SYSYCC_AARCH64_CC`
  Override the AArch64 compiler/linker used for object and binary creation.
- `SYSYCC_AARCH64_SYSROOT`
  Override the AArch64 sysroot used for compile and runtime.

## Import Workflow

1. Use `probe_next_execute_batch.sh` to sample the next upstream batch.
2. Vendor the selected cases with `sync_upstream_cases.sh` or targeted copies.
3. Add them to `manifest.txt`.
4. Teach `common.sh` any required source-compat rewrites.
5. Re-run `imported_suite/run.sh` until the imported manifest is clean.

## Recommended Workflow

1. Use `smoke/run.sh`, `make test-aarch64-single-source`, or
   `run_all.sh --stage aarch64_backend_single_source` while iterating on
   backend changes.
2. Use `imported_suite/run.sh` or `make test-aarch64-single-source-full`
   before merging larger AArch64 backend changes.
3. Use `probe_next_execute_batch.sh` when expanding imported execute coverage.
