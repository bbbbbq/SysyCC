#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
RESULT_DIR="${BUILD_DIR}/intermediate_results"
INPUT_FILE="${SCRIPT_DIR}/macro_literal_expansion_bug.sy"
PREPROCESSED_FILE="${RESULT_DIR}/macro_literal_expansion_bug.preprocessed.sy"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

if ! "${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse; then
    echo "error: compiler failed while checking literal-safe macro expansion" >&2
    exit 1
fi

if [[ ! -f "${PREPROCESSED_FILE}" ]]; then
    echo "error: missing preprocessed output: ${PREPROCESSED_FILE}" >&2
    exit 1
fi

if ! grep -Fq '"URL";' "${PREPROCESSED_FILE}"; then
    echo "error: string literal was still altered by macro expansion" >&2
    exit 1
fi

if ! grep -Fq "'X';" "${PREPROCESSED_FILE}"; then
    echo "error: char literal was still altered by macro expansion" >&2
    exit 1
fi

if grep -Fq '"123";' "${PREPROCESSED_FILE}" || grep -Fq "'456';" "${PREPROCESSED_FILE}"; then
    echo "error: bug output is still present in preprocessed source" >&2
    exit 1
fi

echo "verified: macro expansion no longer enters string/char literals"
