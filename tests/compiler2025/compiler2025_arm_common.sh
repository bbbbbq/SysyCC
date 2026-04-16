#!/usr/bin/env bash

compiler2025_arm_default_case_root() {
    printf '%s\n' "${SYSYCC_COMPILER2025_ARM_CASE_ROOT:-${SCRIPT_DIR}/extracted/ARM-性能}"
}

compiler2025_resolve_case_root() {
    local requested_root="${1:-}"
    local case_root="${requested_root}"

    if [[ -z "${case_root}" ]]; then
        case_root="$(compiler2025_arm_default_case_root)"
    fi

    if [[ ! -d "${case_root}" ]]; then
        echo "missing case directory: ${case_root}" >&2
        echo "hint: pass --case-root or set SYSYCC_COMPILER2025_ARM_CASE_ROOT" >&2
        return 1
    fi

    printf '%s\n' "${case_root}"
}

compiler2025_case_matches_filter() {
    local case_name="$1"
    shift

    if [[ "$#" -eq 0 ]]; then
        return 0
    fi

    local selected_case=""
    for selected_case in "$@"; do
        if [[ "${case_name}" == "${selected_case}" ]]; then
            return 0
        fi
    done
    return 1
}

compiler2025_append_exit_code() {
    local output_file="$1"
    local exit_code="$2"

    python3 - "${output_file}" "${exit_code}" <<'PY'
from pathlib import Path
import sys

output_path = Path(sys.argv[1])
exit_code = sys.argv[2]
content = output_path.read_text(errors="replace")
if content and not content.endswith("\n"):
    content += "\n"
content += f"{exit_code}\n"
output_path.write_text(content)
PY
}

compiler2025_compare_expected_output() {
    local expected_output_file="$1"
    local actual_output_file="$2"
    local diff_log_file="$3"

    python3 - "${expected_output_file}" "${actual_output_file}" \
        >"${diff_log_file}" 2>&1 <<'PY'
from pathlib import Path
import difflib
import sys

expected_path = Path(sys.argv[1])
actual_path = Path(sys.argv[2])
expected = expected_path.read_text(errors="replace").replace("\r\n", "\n").replace("\r", "\n")
actual = actual_path.read_text(errors="replace").replace("\r\n", "\n").replace("\r", "\n")

# Treat a final trailing newline difference as formatting-only noise.
expected_compare = expected.rstrip("\n")
actual_compare = actual.rstrip("\n")

if expected_compare != actual_compare:
    diff = difflib.unified_diff(
        expected.splitlines(True),
        actual.splitlines(True),
        fromfile=str(expected_path),
        tofile=str(actual_path),
    )
    sys.stdout.writelines(diff)
    sys.exit(1)
PY
}

compiler2025_run_binary_capture() {
    local program_binary="$1"
    local input_file="$2"
    local actual_output_file="$3"
    local stderr_log_file="$4"
    local timeout_seconds="${5:-0}"
    local return_code=""
    local status=0

    COMPILER2025_LAST_RUN_STATUS="ok"
    set +e
    return_code="$({
        python3 - "${program_binary}" "${input_file}" "${actual_output_file}" \
            "${stderr_log_file}" "${timeout_seconds}" <<'PY'
from __future__ import annotations

import os
import signal
import subprocess
import sys
from pathlib import Path

program = Path(sys.argv[1])
input_file = Path(sys.argv[2]) if sys.argv[2] else None
actual_output_file = Path(sys.argv[3])
stderr_log_file = Path(sys.argv[4])
timeout_seconds = int(sys.argv[5])

stdin = input_file.open("rb") if input_file is not None and input_file.is_file() else None
try:
    process = None
    try:
        process = subprocess.Popen(
            [str(program)],
            stdin=stdin,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )
        stdout_data, stderr_data = process.communicate(
            timeout=None if timeout_seconds <= 0 else timeout_seconds,
        )
    except subprocess.TimeoutExpired as exc:
        partial_stdout = exc.stdout or b""
        partial_stderr = exc.stderr or b""
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
        newline = b"" if not partial_stdout or partial_stdout.endswith(b"\n") else b"\n"
        actual_output_file.write_bytes(partial_stdout + newline)
        timeout_message = (
            f"timed out after {timeout_seconds}s while running {program}\n".encode()
        )
        stderr_newline = b"" if not partial_stderr or partial_stderr.endswith(b"\n") else b"\n"
        stderr_log_file.write_bytes(partial_stderr + stderr_newline + timeout_message)
        raise SystemExit(124)
finally:
    if stdin is not None:
        stdin.close()

stdout = stdout_data or b""
new_line = b"" if not stdout or stdout.endswith(b"\n") else b"\n"
actual_output_file.write_bytes(stdout + new_line + f"{process.returncode}\n".encode())
stderr_log_file.write_bytes(stderr_data or b"")
print(process.returncode)
PY
    })"
    status=$?
    set -e

    if [[ "${status}" -ne 0 ]]; then
        COMPILER2025_LAST_EXIT_CODE="${status}"
        if [[ "${status}" -eq 124 ]]; then
            COMPILER2025_LAST_RUN_STATUS="timeout"
        else
            COMPILER2025_LAST_RUN_STATUS="error"
        fi
        return "${status}"
    fi

    COMPILER2025_LAST_EXIT_CODE="${return_code}"
}

