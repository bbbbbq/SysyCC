#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
INPUT_FILE="${SCRIPT_DIR}/run_unsigned_char_string_initializer.sy"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/compiler" "${INPUT_FILE}" -o "${TMP_DIR}/unsigned_char_string"
"${TMP_DIR}/unsigned_char_string"

echo "verified: string literals initialize unsigned character arrays"
