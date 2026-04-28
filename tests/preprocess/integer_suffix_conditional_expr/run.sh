#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/integer_suffix_conditional_expr.sy"
PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/integer_suffix_conditional_expr.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse >/dev/null 2>&1

assert_file_nonempty "${PREPROCESSED_FILE}"
grep -q '^int long_literal_branch = 1;$' "${PREPROCESSED_FILE}"
grep -q '^int unsigned_long_long_branch = 2;$' "${PREPROCESSED_FILE}"
grep -q '^int unsigned_64_hex_branch = 3;$' "${PREPROCESSED_FILE}"

echo "verified: #if integer literals accept standard unsigned/long suffixes"
