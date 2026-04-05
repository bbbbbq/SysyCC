#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
GENERATED_DIR="${BUILD_DIR}/generated"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/lexer_keyword_runtime_classification"
TEST_SOURCE="${SCRIPT_DIR}/lexer_keyword_runtime_classification.cpp"
INPUT_FILE="${SCRIPT_DIR}/input.c"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${TEST_BUILD_DIR}"

clang++ -std=c++17 -I"${GENERATED_DIR}" -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${PROJECT_ROOT}/src/common/source_line_map.cpp" \
    "${PROJECT_ROOT}/src/common/source_mapping_view.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/lexer_keyword_registry.cpp" \
    "${GENERATED_DIR}/frontend/lexer/lexer_scanner.cpp" \
    "${PROJECT_ROOT}/src/frontend/parser/parser_runtime.cpp" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}" "${INPUT_FILE}"

echo "verified: lexer scanner classifies identifiers through runtime keyword registry"
