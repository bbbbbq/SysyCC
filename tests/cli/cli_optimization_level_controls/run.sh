#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_optimization_level_controls.sy"
IR_O0="${BUILD_DIR}/cli_optimization_level_controls.o0.ll"
IR_O1="${BUILD_DIR}/cli_optimization_level_controls.o1.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -f "${IR_O0}" "${IR_O1}"

"${BUILD_DIR}/compiler" -S -emit-llvm -O0 -o "${IR_O0}" "${INPUT_FILE}"
"${BUILD_DIR}/compiler" -S -emit-llvm -O1 -o "${IR_O1}" "${INPUT_FILE}"

assert_file_nonempty "${IR_O0}"
assert_file_nonempty "${IR_O1}"

grep -Eq 'br i1 (0|1|%t[0-9]+(\.raw)?), label %if\.then[0-9]+, label %if\.end[0-9]+' "${IR_O0}"
if grep -Eq 'br i1 (0|1|%t[0-9]+(\.raw)?), label %if\.then[0-9]+, label %if\.end[0-9]+' "${IR_O1}"; then
    echo "error: -O1 still emitted the unfused conditional branch" >&2
    exit 1
fi
grep -Eq 'ret i32 2$|ret i32 2[^0-9]' "${IR_O1}"

set +e
UNSUPPORTED_OUTPUT="$("${BUILD_DIR}/compiler" -fsyntax-only -O2 "${INPUT_FILE}" 2>&1)"
UNSUPPORTED_RC=$?
set -e

if [[ ${UNSUPPORTED_RC} -eq 0 ]]; then
    echo "error: unsupported -O2 unexpectedly succeeded" >&2
    echo "${UNSUPPORTED_OUTPUT}" >&2
    exit 1
fi

grep -Fq "argument to '-O' is not supported: '2'" <<<"${UNSUPPORTED_OUTPUT}"

echo "verified: -O0 and -O1 are wired differently and unsupported optimization levels fail explicitly"
