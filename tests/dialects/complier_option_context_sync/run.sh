#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/complier_option_context_sync"
TEST_SOURCE="${SCRIPT_DIR}/complier_option_context_sync.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${TEST_BUILD_DIR}"

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${PROJECT_ROOT}/src/common/diagnostic/diagnostic.cpp" \
    "${PROJECT_ROOT}/src/common/diagnostic/diagnostic_engine.cpp" \
    "${PROJECT_ROOT}/src/common/source_line_map.cpp" \
    "${PROJECT_ROOT}/src/common/source_location_service.cpp" \
    "${PROJECT_ROOT}/src/common/source_mapping_view.cpp" \
    "${PROJECT_ROOT}/src/common/source_manager.cpp" \
    "${PROJECT_ROOT}/src/compiler/complier.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/ast_feature_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/attribute_semantic_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/registries/builtin_type_semantic_handler_registry.cpp" \
    "${PROJECT_ROOT}/src/frontend/dialects/core/dialect_manager.cpp" \
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
    -L"${BUILD_DIR}" \
    -lsysycc_riscv64_codegen \
    -Wl,-rpath,"${BUILD_DIR}" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: compiler option synchronization uses one shared context update path"
