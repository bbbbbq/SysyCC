#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${SYSYCC_COMPILER2025_BUILD_DIR:-${PROJECT_ROOT}/build}"
IR_OUTPUT_DIR="${SYSYCC_COMPILER2025_IR_OUTPUT_DIR:-${PROJECT_ROOT}/build/intermediate_results}"
CASE_BUILD_ROOT_BASE="${SCRIPT_DIR}/build/host_ir_performance"
COMPILER_BIN="${BUILD_DIR}/compiler"
RUNTIME_SOURCE="${SCRIPT_DIR}/sylib.c"
RUNTIME_HEADER="${SCRIPT_DIR}/sylib.h"
RUNTIME_BUILTIN_STUB="${PROJECT_ROOT}/tests/run/support/runtime_builtin_stub.ll"
RUNTIME_COMPAT_SOURCE="${SCRIPT_DIR}/runtime_builtin_compat.c"
RUN_STARTED_AT="$(date '+%Y-%m-%d %H:%M:%S %Z')"
CLANG_BASELINE_OPT_LEVEL="${SYSYCC_COMPILER2025_HOST_BASELINE_OPT_LEVEL:--O3}"

source "${PROJECT_ROOT}/tests/test_helpers.sh"
source "${SCRIPT_DIR}/compiler2025_arm_common.sh"

require_positive_integer() {
    local value="$1"
    local option_name="$2"

    if [[ ! "${value}" =~ ^[1-9][0-9]*$ ]]; then
        echo "${option_name} must be a positive integer, got '${value}'" >&2
        exit 1
    fi
}

usage() {
    cat <<'EOF'
Usage:
  ./tests/compiler2025/run_host_ir_performance.sh [--case-root path] [--report path]
      [--json-report path] [--iterations N] [--warmup N]
      [--compile-timeout N] [--run-timeout N] [case_name ...]

Examples:
  ./tests/compiler2025/run_host_ir_performance.sh
  ./tests/compiler2025/run_host_ir_performance.sh gameoflife-gosper
  ./tests/compiler2025/run_host_ir_performance.sh --iterations 3 --warmup 1 01_mm1 01_mm2
EOF
}

CASE_ROOT=""
REPORT_FILE=""
JSON_REPORT_FILE=""
SELECTED_CASES=()
ITERATION_COUNT="${SYSYCC_COMPILER2025_HOST_PERF_ITERATIONS:-1}"
WARMUP_COUNT="${SYSYCC_COMPILER2025_HOST_PERF_WARMUP:-0}"
COMPILE_TIMEOUT_SECONDS="${SYSYCC_COMPILER2025_HOST_PERF_COMPILE_TIMEOUT:-15}"
RUN_TIMEOUT_SECONDS="${SYSYCC_COMPILER2025_HOST_PERF_RUN_TIMEOUT:-20}"

