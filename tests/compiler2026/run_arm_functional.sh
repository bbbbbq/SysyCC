#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPILER2026_CASE_ROOT="${SCRIPT_DIR}/extracted/functional/functional_recover/functional"
COMPILER2026_BUILD_ROOT="${SCRIPT_DIR}/build/arm_functional"

usage() {
    cat <<'EOF'
Usage:
  ./tests/compiler2026/run_arm_functional.sh [--case-root path] [--report path] [case_name ...]

Examples:
  ./tests/compiler2026/run_arm_functional.sh 00_main
  ./tests/compiler2026/run_arm_functional.sh --case-root /path/to/cases sample_case
EOF
}

has_case_root=0
has_report=0
for arg in "$@"; do
    case "${arg}" in
    --case-root|--case-root=*)
        has_case_root=1
        ;;
    --report|--report=*)
        has_report=1
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    esac
done

script_args=("$@")
if [[ "${has_report}" -eq 0 ]]; then
    script_args=(
        --report
        "${COMPILER2026_BUILD_ROOT}/compiler2026_arm_functional_result.md"
        "${script_args[@]}"
    )
fi
if [[ "${has_case_root}" -eq 0 ]]; then
    script_args=(
        --case-root
        "${COMPILER2026_CASE_ROOT}"
        "${script_args[@]}"
    )
fi

SYSYCC_COMPILER2025_ARM_FUNCTIONAL_BUILD_ROOT="${COMPILER2026_BUILD_ROOT}" \
    "${PROJECT_ROOT}/tests/compiler2025/run_arm_functional.sh" "${script_args[@]}"
