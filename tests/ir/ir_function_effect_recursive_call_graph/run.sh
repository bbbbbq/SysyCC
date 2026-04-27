#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_function_effect_recursive_call_graph.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
"${BUILD_DIR}/compiler" -O1 "${INPUT_FILE}" -o "${BUILD_DIR}/ir_function_effect_recursive_call_graph"
"${BUILD_DIR}/ir_function_effect_recursive_call_graph"

echo "verified: function effect summary handles recursive call graphs"