while [[ $# -gt 0 ]]; do
    case "$1" in
    --case-root)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --case-root" >&2
            exit 1
        fi
        CASE_ROOT="$2"
        shift 2
        ;;
    --report)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --report" >&2
            exit 1
        fi
        REPORT_FILE="$2"
        shift 2
        ;;
    --json-report)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --json-report" >&2
            exit 1
        fi
        JSON_REPORT_FILE="$2"
        shift 2
        ;;
    --iterations)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --iterations" >&2
            exit 1
        fi
        ITERATION_COUNT="$2"
        shift 2
        ;;
    --warmup)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --warmup" >&2
            exit 1
        fi
        WARMUP_COUNT="$2"
        shift 2
        ;;
    --compile-timeout)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --compile-timeout" >&2
            exit 1
        fi
        COMPILE_TIMEOUT_SECONDS="$2"
        shift 2
        ;;
    --run-timeout)
        if [[ $# -lt 2 ]]; then
            echo "missing value for --run-timeout" >&2
            exit 1
        fi
        RUN_TIMEOUT_SECONDS="$2"
        shift 2
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    --)
        shift
        while [[ $# -gt 0 ]]; do
            SELECTED_CASES+=("$1")
            shift
        done
        ;;
    *)
        SELECTED_CASES+=("$1")
        shift
        ;;
    esac
done

CASE_ROOT="$(compiler2025_resolve_case_root "${CASE_ROOT}")"
require_positive_integer "${ITERATION_COUNT}" "--iterations"
if [[ ! "${WARMUP_COUNT}" =~ ^[0-9]+$ ]]; then
    echo "--warmup must be a non-negative integer" >&2
    exit 1
fi
require_positive_integer "${COMPILE_TIMEOUT_SECONDS}" "--compile-timeout"
require_positive_integer "${RUN_TIMEOUT_SECONDS}" "--run-timeout"

if [[ -z "${REPORT_FILE}" ]]; then
    REPORT_FILE="${CASE_BUILD_ROOT_BASE}/compiler2025_host_ir_performance_result.md"
fi
if [[ -z "${JSON_REPORT_FILE}" ]]; then
    JSON_REPORT_FILE="${CASE_BUILD_ROOT_BASE}/compiler2025_host_ir_performance_result.json"
fi

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_ROOT_BASE}"
export SYSYCC_INTERMEDIATE_RESULTS_DIR="${IR_OUTPUT_DIR}"

PYTHON_ARGS=(
    -
    "${CASE_ROOT}"
    "${REPORT_FILE}"
    "${JSON_REPORT_FILE}"
    "${COMPILER_BIN}"
    "${PROJECT_ROOT}"
    "${IR_OUTPUT_DIR}"
    "${ITERATION_COUNT}"
    "${WARMUP_COUNT}"
    "${COMPILE_TIMEOUT_SECONDS}"
    "${RUN_TIMEOUT_SECONDS}"
    "${CLANG_BASELINE_OPT_LEVEL}"
    "${RUN_STARTED_AT}"
)
if [[ ${#SELECTED_CASES[@]} -gt 0 ]]; then
    PYTHON_ARGS+=("${SELECTED_CASES[@]}")
fi

python3 "${PYTHON_ARGS[@]}" <<'PY'
from __future__ import annotations

import json
import math
import os
import resource
import signal
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

case_root = Path(sys.argv[1])
report_file = Path(sys.argv[2])
json_report_file = Path(sys.argv[3])
compiler_bin = Path(sys.argv[4])
project_root = Path(sys.argv[5])
ir_output_dir = Path(sys.argv[6])
iterations = int(sys.argv[7])
warmup = int(sys.argv[8])
compile_timeout = int(sys.argv[9])
run_timeout = int(sys.argv[10])
clang_opt_level = sys.argv[11]
run_started_at = sys.argv[12]
selected_cases = set(sys.argv[13:])

clang = shutil.which("clang")  # type: ignore[name-defined]
if clang is None:
    raise SystemExit("missing clang in PATH")

out_root = report_file.parent
out_root.mkdir(parents=True, exist_ok=True)
json_report_file.parent.mkdir(parents=True, exist_ok=True)

runtime_sylib = project_root / "tests/compiler2025/sylib.c"
runtime_compat = project_root / "tests/compiler2025/runtime_builtin_compat.c"
runtime_stub = project_root / "tests/run/support/runtime_builtin_stub.ll"
runtime_header = project_root / "tests/compiler2025/sylib.h"

runtime_sylib_o = out_root / "sylib.o"
runtime_compat_o = out_root / "runtime_builtin_compat.o"
runtime_stub_o = out_root / "runtime_builtin_stub.o"


def run_with_limits(argv: list[str], *, input_path: Path | None = None,
                    wall_timeout_s: float, capture_stdout: bool = False,
                    cwd: Path | None = None, cpu_limit_s: int | None = None):
    stdin = input_path.open("rb") if input_path is not None and input_path.exists() else None

    def preexec() -> None:
        os.setsid()
        if cpu_limit_s is not None:
            resource.setrlimit(resource.RLIMIT_CPU, (cpu_limit_s, cpu_limit_s))

    try:
        proc = subprocess.Popen(
            argv,
            stdin=stdin,
            stdout=subprocess.PIPE if capture_stdout else subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            cwd=str(cwd or project_root),
            start_new_session=False,
            preexec_fn=preexec,
        )
        start = time.perf_counter()
        try:
            stdout, stderr = proc.communicate(timeout=wall_timeout_s)
            return {
                "status": "ok",
                "elapsed": time.perf_counter() - start,
                "returncode": proc.returncode,
                "stdout": stdout or b"",
                "stderr": stderr or b"",
            }
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid, signal.SIGKILL)
            stdout, stderr = proc.communicate()
            return {
                "status": "timeout",
                "elapsed": time.perf_counter() - start,
                "returncode": None,
                "stdout": stdout or b"",
                "stderr": stderr or b"",
            }
    finally:
        if stdin is not None:
            stdin.close()


def normalize_text(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n").rstrip("\n")


def write_report(results: list[dict]) -> dict:
    pass_rows = [row for row in results if row["status"] == "PASS"]
    status_counts: dict[str, int] = {}
    for row in results:
        status_counts[row["status"]] = status_counts.get(row["status"], 0) + 1

    summary: dict[str, object] = {
        "generated_at": run_started_at,
        "case_root": str(case_root),
        "total_cases": len(results),
        "selected_case_count": len(selected_cases),
        "measured_cases": len(pass_rows),
        "compile_timeout_seconds": compile_timeout,
        "run_timeout_seconds": run_timeout,
        "iterations": iterations,
        "warmup": warmup,
        "clang_baseline_opt_level": clang_opt_level,
        "status_counts": status_counts,
    }

    if pass_rows:
        perf_ratios = [row["perf_percent"] / 100.0 for row in pass_rows]
        slowdowns = [row["sysycc_seconds"] / row["clang_seconds"] for row in pass_rows]
        summary["geomean_relative_performance_percent"] = (
            math.exp(sum(math.log(value) for value in perf_ratios) / len(perf_ratios))
            * 100.0
        )
        summary["geomean_slowdown"] = math.exp(
            sum(math.log(value) for value in slowdowns) / len(slowdowns)
        )
        summary["top10"] = sorted(
            pass_rows, key=lambda row: row["perf_percent"], reverse=True
        )[:10]
        summary["bottom10"] = sorted(
            pass_rows, key=lambda row: row["perf_percent"]
        )[:10]

    payload = {"summary": summary, "results": results}
    json_report_file.write_text(json.dumps(payload, indent=2))

    with report_file.open("w") as handle:
        handle.write("# Host IR Performance Report\n\n")
        handle.write("| Case | Status | SysyCC(s) | Clang(s) | Perf | Detail |\n")
        handle.write("| --- | --- | --- | --- | --- | --- |\n")
        for row in results:
            sysycc_seconds = "-" if row["sysycc_seconds"] is None else f"{row['sysycc_seconds']:.6f}"
            clang_seconds = "-" if row["clang_seconds"] is None else f"{row['clang_seconds']:.6f}"
            perf = "-" if row["perf_percent"] is None else f"{row['perf_percent']:.2f}%"
            handle.write(
                f"| {row['case']} | {row['status']} | {sysycc_seconds} | "
                f"{clang_seconds} | {perf} | {row['detail']} |\n"
            )
        handle.write("\n## Summary\n\n")
        for key, value in summary.items():
            if key in ("top10", "bottom10"):
                continue
            handle.write(f"- {key}: {value}\n")
        if pass_rows:
            handle.write("\n### Bottom 10\n\n")
            for row in summary["bottom10"]:  # type: ignore[index]
                handle.write(f"- {row['case']}: {row['perf_percent']:.2f}%\n")
            handle.write("\n### Top 10\n\n")
            for row in summary["top10"]:  # type: ignore[index]
                handle.write(f"- {row['case']}: {row['perf_percent']:.2f}%\n")

    return summary


def cpu_limit(timeout_s: int) -> int:
    return max(1, timeout_s)


runtime_compile_jobs = [
    ([clang, "-c", str(runtime_sylib), "-o", str(runtime_sylib_o)], "sylib"),
    ([clang, "-c", str(runtime_compat), "-o", str(runtime_compat_o)], "compat"),
    ([clang, "-c", str(runtime_stub), "-o", str(runtime_stub_o)], "stub"),
]
for argv, label in runtime_compile_jobs:
    runtime_compile = run_with_limits(
        argv,
        wall_timeout_s=float(compile_timeout),
        cwd=project_root,
        cpu_limit_s=cpu_limit(compile_timeout),
    )
    if runtime_compile["status"] != "ok" or runtime_compile["returncode"] != 0:
        raise SystemExit(f"failed to compile runtime support ({label})")


results: list[dict] = []
case_sources = sorted(case_root.glob("*.sy"))
if selected_cases:
    case_sources = [source for source in case_sources if source.stem in selected_cases]

if not case_sources:
    raise SystemExit("no matching cases were selected")

for index, source in enumerate(case_sources, 1):
    name = source.stem
    case_dir = out_root / name
    case_dir.mkdir(parents=True, exist_ok=True)
    input_file = case_root / f"{name}.in"
    expected_file = case_root / f"{name}.out"
    ir_file = ir_output_dir / f"{name}.ll"
    sysycc_binary = case_dir / f"{name}.sysycc.bin"
    clang_binary = case_dir / f"{name}.clang.bin"

    result = {
        "case": name,
        "status": "UNKNOWN",
        "sysycc_seconds": None,
        "clang_seconds": None,
        "perf_percent": None,
        "detail": "",
    }

    expected_text = (
        normalize_text(expected_file.read_text(errors="replace"))
        if expected_file.exists()
        else None
    )
    if expected_text is None:
        result["status"] = "MISSING_EXPECTED"
        result["detail"] = str(expected_file)
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue

    sysycc_compile = run_with_limits(
        [
            str(compiler_bin),
            "-include",
            str(runtime_header),
            str(source),
            "--dump-ir",
        ],
        wall_timeout_s=float(compile_timeout),
        cwd=project_root,
        cpu_limit_s=cpu_limit(compile_timeout),
    )
    if sysycc_compile["status"] != "ok":
        result["status"] = "SYSYCC_COMPILE_TIMEOUT"
        result["detail"] = f"{sysycc_compile['elapsed']:.2f}s"
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue
    if (
        sysycc_compile["returncode"] != 0
        or not ir_file.exists()
        or ir_file.stat().st_size == 0
    ):
        result["status"] = "SYSYCC_COMPILE_FAIL"
        result["detail"] = f"rc={sysycc_compile['returncode']}"
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue

    sysycc_link = run_with_limits(
        [
            clang,
            str(ir_file),
            str(runtime_sylib_o),
            str(runtime_compat_o),
            str(runtime_stub_o),
            "-fno-builtin",
            "-o",
            str(sysycc_binary),
        ],
        wall_timeout_s=float(compile_timeout),
        cwd=project_root,
        cpu_limit_s=cpu_limit(compile_timeout),
    )
    if sysycc_link["status"] != "ok" or sysycc_link["returncode"] != 0:
        result["status"] = (
            "SYSYCC_LINK_FAIL" if sysycc_link["status"] == "ok" else "SYSYCC_LINK_TIMEOUT"
        )
        result["detail"] = "link"
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue

    clang_compile = run_with_limits(
        [
            clang,
            clang_opt_level,
            "-std=gnu99",
            "-x",
            "c",
            "-include",
            str(runtime_header),
            "-I",
            str(runtime_header.parent),
            str(source),
            str(runtime_sylib),
            "-o",
            str(clang_binary),
        ],
        wall_timeout_s=float(compile_timeout),
        cwd=project_root,
        cpu_limit_s=cpu_limit(compile_timeout),
    )
    if clang_compile["status"] != "ok" or clang_compile["returncode"] != 0:
        result["status"] = (
            "CLANG_COMPILE_FAIL"
            if clang_compile["status"] == "ok"
            else "CLANG_COMPILE_TIMEOUT"
        )
        result["detail"] = "clang-compile"
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue

    sysycc_correctness = run_with_limits(
        [str(sysycc_binary)],
        input_path=input_file,
        wall_timeout_s=float(run_timeout),
        capture_stdout=True,
        cwd=project_root,
        cpu_limit_s=cpu_limit(run_timeout),
    )
    if sysycc_correctness["status"] != "ok":
        result["status"] = "SYSYCC_TIMEOUT"
        result["detail"] = f"{sysycc_correctness['elapsed']:.2f}s"
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue
    sysycc_output = normalize_text(
        sysycc_correctness["stdout"].decode("utf-8", "replace")
    ) + f"\n{sysycc_correctness['returncode']}"
    if sysycc_output != expected_text:
        result["status"] = "SYSYCC_MISMATCH"
        result["detail"] = "output"
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue

    clang_correctness = run_with_limits(
        [str(clang_binary)],
        input_path=input_file,
        wall_timeout_s=float(run_timeout),
        capture_stdout=True,
        cwd=project_root,
        cpu_limit_s=cpu_limit(run_timeout),
    )
    if clang_correctness["status"] != "ok":
        result["status"] = "CLANG_TIMEOUT"
        result["detail"] = f"{clang_correctness['elapsed']:.2f}s"
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue
    clang_output = normalize_text(
        clang_correctness["stdout"].decode("utf-8", "replace")
    ) + f"\n{clang_correctness['returncode']}"
    if clang_output != expected_text:
        result["status"] = "CLANG_MISMATCH"
        result["detail"] = "output"
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue

    for _ in range(warmup):
        sysycc_warmup = run_with_limits(
            [str(sysycc_binary)],
            input_path=input_file,
            wall_timeout_s=float(run_timeout),
            cwd=project_root,
            cpu_limit_s=cpu_limit(run_timeout),
        )
        if sysycc_warmup["status"] != "ok":
            result["status"] = "SYSYCC_BENCH_TIMEOUT"
            result["detail"] = f"{sysycc_warmup['elapsed']:.2f}s"
            results.append(result)
            write_report(results)
            print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
            break
        clang_warmup = run_with_limits(
            [str(clang_binary)],
            input_path=input_file,
            wall_timeout_s=float(run_timeout),
            cwd=project_root,
            cpu_limit_s=cpu_limit(run_timeout),
        )
        if clang_warmup["status"] != "ok":
            result["status"] = "CLANG_BENCH_TIMEOUT"
            result["detail"] = f"{clang_warmup['elapsed']:.2f}s"
            results.append(result)
            write_report(results)
            print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
            break
    if result["status"] != "UNKNOWN":
        continue

    sysycc_samples = []
    clang_samples = []
    timed_failed = False
    for _ in range(iterations):
        sysycc_timed = run_with_limits(
            [str(sysycc_binary)],
            input_path=input_file,
            wall_timeout_s=float(run_timeout),
            cwd=project_root,
            cpu_limit_s=cpu_limit(run_timeout),
        )
        if sysycc_timed["status"] != "ok":
            result["status"] = "SYSYCC_BENCH_TIMEOUT"
            result["detail"] = f"{sysycc_timed['elapsed']:.2f}s"
            timed_failed = True
            break
        clang_timed = run_with_limits(
            [str(clang_binary)],
            input_path=input_file,
            wall_timeout_s=float(run_timeout),
            cwd=project_root,
            cpu_limit_s=cpu_limit(run_timeout),
        )
        if clang_timed["status"] != "ok":
            result["status"] = "CLANG_BENCH_TIMEOUT"
            result["detail"] = f"{clang_timed['elapsed']:.2f}s"
            timed_failed = True
            break
        sysycc_samples.append(sysycc_timed["elapsed"])
        clang_samples.append(clang_timed["elapsed"])

    if timed_failed:
        results.append(result)
        write_report(results)
        print(f"[{index}/{len(case_sources)}] {name}: {result['status']}", flush=True)
        continue

    sysycc_seconds = statistics.median(sysycc_samples)
    clang_seconds = statistics.median(clang_samples)
    result["status"] = "PASS"
    result["sysycc_seconds"] = sysycc_seconds
    result["clang_seconds"] = clang_seconds
    result["perf_percent"] = clang_seconds / sysycc_seconds * 100.0
    result["detail"] = "ok"
    results.append(result)
    summary = write_report(results)
    print(
        f"[{index}/{len(case_sources)}] {name}: PASS "
        f"{result['perf_percent']:.2f}% ({sysycc_seconds:.3f}s vs {clang_seconds:.3f}s)",
        flush=True,
    )

summary = write_report(results)
print('REPORT_JSON=' + str(json_report_file), flush=True)
print('REPORT_MD=' + str(report_file), flush=True)
print('SUMMARY=' + json.dumps(summary), flush=True)
if any(row["status"] != "PASS" for row in results):
    raise SystemExit(1)
PY
