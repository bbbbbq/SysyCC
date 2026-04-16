cmake_minimum_required(VERSION 3.16)

project(SysyCC VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands" FORCE)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

if(EXISTS "/opt/homebrew/opt/llvm/lib/cmake/llvm/LLVMConfig.cmake")
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/llvm/lib/cmake/llvm")
endif()

find_package(LLVM CONFIG REQUIRED)

set(LEXER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/lexer)
set(PARSER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/parser)
set(GENERATED_FRONTEND_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/frontend)
set(GENERATED_LEXER_DIR ${GENERATED_FRONTEND_DIR}/lexer)
set(GENERATED_PARSER_DIR ${GENERATED_FRONTEND_DIR}/parser)

file(MAKE_DIRECTORY ${GENERATED_LEXER_DIR})
file(MAKE_DIRECTORY ${GENERATED_PARSER_DIR})

bison_target(
    SysyParser
    ${PARSER_DIR}/parser.y
    ${GENERATED_PARSER_DIR}/parser_generated.cpp
    DEFINES_FILE ${GENERATED_PARSER_DIR}/parser.tab.h
)

flex_target(
    SysyLexer
    ${LEXER_DIR}/lexer.l
    ${GENERATED_LEXER_DIR}/lexer_scanner.cpp
    COMPILE_FLAGS "--warn"
)

add_flex_bison_dependency(SysyLexer SysyParser)

set(SYSYCC_AARCH64_CODEGEN_SOURCES
    src/backend/asm_gen/aarch64/api/aarch64_codegen_api.cpp
    src/backend/asm_gen/aarch64/api/aarch64_llvm_bitcode_loader.cpp
    src/backend/asm_gen/aarch64/api/aarch64_llvm_import_instruction_parse_support.cpp
    src/backend/asm_gen/aarch64/api/aarch64_llvm_import_constant_support.cpp
    src/backend/asm_gen/aarch64/api/aarch64_llvm_import_parse_common_support.cpp
    src/backend/asm_gen/aarch64/api/aarch64_llvm_import_type_support.cpp
    src/backend/asm_gen/aarch64/api/aarch64_restricted_llvm_ir_parser.cpp
    src/backend/asm_gen/aarch64/model/aarch64_codegen_model.cpp
    src/backend/asm_gen/aarch64/passes/aarch64_emission_pass.cpp
    src/backend/asm_gen/aarch64/passes/aarch64_frame_finalize_pass.cpp
    src/backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.cpp
    src/backend/asm_gen/aarch64/passes/aarch64_machine_lowering_pass.cpp
    src/backend/asm_gen/aarch64/passes/aarch64_register_allocation_pass.cpp
    src/backend/asm_gen/aarch64/passes/aarch64_spill_rewrite_pass.cpp
    src/backend/asm_gen/aarch64/support/aarch64_frame_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_aggregate_abi_move_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_backend_context.cpp
    src/backend/asm_gen/aarch64/support/aarch64_address_materialization_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_address_value_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_call_abi_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_call_return_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_binary_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_branch_instruction_encoding_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_cast_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_codegen_backend_bridge.cpp
    src/backend/asm_gen/aarch64/support/aarch64_compare_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_debug_object_writer_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_elf_object_writer_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_elf_section_serializer_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_float_helper_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_fp_instruction_encoding_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_function_lowering_facade.cpp
    src/backend/asm_gen/aarch64/support/aarch64_function_shell_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_function_planning_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_function_boundary_abi_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_global_data_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_import_model_core_ir_bridge.cpp
    src/backend/asm_gen/aarch64/support/aarch64_instruction_encoding_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_object_image_builder_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_memory_access_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_memory_instruction_encoding_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_memory_instruction_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_memory_value_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_phi_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_scalar_instruction_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_text_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_type_layout_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_unary_lowering_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_value_conversion_support.cpp
    src/backend/asm_gen/aarch64/support/aarch64_value_materialization_support.cpp
    src/common/diagnostic/diagnostic.cpp
    src/common/diagnostic/diagnostic_engine.cpp
    src/common/source_span.cpp
)

add_library(sysycc_aarch64_codegen SHARED
    ${SYSYCC_AARCH64_CODEGEN_SOURCES}
)

target_include_directories(sysycc_aarch64_codegen
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

llvm_map_components_to_libnames(SYSYCC_AARCH64_LLVM_LIBS
    BitReader
    BitWriter
    Core
    IRReader
    Support
)
target_include_directories(sysycc_aarch64_codegen
    PRIVATE
        ${LLVM_INCLUDE_DIRS}
)
target_compile_definitions(sysycc_aarch64_codegen
    PRIVATE
        ${LLVM_DEFINITIONS}
        SYSYCC_AARCH64_HAVE_LLVM_BITCODE_READER=1
)
target_link_libraries(sysycc_aarch64_codegen
    PRIVATE
        ${SYSYCC_AARCH64_LLVM_LIBS}
)

set_target_properties(sysycc_aarch64_codegen
    PROPERTIES
        OUTPUT_NAME sysycc_aarch64_codegen
)

add_executable(sysycc-aarch64c
    src/tools/sysycc_aarch64c_main.cpp
)

target_include_directories(sysycc-aarch64c
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sysycc-aarch64c
    PRIVATE
        sysycc_aarch64_codegen
)

add_executable(${PROJECT_NAME}
    ${BISON_SysyParser_OUTPUTS}
    ${FLEX_SysyLexer_OUTPUTS}
    src/backend/asm_gen/aarch64/aarch64_asm_gen_pass.cpp
    src/backend/ir/analysis/analysis_manager.cpp
    src/backend/ir/analysis/alias_analysis.cpp
    src/backend/ir/analysis/call_graph_analysis.cpp
    src/backend/ir/analysis/cfg_analysis.cpp
    src/backend/ir/analysis/dominance_frontier_analysis.cpp
    src/backend/ir/analysis/dominator_tree_analysis.cpp
    src/backend/ir/analysis/escape_analysis.cpp
    src/backend/ir/analysis/function_effect_summary_analysis.cpp
    src/backend/ir/analysis/function_attrs_analysis.cpp
    src/backend/ir/analysis/induction_var_analysis.cpp
    src/backend/ir/analysis/loop_info_analysis.cpp
    src/backend/ir/analysis/memory_ssa_analysis.cpp
    src/backend/ir/analysis/promotable_stack_slot_analysis.cpp
    src/backend/ir/analysis/scalar_evolution_lite_analysis.cpp
    src/backend/ir/effect/core_ir_effect.cpp
    src/backend/ir/effect/core_ir_memory_location.cpp
    src/backend/ir/effect/core_ir_memory_query.cpp
    src/backend/ir/shared/core/core_ir_builder.cpp
    src/backend/ir/lower/lowering/core_ir_target_backend_factory.cpp
    src/backend/ir/lower/lowering/llvm/core_ir_llvm_target_backend.cpp
    src/backend/ir/lower/legacy/ir_backend_factory.cpp
    src/backend/ir/build/build_core_ir_pass.cpp
    src/backend/ir/function_attrs/core_ir_function_attrs_pass.cpp
    src/backend/ir/global_dce/core_ir_global_dce_pass.cpp
    src/backend/ir/inliner/core_ir_inliner_pass.cpp
    src/backend/ir/ipsccp/core_ir_ipsccp_pass.cpp
    src/backend/ir/argument_promotion/core_ir_argument_promotion_pass.cpp
    src/backend/ir/canonicalize/core_ir_canonicalize_pass.cpp
    src/backend/ir/copy_propagation/core_ir_copy_propagation_pass.cpp
    src/backend/ir/const_fold/core_ir_const_fold_pass.cpp
    src/backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.cpp
    src/backend/ir/dce/core_ir_dce_pass.cpp
    src/backend/ir/gvn/core_ir_gvn_pass.cpp
    src/backend/ir/if_conversion/core_ir_if_conversion_pass.cpp
    src/backend/ir/instcombine/core_ir_instcombine_pass.cpp
    src/backend/ir/indvar_simplify/core_ir_indvar_simplify_pass.cpp
    src/backend/ir/remainder_strength_reduction/core_ir_remainder_strength_reduction_pass.cpp
    src/backend/ir/lcssa/core_ir_lcssa_pass.cpp
    src/backend/ir/licm/core_ir_licm_pass.cpp
    src/backend/ir/local_cse/core_ir_local_cse_pass.cpp
    src/backend/ir/loop_cursor_promotion/core_ir_loop_cursor_promotion_pass.cpp
    src/backend/ir/loop_idiom/core_ir_loop_idiom_pass.cpp
    src/backend/ir/loop_memory_promotion/core_ir_loop_memory_promotion_pass.cpp
    src/backend/ir/loop_rotate/core_ir_loop_rotate_pass.cpp
    src/backend/ir/loop_simplify/core_ir_loop_simplify_pass.cpp
    src/backend/ir/loop_unroll/core_ir_loop_unroll_pass.cpp
    src/backend/ir/loop_vectorize/core_ir_loop_vectorize_pass.cpp
    src/backend/ir/mem2reg/core_ir_mem2reg_pass.cpp
    src/backend/ir/pipeline/core_ir_pass_pipeline.cpp
    src/backend/ir/sccp/core_ir_sccp_pass.cpp
    src/backend/ir/simple_loop_unswitch/core_ir_simple_loop_unswitch_pass.cpp
    src/backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.cpp
    src/backend/ir/tail_recursion_elimination/core_ir_tail_recursion_elimination_pass.cpp
    src/backend/ir/sroa/core_ir_sroa_pass.cpp
    src/backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.cpp
    src/backend/ir/verify/core_ir_verifier.cpp
    src/backend/ir/lower/legacy/gnu_function_attribute_lowering_handler.cpp
    src/backend/ir/lower/legacy/ir_builder.cpp
    src/backend/ir/lower/lower_ir_pass.cpp
    src/backend/ir/shared/printer/core_ir_raw_printer.cpp
    src/backend/ir/lower/legacy/llvm/llvm_ir_backend.cpp
    src/backend/ir/shared/detail/aggregate_layout.cpp
    src/backend/ir/shared/detail/core_ir_clone_utils.cpp
    src/backend/ir/shared/detail/core_ir_rewrite_utils.cpp
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
    src/common/source_span.cpp
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
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        sysycc_aarch64_codegen
)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
        OUTPUT_NAME compiler
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${PROJECT_NAME}>"
        "${CMAKE_CURRENT_BINARY_DIR}/SysyCC"
    VERBATIM
)

install(TARGETS sysycc_aarch64_codegen sysycc-aarch64c
    EXPORT SysyCCAArch64CodegenTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
    FILES
        src/backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp
    DESTINATION
        ${CMAKE_INSTALL_INCLUDEDIR}/backend/asm_gen/aarch64/api
)

install(EXPORT SysyCCAArch64CodegenTargets
    FILE SysyCCAArch64CodegenTargets.cmake
    NAMESPACE SysyCC::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SysyCCAArch64Codegen
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/SysyCCAArch64CodegenConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/SysyCCAArch64CodegenConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/SysyCCAArch64Codegen
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/SysyCCAArch64CodegenConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/SysyCCAArch64CodegenConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/SysyCCAArch64CodegenConfigVersion.cmake
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/cmake/SysyCCAArch64Codegen
)
