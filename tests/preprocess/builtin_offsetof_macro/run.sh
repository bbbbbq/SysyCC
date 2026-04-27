#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/builtin_offsetof_macro.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
OUTPUT_DIR="${SCRIPT_DIR}/build"
PREPROCESSED_FILE="${OUTPUT_DIR}/${TEST_NAME}.i"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${OUTPUT_DIR}"
"${BUILD_DIR}/SysyCC" -E "${INPUT_FILE}" -o "${PREPROCESSED_FILE}"
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" -fsyntax-only --dump-parse

assert_file_nonempty "${PREPROCESSED_FILE}"

grep -q 'unsigned long' "${PREPROCESSED_FILE}"
grep -q -- '->payload' "${PREPROCESSED_FILE}"

echo "verified: __builtin_offsetof lowers to a regular C member-address expression"
