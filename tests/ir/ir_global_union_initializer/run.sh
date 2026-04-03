#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_global_union_initializer.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -Eq '^@g_box = (internal )?global \{ i64 \} \{ i64 4294967295 \}$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = getelementptr inbounds \{ i64 \}, ptr @g_box, i32 0, i32 0$' "${IR_FILE}"
grep -Eq '^  %t[0-9]+ = load i64, ptr %t[0-9]+$' "${IR_FILE}"

echo "verified: global union initializers preserve first-field bytes across padded union storage"
