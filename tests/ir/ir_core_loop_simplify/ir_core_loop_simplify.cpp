#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
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

std::size_t compute_latch_count(CoreIrFunction &function,
                                CoreIrBasicBlock *header) {
    CoreIrCfgAnalysis cfg_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_runner.Run(function);
    CoreIrDominatorTreeAnalysis dom_runner;
    const CoreIrDominatorTreeAnalysisResult dom = dom_runner.Run(function, cfg);
    CoreIrLoopInfoAnalysis loop_runner;
    const CoreIrLoopInfoAnalysisResult loop_info = loop_runner.Run(function, cfg, dom);
    CoreIrLoopInfo *loop = loop_info.get_loop_for_header(header);
    return loop == nullptr ? 0 : loop->get_latches().size();
}

void test_preheader_creation() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("loop_preheader");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *outside = function->create_basic_block<CoreIrBasicBlock>("outside");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, cond, header, outside);
    outside->create_instruction<CoreIrJumpInst>(void_type, header);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    CoreIrLoopSimplifyPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrCfgAnalysis cfg_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_runner.Run(*function);
    std::size_t outside_pred_count = 0;
    for (CoreIrBasicBlock *pred : cfg.get_predecessors(header)) {
        if (pred != body) {
            ++outside_pred_count;
        }
    }
    assert(outside_pred_count == 1);
}

void test_single_latch_creation() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("loop_latch");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body1 = function->create_basic_block<CoreIrBasicBlock>("body1");
    auto *body2 = function->create_basic_block<CoreIrBasicBlock>("body2");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body1, body2);
    body1->create_instruction<CoreIrCondJumpInst>(void_type, cond, header, exit);
    body2->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    CoreIrLoopSimplifyPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(compute_latch_count(*function, header) == 1);
}

void test_dedicated_exit_creation() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("loop_exit");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *outside = function->create_basic_block<CoreIrBasicBlock>("outside");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *shared_exit = function->create_basic_block<CoreIrBasicBlock>("shared_exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    outside->create_instruction<CoreIrJumpInst>(void_type, shared_exit);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, shared_exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    shared_exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    CoreIrLoopSimplifyPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrCfgAnalysis cfg_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_runner.Run(*function);
    std::size_t pred_count = cfg.get_predecessor_count(shared_exit);
    assert(pred_count >= 2);
}

} // namespace

int main() {
    test_preheader_creation();
    test_single_latch_creation();
    test_dedicated_exit_creation();
    return 0;
}
