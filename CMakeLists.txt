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
    src/backend/ir/ir_backend_factory.cpp
    src/backend/ir/ir_builder.cpp
    src/backend/ir/ir_pass.cpp
    src/backend/ir/llvm/llvm_ir_backend.cpp
    src/backend/ir/detail/ir_context.cpp
    src/backend/ir/detail/symbol_value_map.cpp
    src/cli/cli.cpp
    src/common/diagnostic/diagnostic.cpp
    src/common/diagnostic/diagnostic_engine.cpp
    src/common/source_span.cpp
    src/compiler/complier.cpp
    src/compiler/pass/pass.cpp
    src/frontend/ast/ast_dump.cpp
    src/frontend/ast/ast_node.cpp
    src/frontend/ast/ast_pass.cpp
    src/frontend/ast/detail/ast_builder.cpp
    src/frontend/ast/detail/ast_builder_context.cpp
    src/frontend/ast/detail/parse_tree_matcher.cpp
    src/frontend/lexer/lexer.cpp
    src/frontend/parser/parser.cpp
    src/frontend/parser/parser_runtime.cpp
    src/frontend/preprocess/preprocess.cpp
    src/frontend/preprocess/detail/conditional_stack.cpp
    src/frontend/preprocess/detail/constant_expression_evaluator.cpp
    src/frontend/preprocess/detail/directive_parser.cpp
    src/frontend/preprocess/detail/file_loader.cpp
    src/frontend/preprocess/detail/include_resolver.cpp
    src/frontend/preprocess/detail/macro_expander.cpp
    src/frontend/preprocess/detail/macro_table.cpp
    src/frontend/preprocess/detail/preprocess_runtime.cpp
    src/frontend/preprocess/detail/preprocess_session.cpp
    src/frontend/semantic/model/semantic_diagnostic.cpp
    src/frontend/semantic/model/semantic_model.cpp
    src/frontend/semantic/semantic_pass.cpp
    src/frontend/semantic/model/semantic_symbol.cpp
    src/frontend/semantic/model/semantic_type.cpp
    src/frontend/semantic/support/builtin_symbols.cpp
    src/frontend/semantic/type_system/constant_evaluator.cpp
    src/frontend/semantic/type_system/conversion_checker.cpp
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