compiler2025_compile_runtime_ir() {
    local compiler_bin="$1"
    local runtime_source="$2"
    local ir_output_dir="$3"
    local compile_log_file="$4"
    local runtime_ir_file="${ir_output_dir}/$(basename "${runtime_source}" .c).ll"

    if ! "${compiler_bin}" "${runtime_source}" --dump-ir >"${compile_log_file}" 2>&1; then
        echo "failed to compile runtime with SysyCC, see ${compile_log_file}" >&2
        return 1
    fi

    if [[ ! -f "${runtime_ir_file}" || ! -s "${runtime_ir_file}" ]]; then
        echo "missing runtime IR output: ${runtime_ir_file}" >&2
        return 1
    fi

    printf '%s\n' "${runtime_ir_file}"
}

compiler2025_require_aarch64_execution_stack() {
    local cc=""
    local qemu=""
    local sysroot=""

    cc="$(find_aarch64_cc 2>/dev/null || true)"
    if [[ -z "${cc}" ]]; then
        echo "missing AArch64 cross compiler; set SYSYCC_AARCH64_CC or install a supported toolchain" >&2
        return 1
    fi

    sysroot="$(find_aarch64_sysroot 2>/dev/null || true)"
    if [[ -z "${sysroot}" ]]; then
        echo "missing AArch64 sysroot; set SYSYCC_AARCH64_SYSROOT or install a supported sysroot" >&2
        return 1
    fi

    qemu="$(find_qemu_aarch64 2>/dev/null || true)"
    if [[ -z "${qemu}" ]]; then
        echo "missing qemu-aarch64; set SYSYCC_QEMU_AARCH64 or install qemu user-mode" >&2
        return 1
    fi
}

compiler2025_compile_aarch64_c_object() {
    local source_file="$1"
    local object_file="$2"
    local compile_log_file="$3"
    local cc=""

    cc="$(find_aarch64_cc 2>/dev/null || true)"
    if [[ -z "${cc}" ]]; then
        echo "missing AArch64 cross compiler" >&2
        return 1
    fi

    mkdir -p "$(dirname "${object_file}")"
    if ! run_aarch64_cc "${cc}" -c "${source_file}" -fno-builtin -o "${object_file}" \
        >"${compile_log_file}" 2>&1; then
        echo "failed to compile support object ${source_file}, see ${compile_log_file}" >&2
        return 1
    fi

    if [[ ! -f "${object_file}" || ! -s "${object_file}" ]]; then
        echo "missing AArch64 support object: ${object_file}" >&2
        return 1
    fi
}

compiler2025_run_aarch64_binary_capture() {
    local program_binary="$1"
    local input_file="$2"
    local actual_output_file="$3"
    local stderr_log_file="$4"
    local qemu=""
    local sysroot=""

    qemu="$(find_qemu_aarch64 2>/dev/null || true)"
    sysroot="$(find_aarch64_sysroot 2>/dev/null || true)"
    if [[ -z "${qemu}" || -z "${sysroot}" ]]; then
        echo "missing qemu/sysroot for AArch64 execution" >&2
        return 1
    fi

    set +e
    if [[ -n "${input_file}" && -f "${input_file}" ]]; then
        QEMU_LD_PREFIX="${sysroot}" "${qemu}" -L "${sysroot}" "${program_binary}" \
            <"${input_file}" >"${actual_output_file}" 2>"${stderr_log_file}"
    else
        QEMU_LD_PREFIX="${sysroot}" "${qemu}" -L "${sysroot}" "${program_binary}" \
            >"${actual_output_file}" 2>"${stderr_log_file}"
    fi
    COMPILER2025_LAST_EXIT_CODE=$?
    set -e

    compiler2025_append_exit_code "${actual_output_file}" \
        "${COMPILER2025_LAST_EXIT_CODE}"
}

