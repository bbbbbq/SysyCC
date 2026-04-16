#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

source "${PROJECT_ROOT}/tests/test_helpers.sh"
source "${SCRIPT_DIR}/../common.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
run_aarch64_backend_ll_case "${SCRIPT_DIR}" "${PROJECT_ROOT}" "${BUILD_DIR}"

echo "verified: clang-generated ll strtol parsing case lowers through the standalone AArch64 backend"
