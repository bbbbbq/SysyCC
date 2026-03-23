#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_global_nested_union_array_initializer.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
grep -F '@g_payload = internal global [2 x [1 x { { i32 }, [4 x i8] }]]' "${IR_FILE}"
grep -F '[1 x { { i32 }, [4 x i8] }] [{ { i32 }, [4 x i8] } { { i32 } { i32 7 }, [4 x i8] zeroinitializer }]' "${IR_FILE}"
grep -F '[1 x { { i32 }, [4 x i8] }] [{ { i32 }, [4 x i8] } { { i32 } { i32 9 }, [4 x i8] zeroinitializer }]]' "${IR_FILE}"

echo "verified: ir lowers nested union array globals to their concrete recursive storage layout"
