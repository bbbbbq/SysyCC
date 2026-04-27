#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SYSYCC_SELF_PROFILE_BUILD_DIR:-${PROJECT_ROOT}/build-ninja}"
REPORT_FILE="${SYSYCC_SELF_PROFILE_REPORT:-${PROJECT_ROOT}/build/self_build_profile.md}"
LOG_DIR="${SYSYCC_SELF_PROFILE_LOG_DIR:-${PROJECT_ROOT}/build/self-build-profile}"
JOBS="${SYSYCC_SELF_PROFILE_JOBS:-4}"
SKIP_CLEAN="${SYSYCC_SELF_PROFILE_SKIP_CLEAN:-0}"

mkdir -p "${LOG_DIR}" "$(dirname "${REPORT_FILE}")"

run_id=0
rows=()

now_utc() {
    date -u +"%Y-%m-%dT%H:%M:%SZ"
}

ccache_stats() {
    if command -v ccache >/dev/null 2>&1; then
        ccache --show-stats
    else
        printf 'ccache: not installed\n'
    fi
}

measure_build() {
    local name="$1"
    local command_text="$2"
    local log_file="${LOG_DIR}/$(printf '%02d' "${run_id}")-${name//[^A-Za-z0-9_.-]/_}.log"
    local status=0
    local start_ns=""
    local end_ns=""
    local elapsed_ms=""
    local build_steps=0
    local compile_steps=0

    run_id=$((run_id + 1))
    start_ns="$(python3 -c 'import time; print(time.perf_counter_ns())')"
    set +e
    bash -lc "${command_text}" >"${log_file}" 2>&1
    status=$?
    set -e
    end_ns="$(python3 -c 'import time; print(time.perf_counter_ns())')"
    elapsed_ms="$(python3 - "${start_ns}" "${end_ns}" <<'PY'
import sys
start = int(sys.argv[1])
end = int(sys.argv[2])
print(f"{(end - start) / 1_000_000:.3f}")
PY
)"
    build_steps="$(grep -Ec '^\[[0-9]+/[0-9]+\]' "${log_file}" || true)"
    compile_steps="$(grep -Ec 'Building (C|CXX) object' "${log_file}" || true)"
    rows+=("| ${name} | ${elapsed_ms} | ${status} | ${build_steps} | ${compile_steps} | \`${log_file}\` |")
    if [[ "${status}" -ne 0 ]]; then
        echo "error: ${name} failed, see ${log_file}" >&2
        return "${status}"
    fi
}

touch_and_measure() {
    local name="$1"
    local path="$2"
    local marker="${LOG_DIR}/$(printf '%02d' "${run_id}")-${name//[^A-Za-z0-9_.-]/_}.timestamp"

    cp -p "${PROJECT_ROOT}/${path}" "${marker}"
    touch "${PROJECT_ROOT}/${path}"
    measure_build "${name}" "cd '${PROJECT_ROOT}' && cmake --build '${BUILD_DIR}' --parallel '${JOBS}'"
    touch -r "${marker}" "${PROJECT_ROOT}/${path}"
}

ccache_stats >"${LOG_DIR}/ccache-before.txt"

measure_build "configure" \
    "cd '${PROJECT_ROOT}' && cmake -S . -B '${BUILD_DIR}' -G Ninja -DSYSYCC_USE_COMPILER_CACHE=ON"
measure_build "noop-build" \
    "cd '${PROJECT_ROOT}' && cmake --build '${BUILD_DIR}' --parallel '${JOBS}'"
touch_and_measure "touch-compiler-cpp" "src/compiler/compiler.cpp"
touch_and_measure "touch-parser-cpp" "src/frontend/parser/parser.cpp"
touch_and_measure "touch-ir-instruction-hpp" "src/backend/ir/shared/core/ir_instruction.hpp"

if [[ "${SKIP_CLEAN}" != "1" ]]; then
    measure_build "warm-cache-clean-rebuild" \
        "cd '${PROJECT_ROOT}' && rm -rf '${BUILD_DIR}' && cmake -S . -B '${BUILD_DIR}' -G Ninja -DSYSYCC_USE_COMPILER_CACHE=ON && cmake --build '${BUILD_DIR}' --parallel '${JOBS}'"
fi

ccache_stats >"${LOG_DIR}/ccache-after.txt"

{
    echo "# SysyCC Self Build Profile"
    echo
    echo "- Generated: $(now_utc)"
    echo "- Project root: \`${PROJECT_ROOT}\`"
    echo "- Build dir: \`${BUILD_DIR}\`"
    echo "- Jobs: ${JOBS}"
    echo "- Clean rebuild skipped: ${SKIP_CLEAN}"
    echo
    echo "## Timings"
    echo
    echo "| Scenario | ms | status | ninja steps | compile steps | log |"
    echo "| --- | ---: | ---: | ---: | ---: | --- |"
    printf '%s\n' "${rows[@]}"
    echo
    echo "## ccache Before"
    echo
    echo '```text'
    cat "${LOG_DIR}/ccache-before.txt"
    echo '```'
    echo
    echo "## ccache After"
    echo
    echo '```text'
    cat "${LOG_DIR}/ccache-after.txt"
    echo '```'
} >"${REPORT_FILE}"

echo "wrote ${REPORT_FILE}"
