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
import tempfile
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
    def get_process_snapshot(pid: int) -> dict[str, object]:
        snapshot: dict[str, object] = {"pid": pid}
        try:
            state_result = subprocess.run(
                ["ps", "-o", "state=", "-p", str(pid)],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                check=False,
            )
            state = state_result.stdout.decode("utf-8", "replace").strip()
            if state:
                snapshot["state"] = state
        except Exception:
            pass
        try:
            command_result = subprocess.run(
                ["ps", "-o", "command=", "-p", str(pid)],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                check=False,
            )
            command = command_result.stdout.decode("utf-8", "replace").strip()
            if command:
                snapshot["command"] = command
        except Exception:
            pass
        return snapshot

    def run_supervisor(result_path: Path, stdout_path: Path, stderr_path: Path) -> None:
        stdin = None
        stdout_stream = None
        stderr_stream = None
        proc = None
        start = time.perf_counter()

        def write_result(status: str, elapsed: float, returncode: int | None,
                         error_message: str | None = None,
                         lingering_process: dict[str, object] | None = None) -> None:
            payload = {
                "status": status,
                "elapsed": elapsed,
                "returncode": returncode,
            }
            if error_message is not None:
                payload["error"] = error_message
            if lingering_process is not None:
                payload["lingering_process"] = lingering_process
            result_path.write_text(json.dumps(payload))

        def preexec() -> None:
            os.setsid()
            if cpu_limit_s is not None:
                resource.setrlimit(resource.RLIMIT_CPU, (cpu_limit_s, cpu_limit_s))

        try:
            if input_path is not None and input_path.exists():
                stdin = input_path.open("rb")
            stdout_stream = (
                stdout_path.open("wb") if capture_stdout else open(os.devnull, "wb")
            )
            stderr_stream = stderr_path.open("wb")
            proc = subprocess.Popen(
                argv,
                stdin=stdin,
                stdout=stdout_stream,
                stderr=stderr_stream,
                cwd=str(cwd or project_root),
                start_new_session=False,
                preexec_fn=preexec,
            )
            while True:
                returncode = proc.poll()
                elapsed = time.perf_counter() - start
                if returncode is not None:
                    write_result("ok", elapsed, returncode)
                    return
                if elapsed >= wall_timeout_s:
                    try:
                        os.killpg(proc.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    lingering_process = None
                    for _ in range(5):
                        if proc.poll() is not None:
                            break
                        time.sleep(0.05)
                    if proc.poll() is None:
                        lingering_process = get_process_snapshot(proc.pid)
                    write_result("timeout", elapsed, None, lingering_process=lingering_process)
                    return
                time.sleep(0.05)
        except Exception as exc:
            write_result("error", time.perf_counter() - start, None, str(exc))
        finally:
            if stdout_stream is not None:
                stdout_stream.close()
            if stderr_stream is not None:
                stderr_stream.close()
            if stdin is not None:
                stdin.close()

    with tempfile.TemporaryDirectory(prefix="sysycc-host-ir-run-") as temp_dir:
        temp_root = Path(temp_dir)
        result_path = temp_root / "result.json"
        stdout_path = temp_root / "stdout.bin"
        stderr_path = temp_root / "stderr.bin"
        supervisor_pid = os.fork()
        if supervisor_pid == 0:
            try:
                run_supervisor(result_path, stdout_path, stderr_path)
                os._exit(0)
            except BaseException:
                os._exit(1)

        _, status = os.waitpid(supervisor_pid, 0)
        stdout = stdout_path.read_bytes() if stdout_path.exists() else b""
        stderr = stderr_path.read_bytes() if stderr_path.exists() else b""
        if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
            return {
                "status": "error",
                "elapsed": 0.0,
                "returncode": None,
                "stdout": stdout,
                "stderr": stderr,
            }
        if not result_path.exists():
            return {
                "status": "error",
                "elapsed": 0.0,
                "returncode": None,
                "stdout": stdout,
                "stderr": stderr,
            }
        payload = json.loads(result_path.read_text())
        return {
            "status": payload.get("status", "error"),
            "elapsed": float(payload.get("elapsed", 0.0)),
            "returncode": payload.get("returncode"),
            "stdout": stdout,
            "stderr": stderr,
            "lingering_process": payload.get("lingering_process"),
        }


def normalize_text(text: str) -> str:
    return text.replace("\r\n", "\n").replace("\r", "\n").rstrip("\n")


def format_timeout_detail(run_result: dict[str, object]) -> str:
    detail = f"{float(run_result['elapsed']):.2f}s"
    lingering = run_result.get("lingering_process")
    if isinstance(lingering, dict) and lingering.get("pid") is not None:
        detail += f"; lingering pid={lingering['pid']}"
        if lingering.get("state"):
            detail += f" state={lingering['state']}"
    return detail


def attach_lingering_process(result: dict[str, object], run_result: dict[str, object]) -> None:
    lingering = run_result.get("lingering_process")
    if isinstance(lingering, dict) and lingering:
        result["lingering_process"] = lingering


def write_report(results: list[dict]) -> dict:
    pass_rows = [row for row in results if row["status"] == "PASS"]
    lingering_rows = [
        row for row in results
        if isinstance(row.get("lingering_process"), dict) and row["lingering_process"]
    ]
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
    if lingering_rows:
        summary["lingering_process_count"] = len(lingering_rows)
        summary["lingering_processes"] = [
            {
                "case": row["case"],
                "status": row["status"],
                **row["lingering_process"],
            }
            for row in lingering_rows
        ]

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
            if key in ("top10", "bottom10", "lingering_processes"):
                continue
            handle.write(f"- {key}: {value}\n")
        if lingering_rows:
            handle.write("\n### Lingering Timed-out Processes\n\n")
            for row in lingering_rows:
                lingering = row["lingering_process"]
                handle.write(
                    f"- {row['case']}: pid={lingering.get('pid')} "
                    f"state={lingering.get('state', 'unknown')}\n"
                )
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
        result["detail"] = format_timeout_detail(sysycc_correctness)
        attach_lingering_process(result, sysycc_correctness)
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
        result["detail"] = format_timeout_detail(clang_correctness)
        attach_lingering_process(result, clang_correctness)
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
            result["detail"] = format_timeout_detail(sysycc_warmup)
            attach_lingering_process(result, sysycc_warmup)
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
            result["detail"] = format_timeout_detail(clang_warmup)
            attach_lingering_process(result, clang_warmup)
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
            result["detail"] = format_timeout_detail(sysycc_timed)
            attach_lingering_process(result, sysycc_timed)
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
            result["detail"] = format_timeout_detail(clang_timed)
            attach_lingering_process(result, clang_timed)
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
