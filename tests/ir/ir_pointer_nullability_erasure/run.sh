#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/ir_pointer_nullability_erasure"
TEST_SOURCE="${SCRIPT_DIR}/ir_pointer_nullability_erasure.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

mkdir -p "${TEST_BUILD_DIR}"

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${PROJECT_ROOT}/src/backend/ir/detail/aggregate_layout.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/llvm/llvm_ir_backend.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/detail/ir_context.cpp" \
    "${PROJECT_ROOT}/src/common/integer_literal.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/ast_node.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/builtin_type_semantic_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/semantic_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/model/semantic_diagnostic.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/model/semantic_model.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/model/semantic_symbol.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/model/semantic_type.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/support/semantic_context.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/support/scope_stack.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/constant_evaluator.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/conversion_checker.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/extended_builtin_type_semantic_handler.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/integer_conversion_service.cpp" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: LLVM IR erases pointer nullability annotations into ordinary ptr"
