#include <cassert>
#include <memory>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominance_frontier_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_dominance_frontier_analysis");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *left = function->create_basic_block<CoreIrBasicBlock>("left");
    auto *right = function->create_basic_block<CoreIrBasicBlock>("right");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *loop_header =
        function->create_basic_block<CoreIrBasicBlock>("loop_header");
    auto *loop_body = function->create_basic_block<CoreIrBasicBlock>("loop_body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *dead = function->create_basic_block<CoreIrBasicBlock>("dead");
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    entry->create_instruction<CoreIrCondJumpInst>(void_type, one, left, right);
    left->create_instruction<CoreIrJumpInst>(void_type, merge);
    right->create_instruction<CoreIrJumpInst>(void_type, merge);
    merge->create_instruction<CoreIrJumpInst>(void_type, loop_header);
    loop_header->create_instruction<CoreIrCondJumpInst>(void_type, one, loop_body, exit);
    loop_body->create_instruction<CoreIrJumpInst>(void_type, loop_header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    dead->create_instruction<CoreIrReturnInst>(void_type, zero);

    CoreIrCfgAnalysis cfg_analysis_runner;
    const CoreIrCfgAnalysisResult cfg_analysis = cfg_analysis_runner.Run(*function);
    CoreIrDominatorTreeAnalysis dominator_tree_runner;
    const CoreIrDominatorTreeAnalysisResult dominator_tree =
        dominator_tree_runner.Run(*function, cfg_analysis);
    CoreIrDominanceFrontierAnalysis frontier_runner;
    const CoreIrDominanceFrontierAnalysisResult frontier =
        frontier_runner.Run(*function, cfg_analysis, dominator_tree);

    assert(frontier.has_frontier_edge(left, merge));
    assert(frontier.has_frontier_edge(right, merge));
    assert(frontier.has_frontier_edge(loop_body, loop_header));
    assert(!frontier.has_frontier_edge(entry, merge));
    assert(frontier.get_frontier(dead).empty());
    return 0;
}
