#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_optimization_level_controls.sy"
IR_O0="${BUILD_DIR}/cli_optimization_level_controls.o0.ll"
IR_O1="${BUILD_DIR}/cli_optimization_level_controls.o1.ll"
IR_O2="${BUILD_DIR}/cli_optimization_level_controls.o2.ll"
IR_O3="${BUILD_DIR}/cli_optimization_level_controls.o3.ll"
IR_OS="${BUILD_DIR}/cli_optimization_level_controls.os.ll"
IR_OG="${BUILD_DIR}/cli_optimization_level_controls.og.ll"
IR_OZ="${BUILD_DIR}/cli_optimization_level_controls.oz.ll"
IR_OFAST="${BUILD_DIR}/cli_optimization_level_controls.ofast.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -f "${IR_O0}" "${IR_O1}" "${IR_O2}" "${IR_O3}" "${IR_OS}" "${IR_OG}" \
    "${IR_OZ}" "${IR_OFAST}"

"${BUILD_DIR}/compiler" -S -emit-llvm -O0 -o "${IR_O0}" "${INPUT_FILE}"
"${BUILD_DIR}/compiler" -S -emit-llvm -O1 -o "${IR_O1}" "${INPUT_FILE}"
"${BUILD_DIR}/compiler" -S -emit-llvm -O2 -o "${IR_O2}" "${INPUT_FILE}"
"${BUILD_DIR}/compiler" -S -emit-llvm -O3 -o "${IR_O3}" "${INPUT_FILE}"
"${BUILD_DIR}/compiler" -S -emit-llvm -Os -o "${IR_OS}" "${INPUT_FILE}"
"${BUILD_DIR}/compiler" -S -emit-llvm -Og -o "${IR_OG}" "${INPUT_FILE}"
"${BUILD_DIR}/compiler" -S -emit-llvm -Oz -o "${IR_OZ}" "${INPUT_FILE}"
"${BUILD_DIR}/compiler" -S -emit-llvm -Ofast -o "${IR_OFAST}" "${INPUT_FILE}"

assert_file_nonempty "${IR_O0}"
assert_file_nonempty "${IR_O1}"
assert_file_nonempty "${IR_O2}"
assert_file_nonempty "${IR_O3}"
assert_file_nonempty "${IR_OS}"
assert_file_nonempty "${IR_OG}"
assert_file_nonempty "${IR_OZ}"
assert_file_nonempty "${IR_OFAST}"

grep -Eq 'br i1 (0|1|%t[0-9]+(\.raw)?), label %if\.then[0-9]+, label %if\.end[0-9]+' "${IR_O0}"
if grep -Eq 'br i1 (0|1|%t[0-9]+(\.raw)?), label %if\.then[0-9]+, label %if\.end[0-9]+' "${IR_O1}"; then
    echo "error: -O1 still emitted the unfused conditional branch" >&2
    exit 1
fi
grep -Eq 'ret i32 2$|ret i32 2[^0-9]' "${IR_O1}"
cmp -s "${IR_O1}" "${IR_O2}"
cmp -s "${IR_O1}" "${IR_O3}"
cmp -s "${IR_O1}" "${IR_OS}"
cmp -s "${IR_O1}" "${IR_OG}"
cmp -s "${IR_O1}" "${IR_OZ}"
cmp -s "${IR_O1}" "${IR_OFAST}"

echo "verified: -O0 and -O1 are wired differently, and higher public optimization spellings currently map to -O1"
