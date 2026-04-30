#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_global_union_pointer_address_initializer.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir >/dev/null

assert_file_nonempty "${IR_FILE}"
grep -Eq '^@cmp_vars = (internal )?global \[2 x \{ ptr \}\] \[ \{ ptr \} \{ ptr @opt_config \}, \{ ptr \} \{ ptr @opt_verbosity \} \]$' "${IR_FILE}"

echo "verified: global union pointer address initializers lower without byte packing"
