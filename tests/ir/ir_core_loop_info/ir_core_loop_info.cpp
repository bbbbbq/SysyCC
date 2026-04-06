#include <cassert>
#include <memory>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

using namespace sysycc;

namespace {

void test_single_loop() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("single_loop");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CoreIrCfgAnalysis cfg_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_runner.Run(*function);
    CoreIrDominatorTreeAnalysis dom_runner;
    const CoreIrDominatorTreeAnalysisResult dom = dom_runner.Run(*function, cfg);
    CoreIrLoopInfoAnalysis loop_runner;
    const CoreIrLoopInfoAnalysisResult loop_info = loop_runner.Run(*function, cfg, dom);
    assert(loop_info.get_loops().size() == 1);
    const CoreIrLoopInfo *loop = loop_info.get_loops().front().get();
    assert(loop->get_header() == header);
    assert(loop->get_preheader() == entry);
    assert(loop->get_latches().find(body) != loop->get_latches().end());
    assert(loop->get_exit_blocks().find(exit) != loop->get_exit_blocks().end());
}

void test_nested_loops() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("nested_loops");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *outer_header = function->create_basic_block<CoreIrBasicBlock>("outer_header");
    auto *inner_header = function->create_basic_block<CoreIrBasicBlock>("inner_header");
    auto *inner_body = function->create_basic_block<CoreIrBasicBlock>("inner_body");
    auto *outer_latch = function->create_basic_block<CoreIrBasicBlock>("outer_latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    entry->create_instruction<CoreIrJumpInst>(void_type, outer_header);
    outer_header->create_instruction<CoreIrCondJumpInst>(void_type, cond, inner_header,
                                                         exit);
    inner_header->create_instruction<CoreIrCondJumpInst>(void_type, cond, inner_body,
                                                         outer_latch);
    inner_body->create_instruction<CoreIrJumpInst>(void_type, inner_header);
    outer_latch->create_instruction<CoreIrJumpInst>(void_type, outer_header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CoreIrCfgAnalysis cfg_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_runner.Run(*function);
    CoreIrDominatorTreeAnalysis dom_runner;
    const CoreIrDominatorTreeAnalysisResult dom = dom_runner.Run(*function, cfg);
    CoreIrLoopInfoAnalysis loop_runner;
    const CoreIrLoopInfoAnalysisResult loop_info = loop_runner.Run(*function, cfg, dom);
    assert(loop_info.get_loops().size() == 2);
    CoreIrLoopInfo *outer = loop_info.get_loop_for_header(outer_header);
    CoreIrLoopInfo *inner = loop_info.get_loop_for_header(inner_header);
    assert(outer != nullptr && inner != nullptr);
    assert(inner->get_parent_loop() == outer);
    assert(inner->get_depth() == outer->get_depth() + 1);
}

void test_irreducible_cfg_not_reported_as_loop() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("irreducible");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *a = function->create_basic_block<CoreIrBasicBlock>("a");
    auto *b = function->create_basic_block<CoreIrBasicBlock>("b");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    entry->create_instruction<CoreIrCondJumpInst>(void_type, cond, a, b);
    a->create_instruction<CoreIrCondJumpInst>(void_type, cond, b, exit);
    b->create_instruction<CoreIrCondJumpInst>(void_type, cond, a, exit);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CoreIrCfgAnalysis cfg_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_runner.Run(*function);
    CoreIrDominatorTreeAnalysis dom_runner;
    const CoreIrDominatorTreeAnalysisResult dom = dom_runner.Run(*function, cfg);
    CoreIrLoopInfoAnalysis loop_runner;
    const CoreIrLoopInfoAnalysisResult loop_info = loop_runner.Run(*function, cfg, dom);
    assert(loop_info.get_loops().empty());
}

} // namespace

int main() {
    test_single_loop();
    test_nested_loops();
    test_irreducible_cfg_not_reported_as_loop();
    return 0;
}
