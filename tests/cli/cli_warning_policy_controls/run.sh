#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_warning_policy_controls.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
NO_WARNING_OUTPUT="$("${BUILD_DIR}/SysyCC" -fsyntax-only -Wall -Wno-sign-compare "${INPUT_FILE}" 2>&1)"
NO_WARNING_RC=$?
set -e

if [[ ${NO_WARNING_RC} -ne 0 ]]; then
    echo "error: -Wno-sign-compare unexpectedly failed" >&2
    echo "${NO_WARNING_OUTPUT}" >&2
    exit 1
fi

if grep -Fq "comparison of integers of different signs" <<<"${NO_WARNING_OUTPUT}"; then
    echo "error: -Wno-sign-compare did not suppress the warning" >&2
    echo "${NO_WARNING_OUTPUT}" >&2
    exit 1
fi

set +e
WERROR_OUTPUT="$("${BUILD_DIR}/SysyCC" -fsyntax-only -Werror=sign-compare "${INPUT_FILE}" 2>&1)"
WERROR_RC=$?
set -e

if [[ ${WERROR_RC} -eq 0 ]]; then
    echo "error: -Werror=sign-compare unexpectedly succeeded" >&2
    echo "${WERROR_OUTPUT}" >&2
    exit 1
fi

grep -Fq "error: comparison of integers of different signs [-Werror=sign-compare]" <<<"${WERROR_OUTPUT}"

ASM_OUTPUT_FILE="${BUILD_DIR}/cli_warning_policy_controls.s"
rm -f "${ASM_OUTPUT_FILE}"
set +e
ASM_WERROR_OUTPUT="$("${BUILD_DIR}/SysyCC" -S -Werror=sign-compare -o "${ASM_OUTPUT_FILE}" "${INPUT_FILE}" 2>&1)"
ASM_WERROR_RC=$?
set -e

if [[ ${ASM_WERROR_RC} -eq 0 ]]; then
    echo "error: -S -Werror=sign-compare unexpectedly succeeded" >&2
    echo "${ASM_WERROR_OUTPUT}" >&2
    exit 1
fi

if [[ -e "${ASM_OUTPUT_FILE}" ]]; then
    echo "error: promoted warning still produced an asm artifact" >&2
    ls -l "${ASM_OUTPUT_FILE}" >&2
    echo "${ASM_WERROR_OUTPUT}" >&2
    exit 1
fi

grep -Fq "error: comparison of integers of different signs [-Werror=sign-compare]" <<<"${ASM_WERROR_OUTPUT}"

CONVERSION_INPUT="${PROJECT_ROOT}/tests/semantic/semantic_narrowing_initializer_warning/semantic_narrowing_initializer_warning.sy"
set +e
CONVERSION_OUTPUT="$("${BUILD_DIR}/SysyCC" -fsyntax-only -Wconversion "${CONVERSION_INPUT}" 2>&1)"
CONVERSION_RC=$?
set -e

if [[ ${CONVERSION_RC} -ne 0 ]]; then
    echo "error: -Wconversion unexpectedly failed" >&2
    echo "${CONVERSION_OUTPUT}" >&2
    exit 1
fi

grep -Fq "warning: implicit integer conversion may change value [-Wconversion]" <<<"${CONVERSION_OUTPUT}"

echo "verified: warning groups, suppression, promotion, and explicit enables behave like a GCC-style driver"
