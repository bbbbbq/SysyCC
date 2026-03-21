#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/clang_builtin_probe_condition.sy"
PREPROCESSED_FILE="${BUILD_DIR}/intermediate_results/clang_builtin_probe_condition.preprocessed.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse >/dev/null 2>&1

assert_file_nonempty "${PREPROCESSED_FILE}"
grep -q '^int feature_probe_branch = 1;$' "${PREPROCESSED_FILE}"
grep -q '^int extension_probe_branch = 2;$' "${PREPROCESSED_FILE}"
grep -q '^int builtin_probe_branch = 3;$' "${PREPROCESSED_FILE}"
grep -q '^int attribute_probe_branch = 4;$' "${PREPROCESSED_FILE}"
grep -q '^int building_module_branch = 5;$' "${PREPROCESSED_FILE}"

echo "verified: clang-style preprocessor builtin probes parse in #if expressions"
