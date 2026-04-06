#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_public_asm_output.sy"
OUTPUT_O0="${BUILD_DIR}/cli_public_asm_output.o0.s"
OUTPUT_O1="${BUILD_DIR}/cli_public_asm_output.o1.s"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -f "${OUTPUT_O0}" "${OUTPUT_O1}"

"${BUILD_DIR}/compiler" -S -o "${OUTPUT_O0}" "${INPUT_FILE}"
"${BUILD_DIR}/compiler" -S -O1 -o "${OUTPUT_O1}" "${INPUT_FILE}"

assert_file_nonempty "${OUTPUT_O0}"
assert_file_nonempty "${OUTPUT_O1}"

grep -q '^\.globl main$' "${OUTPUT_O0}"
grep -q '^\.globl main$' "${OUTPUT_O1}"

echo "verified: the public compiler binary accepts official -S/-o and -O1 assembly commands"