compiler2025_measure_binary_runtime() {
    local program_binary="$1"
    local input_file="$2"
    local warmup_count="$3"
    local iteration_count="$4"
    local expected_exit_code="$5"
    local output_json_file="$6"
    local timeout_seconds="${7:-0}"

    python3 - "${program_binary}" "${input_file}" "${warmup_count}" \
        "${iteration_count}" "${expected_exit_code}" "${output_json_file}" \
        "${timeout_seconds}" <<'PY'
from __future__ import annotations

import json
import os
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path

program = Path(sys.argv[1])
input_file = Path(sys.argv[2]) if sys.argv[2] else None
warmup = int(sys.argv[3])
iterations = int(sys.argv[4])
expected_exit = int(sys.argv[5])
output_json = Path(sys.argv[6])
timeout_seconds = int(sys.argv[7])

if iterations <= 0:
    raise SystemExit("iterations must be > 0")
if warmup < 0:
    raise SystemExit("warmup must be >= 0")

def run_once(label: str) -> float:
    stdin = None
    if input_file is not None and input_file.is_file():
        stdin = input_file.open("rb")
    devnull = open(os.devnull, "wb")
    start = time.perf_counter()
    try:
        process = None
        try:
            process = subprocess.Popen(
                [str(program)],
                stdin=stdin,
                stdout=devnull,
                stderr=devnull,
                start_new_session=True,
            )
            return_code = process.wait(
                timeout=None if timeout_seconds <= 0 else timeout_seconds,
            )
        except subprocess.TimeoutExpired:
            if process is not None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                try:
                    process.wait(timeout=1.0)
                except subprocess.TimeoutExpired:
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    process.wait()
            timeout_suffix = f" after {timeout_seconds}s" if timeout_seconds > 0 else ""
            print(
                f"timed out while measuring {program} ({label}){timeout_suffix}",
                file=sys.stderr,
            )
            raise SystemExit(124)
    finally:
        end = time.perf_counter()
        devnull.close()
        if stdin is not None:
            stdin.close()
    if return_code != expected_exit:
        raise SystemExit(
            f"unexpected exit code from {program}: "
            f"{return_code} != {expected_exit}"
        )
    return end - start

for index in range(warmup):
    run_once(f"warmup {index + 1}/{warmup}")

samples = [run_once(f"sample {index + 1}/{iterations}") for index in range(iterations)]
payload = {
    "samples": samples,
    "median_seconds": statistics.median(samples),
    "mean_seconds": statistics.fmean(samples),
    "min_seconds": min(samples),
    "max_seconds": max(samples),
}
output_json.write_text(json.dumps(payload, indent=2, sort_keys=True))
PY
}

compiler2025_prepare_compiler_snapshot() {
    local compiler_bin="$1"
    local ir_output_dir="$2"
    local snapshot_root="$3"

    if [[ ! -f "${compiler_bin}" || ! -x "${compiler_bin}" ]]; then
        echo "missing compiler binary for snapshot: ${compiler_bin}" >&2
        return 1
    fi

    mkdir -p "${snapshot_root}"
    local snapshot_bin="${snapshot_root}/SysyCC.snapshot"
    local snapshot_ir_dir="${snapshot_root}/intermediate_results"
    rm -rf "${snapshot_ir_dir}"
    mkdir -p "${snapshot_ir_dir}"
    cp "${compiler_bin}" "${snapshot_bin}"
    chmod +x "${snapshot_bin}"

    COMPILER2025_SNAPSHOT_COMPILER_BIN="${snapshot_bin}"
    COMPILER2025_SNAPSHOT_IR_OUTPUT_DIR="${snapshot_ir_dir}"
    export COMPILER2025_SNAPSHOT_COMPILER_BIN
    export COMPILER2025_SNAPSHOT_IR_OUTPUT_DIR

    if [[ -d "${ir_output_dir}" ]]; then
        # Keep existing tooling that expects the directory to exist, but run the
        # actual benchmark compiles against the isolated snapshot directory above.
        :
    fi
}
