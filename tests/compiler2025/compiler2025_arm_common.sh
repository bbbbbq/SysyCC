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

    set +e
    if [[ -n "${input_file}" && -f "${input_file}" ]]; then
        "${program_binary}" <"${input_file}" >"${actual_output_file}" \
            2>"${stderr_log_file}"
    else
        "${program_binary}" >"${actual_output_file}" 2>"${stderr_log_file}"
    fi
    COMPILER2025_LAST_EXIT_CODE=$?
    set -e

    compiler2025_append_exit_code "${actual_output_file}" \
        "${COMPILER2025_LAST_EXIT_CODE}"
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

compiler2025_measure_binary_runtime() {
    local program_binary="$1"
    local input_file="$2"
    local warmup_count="$3"
    local iteration_count="$4"
    local expected_exit_code="$5"
    local output_json_file="$6"

    python3 - "${program_binary}" "${input_file}" "${warmup_count}" \
        "${iteration_count}" "${expected_exit_code}" "${output_json_file}" <<'PY'
from __future__ import annotations

import json
import os
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

if iterations <= 0:
    raise SystemExit("iterations must be > 0")
if warmup < 0:
    raise SystemExit("warmup must be >= 0")

def run_once() -> float:
    stdin = None
    if input_file is not None and input_file.is_file():
        stdin = input_file.open("rb")
    devnull = open(os.devnull, "wb")
    start = time.perf_counter()
    try:
        completed = subprocess.run(
            [str(program)],
            stdin=stdin,
            stdout=devnull,
            stderr=devnull,
            check=False,
        )
    finally:
        end = time.perf_counter()
        devnull.close()
        if stdin is not None:
            stdin.close()
    if completed.returncode != expected_exit:
        raise SystemExit(
            f"unexpected exit code from {program}: "
            f"{completed.returncode} != {expected_exit}"
        )
    return end - start

for _ in range(warmup):
    run_once()

samples = [run_once() for _ in range(iterations)]
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
