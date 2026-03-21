#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/preprocess_feature_runtime_policy"
TEST_SOURCE="${SCRIPT_DIR}/preprocess_feature_runtime_policy.cpp"

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
    "${PROJECT_ROOT}/src/frontend/dialects/clang/clang_dialect.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/gnu/gnu_dialect.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/conditional/builtin_probe_evaluator.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/conditional/clang_extension_provider.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/conditional/gnu_extension_provider.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/conditional/nonstandard_extension_manager.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/include_resolver.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/macro_table.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/predefined_macro_initializer.cpp" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: preprocess feature registry gates predefined macros and builtin probes"
