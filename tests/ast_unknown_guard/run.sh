#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ast_unknown_guard.sy"
OUTPUT_FILE="${BUILD_DIR}/ast_unknown_guard.out"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

set +e
"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ast \
    >"${OUTPUT_FILE}" 2>&1
STATUS=$?
set -e

if [[ "${STATUS}" -eq 0 ]]; then
    echo "AstPass should fail when unknown nodes remain in the AST" >&2
    exit 1
fi

grep -q 'failed to build ast: ast contains unknown nodes' "${OUTPUT_FILE}"

echo "verified: AstPass rejects ASTs that still contain unknown nodes"
