#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

bool contains_block(const std::vector<CoreIrBasicBlock *> &blocks,
                    CoreIrBasicBlock *block) {
    return std::find(blocks.begin(), blocks.end(), block) != blocks.end();
}

} // namespace

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_cfg_analysis");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *else_block = function->create_basic_block<CoreIrBasicBlock>("else");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *dead = function->create_basic_block<CoreIrBasicBlock>("dead");

    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    entry->create_instruction<CoreIrCondJumpInst>(void_type, one, then_block,
                                                  else_block);
    then_block->create_instruction<CoreIrJumpInst>(void_type, exit);
    else_block->create_instruction<CoreIrJumpInst>(void_type, exit);
    exit->create_instruction<CoreIrReturnInst>(void_type, seven);
    dead->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrAnalysisManager *analysis_manager =
        compiler_context.get_core_ir_build_result()->get_analysis_manager();
    assert(analysis_manager != nullptr);

    const CoreIrCfgAnalysisResult &cfg_analysis =
        analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);

    assert(cfg_analysis.get_entry_block() == entry);
    assert(cfg_analysis.is_reachable(entry));
    assert(cfg_analysis.is_reachable(then_block));
    assert(cfg_analysis.is_reachable(else_block));
    assert(cfg_analysis.is_reachable(exit));
    assert(!cfg_analysis.is_reachable(dead));

    const auto &entry_successors = cfg_analysis.get_successors(entry);
    assert(entry_successors.size() == 2);
    assert(contains_block(entry_successors, then_block));
    assert(contains_block(entry_successors, else_block));

    const auto &then_predecessors = cfg_analysis.get_predecessors(then_block);
    assert(then_predecessors.size() == 1);
    assert(then_predecessors.front() == entry);

    const auto &exit_predecessors = cfg_analysis.get_predecessors(exit);
    assert(exit_predecessors.size() == 2);
    assert(contains_block(exit_predecessors, then_block));
    assert(contains_block(exit_predecessors, else_block));
    assert(cfg_analysis.get_predecessor_count(exit) == 2);

    const auto &dead_successors = cfg_analysis.get_successors(dead);
    assert(dead_successors.empty());
    assert(cfg_analysis.has_block(dead));
    return 0;
}
