#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/ast_feature_runtime_policy"
TEST_SOURCE="${SCRIPT_DIR}/ast_feature_runtime_policy.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${TEST_BUILD_DIR}"

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${PROJECT_ROOT}/src/frontend/ast/ast_node.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/detail/ast_feature_validator.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/ast_feature_registry.cpp" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: ast feature registry gates lowered AST features at runtime"
