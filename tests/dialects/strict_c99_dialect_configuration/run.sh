#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/strict_c99_dialect_configuration"
TEST_SOURCE="${SCRIPT_DIR}/strict_c99_dialect_configuration.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${TEST_BUILD_DIR}"

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${PROJECT_ROOT}/src/frontend/dialects/ast_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/attribute_semantic_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/builtin_type_semantic_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/dialect_manager.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/ir_extension_lowering_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/ir_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/lexer_keyword_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/parser_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/preprocess_directive_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/preprocess_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/preprocess_probe_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/semantic_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/builtin_types/builtin_type_extension_pack.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/c99/c99_dialect.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/clang/clang_dialect.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/gnu/gnu_dialect.cpp" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: strict c99 configuration only registers the c99 dialect pack"
