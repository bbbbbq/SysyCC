cmake_minimum_required(VERSION 3.16)

project(SysyCC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands" FORCE)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(LEXER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/lexer)
set(PARSER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/parser)

bison_target(
    SysyParser
    ${PARSER_DIR}/parser.y
    ${PARSER_DIR}/parser_generated.cpp
    DEFINES_FILE ${PARSER_DIR}/parser.tab.h
)

flex_target(
    SysyLexer
    ${LEXER_DIR}/lexer.l
    ${LEXER_DIR}/lexer_scanner.cpp
    OPTIONS --warn
)

add_flex_bison_dependency(SysyLexer SysyParser)

add_executable(${PROJECT_NAME}
    ${BISON_SysyParser_OUTPUTS}
    ${FLEX_SysyLexer_OUTPUTS}
    src/backend/asm_gen/aarch64/aarch64_asm_backend.cpp
    src/backend/asm_gen/aarch64/aarch64_asm_gen_pass.cpp
    src/backend/ir/shared/core/core_ir_builder.cpp
    src/backend/ir/lower/lowering/core_ir_target_backend_factory.cpp
    src/backend/ir/lower/lowering/aarch64/core_ir_aarch64_target_backend.cpp
    src/backend/ir/lower/lowering/llvm/core_ir_llvm_target_backend.cpp
    src/backend/ir/lower/legacy/ir_backend_factory.cpp
    src/backend/ir/build/build_core_ir_pass.cpp
    src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp
    src/backend/ir/const_fold/core_ir_const_fold_pass.cpp
    src/backend/ir/dce/core_ir_dce_pass.cpp
    src/backend/ir/lower/legacy/gnu_function_attribute_lowering_handler.cpp
    src/backend/ir/lower/legacy/ir_builder.cpp
    src/backend/ir/lower/lower_ir_pass.cpp
    src/backend/ir/shared/printer/core_ir_raw_printer.cpp
    src/backend/ir/lower/legacy/llvm/llvm_ir_backend.cpp
    src/backend/ir/shared/detail/aggregate_layout.cpp
    src/backend/ir/shared/detail/ir_context.cpp
    src/backend/ir/shared/detail/symbol_value_map.cpp
    src/cli/cli.cpp
    src/common/diagnostic/diagnostic.cpp
    src/common/diagnostic/diagnostic_engine.cpp
    src/common/diagnostic/diagnostic_formatter.cpp
    src/common/integer_literal.cpp
    src/common/string_literal.cpp
    src/common/source_line_map.cpp
    src/common/source_location_service.cpp
    src/common/source_mapping_view.cpp
    src/common/source_manager.cpp
    src/compiler/complier.cpp
    src/compiler/pass/pass.cpp
    src/frontend/ast/ast_dump.cpp
    src/frontend/ast/ast_node.cpp
    src/frontend/ast/ast_pass.cpp
    src/frontend/ast/detail/ast_builder.cpp
    src/frontend/ast/detail/ast_builder_context.cpp
    src/frontend/ast/detail/ast_feature_validator.cpp
    src/frontend/ast/detail/parse_tree_matcher.cpp
    src/frontend/attribute/attribute_analyzer.cpp
    src/frontend/attribute/gnu_function_attribute_handler.cpp
    src/frontend/attribute/attribute_parser.cpp
    src/frontend/dialects/registries/ast_feature_registry.cpp
    src/frontend/dialects/registries/attribute_semantic_handler_registry.cpp
    src/frontend/dialects/registries/builtin_type_semantic_handler_registry.cpp
    src/frontend/dialects/core/dialect_manager.cpp
    src/frontend/dialects/registries/ir_extension_lowering_registry.cpp
    src/frontend/dialects/registries/ir_feature_registry.cpp
    src/frontend/dialects/registries/lexer_keyword_registry.cpp
    src/frontend/dialects/registries/parser_feature_registry.cpp
    src/frontend/dialects/registries/preprocess_directive_handler_registry.cpp
    src/frontend/dialects/registries/preprocess_feature_registry.cpp
    src/frontend/dialects/registries/preprocess_probe_handler_registry.cpp
    src/frontend/dialects/registries/semantic_feature_registry.cpp
    src/frontend/dialects/packs/builtin_types/builtin_type_extension_pack.cpp
    src/frontend/dialects/packs/c99/c99_dialect.cpp
    src/frontend/dialects/packs/clang/clang_dialect.cpp
    src/frontend/dialects/packs/gnu/gnu_dialect.cpp
    src/frontend/lexer/lexer.cpp
    src/frontend/parser/parser.cpp
    src/frontend/parser/parser_feature_validator.cpp
    src/frontend/parser/parser_runtime.cpp
    src/frontend/preprocess/preprocess.cpp
    src/frontend/preprocess/detail/conditional_stack.cpp
    src/frontend/preprocess/detail/conditional/builtin_probe_evaluator.cpp
    src/frontend/preprocess/detail/conditional/clang_extension_provider.cpp
    src/frontend/preprocess/detail/conditional/gnu_extension_provider.cpp
    src/frontend/preprocess/detail/conditional/nonstandard_extension_manager.cpp
    src/frontend/preprocess/detail/constant_expression_evaluator.cpp
    src/frontend/preprocess/detail/directive/directive_executor.cpp
    src/frontend/preprocess/detail/directive_parser.cpp
    src/frontend/preprocess/detail/file_loader.cpp
    src/frontend/preprocess/detail/include_resolver.cpp
    src/frontend/preprocess/detail/macro_expander.cpp
    src/frontend/preprocess/detail/macro_table.cpp
    src/frontend/preprocess/detail/predefined_macro_initializer.cpp
    src/frontend/preprocess/detail/preprocess_context.cpp
    src/frontend/preprocess/detail/preprocess_runtime.cpp
    src/frontend/preprocess/detail/source/source_mapper.cpp
    src/frontend/preprocess/detail/preprocess_session.cpp
    src/frontend/semantic/model/semantic_diagnostic.cpp
    src/frontend/semantic/model/semantic_model.cpp
    src/frontend/semantic/semantic_pass.cpp
    src/frontend/semantic/model/semantic_symbol.cpp
    src/frontend/semantic/model/semantic_type.cpp
    src/frontend/semantic/support/builtin_symbols.cpp
    src/frontend/semantic/type_system/constant_evaluator.cpp
    src/frontend/semantic/type_system/conversion_checker.cpp
    src/frontend/semantic/type_system/extended_builtin_type_semantic_handler.cpp
    src/frontend/semantic/type_system/integer_conversion_service.cpp
    src/frontend/semantic/analysis/decl_analyzer.cpp
    src/frontend/semantic/analysis/expr_analyzer.cpp
    src/frontend/semantic/support/scope_stack.cpp
    src/frontend/semantic/analysis/semantic_analyzer.cpp
    src/frontend/semantic/support/semantic_context.cpp
    src/frontend/semantic/analysis/stmt_analyzer.cpp
    src/frontend/semantic/type_system/type_resolver.cpp
    src/main.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
