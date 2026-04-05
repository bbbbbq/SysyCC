#include <cassert>
#include <memory>

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

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_dominator_tree_analysis");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *left = function->create_basic_block<CoreIrBasicBlock>("left");
    auto *right = function->create_basic_block<CoreIrBasicBlock>("right");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *dead = function->create_basic_block<CoreIrBasicBlock>("dead");

    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    entry->create_instruction<CoreIrCondJumpInst>(void_type, one, left, right);
    left->create_instruction<CoreIrJumpInst>(void_type, exit);
    right->create_instruction<CoreIrJumpInst>(void_type, exit);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    dead->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrAnalysisManager *analysis_manager =
        compiler_context.get_core_ir_build_result()->get_analysis_manager();
    assert(analysis_manager != nullptr);

    const CoreIrDominatorTreeAnalysisResult &dominator_tree =
        analysis_manager->get_or_compute<CoreIrDominatorTreeAnalysis>(*function);

    assert(dominator_tree.is_reachable(entry));
    assert(dominator_tree.is_reachable(left));
    assert(dominator_tree.is_reachable(right));
    assert(dominator_tree.is_reachable(exit));
    assert(!dominator_tree.is_reachable(dead));

    assert(dominator_tree.dominates(entry, entry));
    assert(dominator_tree.dominates(entry, left));
    assert(dominator_tree.dominates(entry, right));
    assert(dominator_tree.dominates(entry, exit));
    assert(!dominator_tree.dominates(left, right));
    assert(!dominator_tree.dominates(right, left));
    assert(!dominator_tree.dominates(left, exit));
    assert(!dominator_tree.dominates(right, exit));

    assert(dominator_tree.get_immediate_dominator(entry) == nullptr);
    assert(dominator_tree.get_immediate_dominator(left) == entry);
    assert(dominator_tree.get_immediate_dominator(right) == entry);
    assert(dominator_tree.get_immediate_dominator(exit) == entry);
    assert(dominator_tree.get_immediate_dominator(dead) == nullptr);
    return 0;
}
