#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STAGE_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
MANIFEST_FILE="${SYSYCC_RISCV64_SINGLE_SOURCE_MANIFEST:-${STAGE_ROOT}/manifest.txt}"

export SYSYCC_TEST_DISABLE_HOST_TOOL_WRAPPERS=1

source "${PROJECT_ROOT}/tests/test_helpers.sh"
source "${STAGE_ROOT}/common.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

batch_index="$(riscv_single_source_batch_index)"
batch_size="$(riscv_single_source_batch_size)"
start_index="$(riscv_single_source_start_index)"
list_only="${SYSYCC_RISCV64_SINGLE_SOURCE_LIST_ONLY:-0}"
discover_cases="${SYSYCC_RISCV64_SINGLE_SOURCE_DISCOVER:-1}"
source_stage_root="$(riscv_single_source_source_stage_root)"

selected_entries=()
while IFS= read -r entry; do
    [[ -z "${entry}" ]] && continue
    selected_entries+=("${entry}")
done < <(
    if [[ "${discover_cases}" == "1" ]]; then
        emit_riscv_single_source_discovered_entries "${source_stage_root}" | \
            emit_riscv_single_source_batch_entries /dev/stdin "${start_index}" "${batch_index}" "${batch_size}"
    else
        emit_riscv_single_source_batch_entries "${MANIFEST_FILE}" "${start_index}" "${batch_index}" "${batch_size}"
    fi
)

if [[ "${#selected_entries[@]}" -eq 0 ]]; then
    echo "error: no SingleSource entries found for batch ${batch_index}" >&2
    exit 1
fi

range_begin="${start_index}"
range_end=$(( start_index + (batch_size - 1) ))
if [[ "${batch_index}" -gt 1 ]]; then
    range_begin=$(( start_index + (batch_index - 1) * batch_size ))
    range_end=$(( range_begin + batch_size - 1 ))
fi

echo "==> Selected RISC-V64 SingleSource batch ${batch_index} (size=${#selected_entries[@]}, cases=${range_begin}-${range_end})"
for entry in "${selected_entries[@]}"; do
    IFS='|' read -r c_std source_rel argv_text <<<"${entry}"
    printf '   - %s [%s]\n' "${source_rel}" "${c_std}"
done

if [[ "${list_only}" == "1" ]]; then
    exit 0
fi

host_clang="$(find_riscv64_host_clang 2>/dev/null || true)"
sysroot="$(find_riscv64_linux_sysroot 2>/dev/null || true)"
qemu_riscv64="$(find_riscv64_user_qemu 2>/dev/null || true)"
runtime_mode=""

if [[ -z "${host_clang}" ]]; then
    echo "skipped: missing host clang for riscv64 SingleSource batch"
    exit 0
fi

if [[ -n "${sysroot}" && -n "${qemu_riscv64}" ]]; then
    runtime_mode="host"
elif have_riscv64_docker_runtime; then
    ensure_riscv64_docker_image "${STAGE_ROOT}"
    runtime_mode="docker"
else
    echo "skipped: missing local qemu-riscv64/sysroot and no docker fallback for riscv64 SingleSource batch"
    exit 0
fi

pass_count=0
fail_count=0
case_count=0

for entry in "${selected_entries[@]}"; do
    IFS='|' read -r c_std source_rel argv_text <<<"${entry}"
    case_count=$((case_count + 1))
    case_id="$(single_source_case_id "${source_rel}")"
    log_file="${BUILD_DIR}/test_logs/riscv64_backend_single_source_${case_id}.log"
    progress_prefix="[${case_count}/${#selected_entries[@]}]"

    if [[ ! -f "${source_stage_root}/upstream/${source_rel}" ]]; then
        echo "${progress_prefix} [FAIL] ${source_rel} (missing vendored snapshot)"
        fail_count=$((fail_count + 1))
        continue
    fi

    if run_riscv_single_source_snapshot_case \
        "${STAGE_ROOT}" "${source_stage_root}" "${PROJECT_ROOT}" "${BUILD_DIR}" \
        "${runtime_mode}" "${sysroot}" "${host_clang}" "${source_rel}" "${c_std}" "${argv_text}"; then
        echo "${progress_prefix} [PASS] ${source_rel}"
        pass_count=$((pass_count + 1))
    else
        echo "${progress_prefix} [FAIL] ${source_rel}"
        echo "                 log: ${log_file}"
        fail_count=$((fail_count + 1))
    fi
done

echo
echo "RISC-V64 SingleSource summary: total=${case_count} pass=${pass_count} fail=${fail_count}"

if [[ "${fail_count}" -ne 0 ]]; then
    exit 1
fi

echo "verified: selected llvm-test-suite SingleSource batch matches clang baseline on RISC-V64"
