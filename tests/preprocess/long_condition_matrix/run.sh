#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/long_condition_matrix.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
INCLUDE_FIRST_DIR="${SCRIPT_DIR}/include_first"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" \
    -I "${INCLUDE_FIRST_DIR}" \
    "${INPUT_FILE}" \
    --dump-tokens \
    --dump-parse

assert_basic_frontend_outputs "${BUILD_DIR}" "${TEST_NAME}"

PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.preprocessed.sy"

grep -q '^int defined_branch = 1;$' "${PREPROCESSED_FILE}"
grep -q '^int local_feature_value = 3;$' "${PREPROCESSED_FILE}"
grep -q '^int chain_feature_total = 4 + 5;$' "${PREPROCESSED_FILE}"
grep -q '^int elifdef_branch = 30;$' "${PREPROCESSED_FILE}"
grep -q '^int cpp_attribute_probe = 40;$' "${PREPROCESSED_FILE}"
grep -q '^int feature_probe = 50;$' "${PREPROCESSED_FILE}"
grep -q '^int extension_probe = 60;$' "${PREPROCESSED_FILE}"
grep -q '^int builtin_probe = 70;$' "${PREPROCESSED_FILE}"
grep -q '^int attribute_probe = 80;$' "${PREPROCESSED_FILE}"
grep -q '^int building_module_probe = 90;$' "${PREPROCESSED_FILE}"

if grep -q 'missing_in_dead_branch' "${PREPROCESSED_FILE}"; then
    echo "error: inactive branch include leaked into long_condition_matrix output" >&2
    cat "${PREPROCESSED_FILE}" >&2
    exit 1
fi

if grep -q '^int elifndef_branch = -30;$' "${PREPROCESSED_FILE}" ||
   grep -q '^int fallback_branch = -31;$' "${PREPROCESSED_FILE}" ||
   grep -q '^int local_feature_value = -10;$' "${PREPROCESSED_FILE}" ||
   grep -q '^int chain_feature_total = -20;$' "${PREPROCESSED_FILE}"; then
    echo "error: false fallback branch leaked into long_condition_matrix output" >&2
    cat "${PREPROCESSED_FILE}" >&2
    exit 1
fi

echo "verified: long condition matrix combines defined/elifdef/include probes and clang-style builtin condition functions"
