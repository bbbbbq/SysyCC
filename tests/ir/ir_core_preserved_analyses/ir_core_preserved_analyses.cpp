#include <cassert>
#include <memory>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "compiler/pass/pass.hpp"

using namespace sysycc;

namespace {

std::unique_ptr<CompilerContext> make_copyprop_context() {
    auto context = std::make_unique<CompilerContext>();
    auto ir_context = std::make_unique<CoreIrContext>();
    auto *void_type = ir_context->create_type<CoreIrVoidType>();
    auto *i32_type = ir_context->create_type<CoreIrIntegerType>(32);
    auto *function_type = ir_context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        ir_context->create_module<CoreIrModule>("copyprop_preserved");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *seven = ir_context->create_constant<CoreIrConstantInt>(i32_type, 7);
    entry->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
    auto *load0 =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "a", slot);
    auto *load1 =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "b", slot);
    entry->create_instruction<CoreIrReturnInst>(void_type, load1);
    assert(load0 != nullptr);
    context->set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(ir_context), module));
    return context;
}

std::unique_ptr<CompilerContext> make_simplify_cfg_context() {
    auto context = std::make_unique<CompilerContext>();
    auto ir_context = std::make_unique<CoreIrContext>();
    auto *void_type = ir_context->create_type<CoreIrVoidType>();
    auto *i32_type = ir_context->create_type<CoreIrIntegerType>(32);
    auto *function_type = ir_context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        ir_context->create_module<CoreIrModule>("simplify_cfg_preserved");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *middle = function->create_basic_block<CoreIrBasicBlock>("middle");
    auto *seven = ir_context->create_constant<CoreIrConstantInt>(i32_type, 7);
    entry->create_instruction<CoreIrJumpInst>(void_type, middle);
    middle->create_instruction<CoreIrReturnInst>(void_type, seven);
    context->set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(ir_context), module));
    return context;
}

std::unique_ptr<CompilerContext> make_mem2reg_context() {
    auto context = std::make_unique<CompilerContext>();
    auto ir_context = std::make_unique<CoreIrContext>();
    auto *void_type = ir_context->create_type<CoreIrVoidType>();
    auto *i32_type = ir_context->create_type<CoreIrIntegerType>(32);
    auto *function_type = ir_context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = ir_context->create_module<CoreIrModule>("mem2reg_preserved");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *seven = ir_context->create_constant<CoreIrConstantInt>(i32_type, 7);
    entry->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
    auto *load = entry->create_instruction<CoreIrLoadInst>(i32_type, "x", slot);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);
    context->set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(ir_context), module));
    return context;
}

std::unique_ptr<CompilerContext> make_function_attrs_context() {
    auto context = std::make_unique<CompilerContext>();
    auto ir_context = std::make_unique<CoreIrContext>();
    auto *void_type = ir_context->create_type<CoreIrVoidType>();
    auto *i32_type = ir_context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = ir_context->create_type<CoreIrPointerType>(i32_type);
    auto *callee_type = ir_context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *caller_type = ir_context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        ir_context->create_module<CoreIrModule>("function_attrs_preserved");

    auto *callee =
        module->create_function<CoreIrFunction>("read_ptr", callee_type, false);
    auto *param = callee->create_parameter<CoreIrParameter>(ptr_i32_type, "p");
    auto *callee_entry = callee->create_basic_block<CoreIrBasicBlock>("entry");
    auto *load = callee_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "load", param);
    callee_entry->create_instruction<CoreIrReturnInst>(void_type, load);

    auto *caller =
        module->create_function<CoreIrFunction>("main", caller_type, false);
    auto *entry = caller->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        caller->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr", slot);
    auto *call = entry->create_instruction<CoreIrCallInst>(
        i32_type, "call", "read_ptr", callee_type,
        std::vector<CoreIrValue *>{addr});
    entry->create_instruction<CoreIrReturnInst>(void_type, call);

    context->set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(ir_context), module));
    return context;
}

void test_copyprop_preserves_cfg_but_invalidates_memory() {
    auto context = make_copyprop_context();
    auto *build_result = context->get_core_ir_build_result();
    auto *function = build_result->get_module()->get_functions().front().get();
    auto *analysis_manager = build_result->get_analysis_manager();
    (void)analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
    assert(analysis_manager->get_compute_count(*function,
                                               CoreIrAnalysisKind::Cfg) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::AliasAnalysis) == 1);

    PassManager pass_manager;
    pass_manager.AddPass(std::make_unique<CoreIrCopyPropagationPass>());
    assert(pass_manager.Run(*context).ok);

    (void)analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
    assert(analysis_manager->get_compute_count(*function,
                                               CoreIrAnalysisKind::Cfg) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::AliasAnalysis) == 2);
}

void test_simplify_cfg_invalidates_cfg_family() {
    auto context = make_simplify_cfg_context();
    auto *build_result = context->get_core_ir_build_result();
    auto *function = build_result->get_module()->get_functions().front().get();
    auto *analysis_manager = build_result->get_analysis_manager();
    (void)analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    assert(analysis_manager->get_compute_count(*function,
                                               CoreIrAnalysisKind::Cfg) == 1);

    PassManager pass_manager;
    pass_manager.AddPass(std::make_unique<CoreIrSimplifyCfgPass>());
    assert(pass_manager.Run(*context).ok);

    (void)analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    assert(analysis_manager->get_compute_count(*function,
                                               CoreIrAnalysisKind::Cfg) == 2);
}

void test_mem2reg_preserves_cfg_but_invalidates_memory() {
    auto context = make_mem2reg_context();
    auto *build_result = context->get_core_ir_build_result();
    auto *function = build_result->get_module()->get_functions().front().get();
    auto *analysis_manager = build_result->get_analysis_manager();
    (void)analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
    assert(analysis_manager->get_compute_count(*function,
                                               CoreIrAnalysisKind::Cfg) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::AliasAnalysis) == 1);

    PassManager pass_manager;
    pass_manager.AddPass(std::make_unique<CoreIrMem2RegPass>());
    assert(pass_manager.Run(*context).ok);

    (void)analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
    assert(analysis_manager->get_compute_count(*function,
                                               CoreIrAnalysisKind::Cfg) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::AliasAnalysis) == 2);
}

void test_function_attrs_invalidation_recomputes_escape_dependents() {
    auto context = make_function_attrs_context();
    auto *build_result = context->get_core_ir_build_result();
    auto *module = build_result->get_module();
    auto *function = module->find_function("main");
    auto *analysis_manager = build_result->get_analysis_manager();
    assert(function != nullptr);

    (void)analysis_manager->get_or_compute<CoreIrEscapeAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrMemorySSAAnalysis>(*function);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::EscapeAnalysis) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::AliasAnalysis) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::MemorySSA) == 1);

    analysis_manager->invalidate(*module, CoreIrAnalysisKind::FunctionAttrs);

    (void)analysis_manager->get_or_compute<CoreIrEscapeAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrMemorySSAAnalysis>(*function);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::EscapeAnalysis) == 2);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::AliasAnalysis) == 2);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::MemorySSA) == 2);
}

} // namespace

int main() {
    test_copyprop_preserves_cfg_but_invalidates_memory();
    test_simplify_cfg_invalidates_cfg_family();
    test_mem2reg_preserves_cfg_but_invalidates_memory();
    test_function_attrs_invalidation_recomputes_escape_dependents();
    return 0;
}
