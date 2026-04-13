#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
MANIFEST_FILE="${STAGE_ROOT}/manifest.txt"

source "${PROJECT_ROOT}/tests/test_helpers.sh"
source "${STAGE_ROOT}/common.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

host_clang="${SYSYCC_HOST_CLANG:-$(command -v clang 2>/dev/null || true)}"
sysroot="$(find_aarch64_sysroot 2>/dev/null || true)"
aarch64_cc="$(find_aarch64_cc 2>/dev/null || true)"

if [[ -z "${host_clang}" ]]; then
    echo "skipped: missing host clang for llvm SingleSource import stage"
    exit 0
fi
if [[ -z "${sysroot}" ]]; then
    echo "skipped: missing AArch64 sysroot for llvm SingleSource import stage"
    exit 0
fi
if [[ -z "${aarch64_cc}" ]]; then
    echo "skipped: missing AArch64 C compiler for llvm SingleSource import stage"
    exit 0
fi
if ! have_aarch64_binary_runtime; then
    echo "skipped: missing AArch64 runtime runner for llvm SingleSource import stage"
    exit 0
fi

pass_count=0
xfail_count=0
fail_count=0
xpass_count=0
case_count=0

while IFS='|' read -r expectation c_std source_rel xfail_substring; do
    if [[ -z "${expectation}" || "${expectation}" == \#* ]]; then
        continue
    fi
    case_count=$((case_count + 1))
    case_id="$(single_source_case_id "${source_rel}")"
    log_file="${BUILD_DIR}/test_logs/aarch64_backend_single_source_${case_id}.log"

    if [[ ! -f "${STAGE_ROOT}/upstream/${source_rel}" ]]; then
        echo "[FAIL] ${source_rel} (missing vendored snapshot; run sync_upstream_cases.sh)"
        fail_count=$((fail_count + 1))
        continue
    fi

    if run_single_source_snapshot_case "${STAGE_ROOT}" "${PROJECT_ROOT}" "${BUILD_DIR}" \
        "${sysroot}" "${aarch64_cc}" "${host_clang}" "${source_rel}" "${c_std}"; then
        if [[ "${expectation}" == "XFAIL" ]]; then
            echo "[XPASS] ${source_rel}"
            xpass_count=$((xpass_count + 1))
        else
            echo "[PASS] ${source_rel}"
            pass_count=$((pass_count + 1))
        fi
        continue
    fi

    if [[ "${expectation}" == "XFAIL" ]] && grep -Fq "${xfail_substring}" "${log_file}"; then
        echo "[XFAIL] ${source_rel}"
        xfail_count=$((xfail_count + 1))
    else
        echo "[FAIL] ${source_rel}"
        echo "       log: ${log_file}"
        fail_count=$((fail_count + 1))
    fi
done <"${MANIFEST_FILE}"

echo
echo "SingleSource summary: total=${case_count} pass=${pass_count} xfail=${xfail_count} fail=${fail_count} xpass=${xpass_count}"

if [[ "${fail_count}" -ne 0 || "${xpass_count}" -ne 0 ]]; then
    exit 1
fi

echo "verified: selected llvm-test-suite SingleSource cases match clang baseline or fail with tracked xfail reasons"
