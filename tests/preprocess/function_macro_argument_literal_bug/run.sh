#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
RESULT_DIR="${BUILD_DIR}/intermediate_results"
INPUT_FILE="${SCRIPT_DIR}/function_macro_argument_literal_bug.sy"
PREPROCESSED_FILE="${RESULT_DIR}/function_macro_argument_literal_bug.preprocessed.sy"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

if ! "${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse >/dev/null 2>&1; then
    echo "error: compiler failed while checking function-like macro literal arguments" >&2
    exit 1
fi

if [[ ! -f "${PREPROCESSED_FILE}" ]]; then
    echo "error: missing preprocessed output: ${PREPROCESSED_FILE}" >&2
    exit 1
fi

if ! grep -Fq '"left,right";' "${PREPROCESSED_FILE}"; then
    echo "error: function-like macro did not expand across string literal argument" >&2
    exit 1
fi

if grep -Fq 'PAIR("left,right", 0);' "${PREPROCESSED_FILE}"; then
    echo "error: unexpanded macro call is still present in preprocessed source" >&2
    exit 1
fi

echo "verified: function-like macro arguments handle string literals correctly"
