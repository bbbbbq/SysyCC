#include <cassert>
#include <memory>

#include "backend/ir/analysis/alias_analysis.hpp"
#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominance_frontier_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/function_effect_summary_analysis.hpp"
#include "backend/ir/analysis/memory_ssa_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_memory_ssa");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *left = function->create_basic_block<CoreIrBasicBlock>("left");
    auto *right = function->create_basic_block<CoreIrBasicBlock>("right");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *slot_a = function->create_stack_slot<CoreIrStackSlot>("a", i32_type, 4);
    auto *slot_b = function->create_stack_slot<CoreIrStackSlot>("b", i32_type, 4);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrStoreInst>(void_type, one, slot_a);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, cond, left, right);
    left->create_instruction<CoreIrStoreInst>(void_type, two, slot_b);
    left->create_instruction<CoreIrJumpInst>(void_type, merge);
    right->create_instruction<CoreIrJumpInst>(void_type, merge);
    auto *merge_load =
        merge->create_instruction<CoreIrLoadInst>(i32_type, "merge_load", slot_a);
    merge->create_instruction<CoreIrReturnInst>(void_type, merge_load);

    CoreIrCfgAnalysis cfg_analysis_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_analysis_runner.Run(*function);
    CoreIrDominatorTreeAnalysis dom_runner;
    const CoreIrDominatorTreeAnalysisResult dom = dom_runner.Run(*function, cfg);
    CoreIrDominanceFrontierAnalysis frontier_runner;
    const CoreIrDominanceFrontierAnalysisResult frontier =
        frontier_runner.Run(*function, cfg, dom);
    CoreIrFunctionEffectSummaryAnalysis effect_runner;
    const CoreIrFunctionEffectSummaryAnalysisResult effect =
        effect_runner.Run(*function);
    CoreIrAliasAnalysis alias_runner;
    const CoreIrAliasAnalysisResult alias = alias_runner.Run(*function);
    CoreIrMemorySSAAnalysis memory_ssa_runner;
    const CoreIrMemorySSAAnalysisResult memory_ssa = memory_ssa_runner.Run(
        *function, cfg, dom, frontier, effect, alias);

    assert(memory_ssa.get_live_on_entry() != nullptr);
    assert(memory_ssa.get_phis_for_block(merge).size() == 1);
    assert(memory_ssa.get_access_for_instruction(merge_load) != nullptr);
    CoreIrMemoryAccess *clobber = memory_ssa.get_clobbering_access(merge_load);
    assert(clobber != nullptr);
    assert(clobber->get_kind() == CoreIrMemoryAccessKind::Phi);
    return 0;
}
