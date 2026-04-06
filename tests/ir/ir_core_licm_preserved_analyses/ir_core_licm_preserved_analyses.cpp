#include <cassert>
#include <memory>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/licm/core_ir_licm_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "compiler/pass/pass.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_licm_preserved_analyses");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *sum = header->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", one, two);
    auto *doubled = header->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i32_type, "doubled", sum, two);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, doubled);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrBuildResult *build_result = compiler_context.get_core_ir_build_result();
    assert(build_result != nullptr);
    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    assert(analysis_manager != nullptr);

    (void)analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrMemorySSAAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrFunctionEffectSummaryAnalysis>(
        *function);

    assert(analysis_manager->get_compute_count(*function, CoreIrAnalysisKind::Cfg) ==
           1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::LoopInfo) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::AliasAnalysis) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::MemorySSA) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::FunctionEffectSummary) == 1);

    PassManager pass_manager;
    pass_manager.AddPass(std::make_unique<CoreIrLicmPass>());
    assert(pass_manager.Run(compiler_context).ok);

    (void)analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrMemorySSAAnalysis>(*function);
    (void)analysis_manager->get_or_compute<CoreIrFunctionEffectSummaryAnalysis>(
        *function);

    assert(analysis_manager->get_compute_count(*function, CoreIrAnalysisKind::Cfg) ==
           1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::LoopInfo) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::AliasAnalysis) == 1);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::MemorySSA) == 2);
    assert(analysis_manager->get_compute_count(
               *function, CoreIrAnalysisKind::FunctionEffectSummary) == 1);
    return 0;
}
