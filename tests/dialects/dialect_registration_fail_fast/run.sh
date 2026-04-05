#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
GENERATED_DIR="${BUILD_DIR}/generated"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/dialect_registration_fail_fast"
TEST_SOURCE="${SCRIPT_DIR}/dialect_registration_fail_fast.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${TEST_BUILD_DIR}"

clang++ -std=c++17 -I"${GENERATED_DIR}" -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${PROJECT_ROOT}/src/backend/asm_gen/aarch64/aarch64_asm_backend.cpp" \
    "${PROJECT_ROOT}/src/backend/asm_gen/aarch64/aarch64_asm_gen_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/analysis/analysis_manager.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/analysis/alias_analysis.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/analysis/cfg_analysis.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/analysis/dominance_frontier_analysis.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/analysis/dominator_tree_analysis.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/analysis/function_effect_summary_analysis.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/analysis/loop_info_analysis.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/analysis/memory_ssa_analysis.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/analysis/promotable_stack_slot_analysis.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/effect/core_ir_effect.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/effect/core_ir_memory_location.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/shared/detail/aggregate_layout.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/lower/legacy/ir_backend_factory.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/build/build_core_ir_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/copy_propagation/core_ir_copy_propagation_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/const_fold/core_ir_const_fold_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/dce/core_ir_dce_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/gvn/core_ir_gvn_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/local_cse/core_ir_local_cse_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/loop_simplify/core_ir_loop_simplify_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/mem2reg/core_ir_mem2reg_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/sccp/core_ir_sccp_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/lower/legacy/gnu_function_attribute_lowering_handler.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/lower/legacy/ir_builder.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/lower/lower_ir_pass.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/lower/legacy/llvm/llvm_ir_backend.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/shared/core/core_ir_builder.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/lower/lowering/core_ir_target_backend_factory.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/lower/lowering/llvm/core_ir_llvm_target_backend.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/lower/lowering/aarch64/core_ir_aarch64_target_backend.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/shared/printer/core_ir_raw_printer.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/shared/detail/ir_context.cpp" \
    "${PROJECT_ROOT}/src/backend/ir/shared/detail/symbol_value_map.cpp" \
    "${PROJECT_ROOT}/src/cli/cli.cpp" \
    "${PROJECT_ROOT}/src/common/diagnostic/diagnostic.cpp" \
    "${PROJECT_ROOT}/src/common/diagnostic/diagnostic_engine.cpp" \
    "${PROJECT_ROOT}/src/common/integer_literal.cpp" \
    "${PROJECT_ROOT}/src/common/source_line_map.cpp" \
    "${PROJECT_ROOT}/src/common/source_location_service.cpp" \
    "${PROJECT_ROOT}/src/common/source_mapping_view.cpp" \
    "${PROJECT_ROOT}/src/common/source_manager.cpp" \
    "${PROJECT_ROOT}/src/common/string_literal.cpp" \
    "${PROJECT_ROOT}/src/compiler/complier.cpp" \
    "${PROJECT_ROOT}/src/compiler/pass/pass.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/ast_dump.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/ast_pass.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/ast_node.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/detail/ast_builder.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/detail/ast_builder_context.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/detail/ast_feature_validator.cpp" \
    "${PROJECT_ROOT}/src/frontend/ast/detail/parse_tree_matcher.cpp" \
    "${PROJECT_ROOT}/src/frontend/attribute/attribute_analyzer.cpp" \
    "${PROJECT_ROOT}/src/frontend/attribute/attribute_parser.cpp" \
    "${PROJECT_ROOT}/src/frontend/attribute/gnu_function_attribute_handler.cpp" \
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
    "${PROJECT_ROOT}/src/frontend/lexer/lexer.cpp" \
    "${GENERATED_DIR}/frontend/lexer/lexer_scanner.cpp" \
    "${PROJECT_ROOT}/src/frontend/parser/parser.cpp" \
    "${GENERATED_DIR}/frontend/parser/parser_generated.cpp" \
    "${PROJECT_ROOT}/src/frontend/parser/parser_feature_validator.cpp" \
    "${PROJECT_ROOT}/src/frontend/parser/parser_runtime.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/preprocess.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/conditional_stack.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/conditional/builtin_probe_evaluator.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/conditional/clang_extension_provider.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/conditional/gnu_extension_provider.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/conditional/nonstandard_extension_manager.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/constant_expression_evaluator.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/directive/directive_executor.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/directive_parser.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/file_loader.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/include_resolver.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/macro_expander.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/macro_table.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/predefined_macro_initializer.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/preprocess_context.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/preprocess_runtime.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/source/source_mapper.cpp" \
    "${PROJECT_ROOT}/src/frontend/preprocess/detail/preprocess_session.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/analysis/decl_analyzer.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/analysis/expr_analyzer.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/analysis/semantic_analyzer.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/analysis/stmt_analyzer.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/model/semantic_diagnostic.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/model/semantic_model.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/model/semantic_symbol.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/model/semantic_type.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/semantic_pass.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/support/builtin_symbols.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/support/scope_stack.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/support/semantic_context.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/constant_evaluator.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/conversion_checker.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/extended_builtin_type_semantic_handler.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/integer_conversion_service.cpp" \
    "${PROJECT_ROOT}/src/frontend/semantic/type_system/type_resolver.cpp" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: dialect registration conflicts fail fast before pipeline execution"
