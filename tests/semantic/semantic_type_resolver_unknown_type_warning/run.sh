#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/semantic_type_resolver_unknown_type_warning"
TEST_SOURCE="${SCRIPT_DIR}/semantic_type_resolver_unknown_type_warning.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

mkdir -p "${TEST_BUILD_DIR}"

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${PROJECT_ROOT}/src/common/integer_literal.cpp" \
    "${PROJECT_ROOT}/src/common/source_span.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/ast_node.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/core/dialect_manager.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/ast_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/attribute_semantic_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/builtin_type_semantic_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/ir_extension_lowering_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/ir_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/lexer_keyword_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/parser_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/preprocess_directive_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/preprocess_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/preprocess_probe_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/semantic_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/packs/builtin_types/builtin_type_extension_pack.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/packs/c99/c99_dialect.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/packs/clang/clang_dialect.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/packs/gnu/gnu_dialect.cpp" \
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
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/type_resolver.cpp" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: TypeResolver records a warning for UnknownTypeNode fallback resolution"
