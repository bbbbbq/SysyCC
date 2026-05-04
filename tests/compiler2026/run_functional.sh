#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPILER2026_DATA_ROOT="${SCRIPT_DIR}/extracted/functional/functional_recover"
COMPILER2026_BUILD_ROOT="${SCRIPT_DIR}/build"

usage() {
    cat <<'EOF'
Usage:
  ./tests/compiler2026/run_functional.sh [--suite functional|h_functional|all] [--report path] [case_name ...]

Examples:
  ./tests/compiler2026/run_functional.sh --suite functional 00_main
  ./tests/compiler2026/run_functional.sh --suite all
EOF
}

has_report=0
suite_name="functional"
for arg in "$@"; do
    case "${arg}" in
    --report|--report=*)
        has_report=1
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    esac
done

index=1
while [[ "${index}" -le "$#" ]]; do
    arg="${!index}"
    case "${arg}" in
    --suite)
        next_index=$((index + 1))
        if [[ "${next_index}" -le "$#" ]]; then
            suite_name="${!next_index}"
        fi
        index=$((index + 2))
        ;;
    --suite=*)
        suite_name="${arg#--suite=}"
        index=$((index + 1))
        ;;
    *)
        index=$((index + 1))
        ;;
    esac
done

script_args=("$@")
if [[ "${has_report}" -eq 0 ]]; then
    script_args=(
        --report
        "${COMPILER2026_BUILD_ROOT}/compiler2026_${suite_name}_result.md"
        "${script_args[@]}"
    )
fi

SYSYCC_COMPILER2025_FUNCTIONAL_DATA_ROOT="${COMPILER2026_DATA_ROOT}" \
SYSYCC_COMPILER2025_FUNCTIONAL_BUILD_ROOT="${COMPILER2026_BUILD_ROOT}" \
    "${PROJECT_ROOT}/tests/compiler2025/run_functional.sh" "${script_args[@]}"
