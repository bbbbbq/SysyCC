#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/induction_var_analysis.hpp"
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

void test_detects_positive_step_canonical_iv() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("iv_positive");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *ten = context->create_constant<CoreIrConstantInt>(i32_type, 10);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, ten);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next);

    CoreIrCfgAnalysis cfg_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_runner.Run(*function);
    CoreIrDominatorTreeAnalysis dom_runner;
    const CoreIrDominatorTreeAnalysisResult dom = dom_runner.Run(*function, cfg);
    CoreIrLoopInfoAnalysis loop_runner;
    const CoreIrLoopInfoAnalysisResult loop_info = loop_runner.Run(*function, cfg, dom);
    assert(loop_info.get_loops().size() == 1);

    CoreIrInductionVarAnalysis analysis;
    const CoreIrInductionVarAnalysisResult result =
        analysis.Run(*function, cfg, dom, loop_info);
    const CoreIrCanonicalInductionVarInfo *info =
        result.get_canonical_induction_var(*loop_info.get_loops().front());
    assert(info != nullptr);
    assert(info->phi == iv);
    assert(info->initial_value == zero);
    assert(info->latch == body);
    assert(info->step == 1);
    assert(info->exit_compare == cmp);
    assert(info->exit_bound == ten);
    assert(info->normalized_predicate == CoreIrComparePredicate::SignedLess);
}

void test_detects_negative_step_canonical_iv() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("iv_negative");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *ten = context->create_constant<CoreIrConstantInt>(i32_type, 10);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedGreater, i1_type, "cmp", iv, zero);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, i32_type, "next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, ten);
    iv->add_incoming(body, next);

    CoreIrCfgAnalysis cfg_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_runner.Run(*function);
    CoreIrDominatorTreeAnalysis dom_runner;
    const CoreIrDominatorTreeAnalysisResult dom = dom_runner.Run(*function, cfg);
    CoreIrLoopInfoAnalysis loop_runner;
    const CoreIrLoopInfoAnalysisResult loop_info = loop_runner.Run(*function, cfg, dom);
    assert(loop_info.get_loops().size() == 1);

    CoreIrInductionVarAnalysis analysis;
    const CoreIrInductionVarAnalysisResult result =
        analysis.Run(*function, cfg, dom, loop_info);
    const CoreIrCanonicalInductionVarInfo *info =
        result.get_canonical_induction_var(*loop_info.get_loops().front());
    assert(info != nullptr);
    assert(info->step == -1);
    assert(info->normalized_predicate == CoreIrComparePredicate::SignedGreater);
}

void test_rejects_non_constant_step() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("iv_non_constant");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *step_param = function->create_parameter<CoreIrParameter>(i32_type, "step");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *ten = context->create_constant<CoreIrConstantInt>(i32_type, 10);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, ten);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", iv, step_param);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next);

    CoreIrCfgAnalysis cfg_runner;
    const CoreIrCfgAnalysisResult cfg = cfg_runner.Run(*function);
    CoreIrDominatorTreeAnalysis dom_runner;
    const CoreIrDominatorTreeAnalysisResult dom = dom_runner.Run(*function, cfg);
    CoreIrLoopInfoAnalysis loop_runner;
    const CoreIrLoopInfoAnalysisResult loop_info = loop_runner.Run(*function, cfg, dom);
    assert(loop_info.get_loops().size() == 1);

    CoreIrInductionVarAnalysis analysis;
    const CoreIrInductionVarAnalysisResult result =
        analysis.Run(*function, cfg, dom, loop_info);
    assert(result.get_canonical_induction_var(
               *loop_info.get_loops().front()) == nullptr);
}

} // namespace

int main() {
    test_detects_positive_step_canonical_iv();
    test_detects_negative_step_canonical_iv();
    test_rejects_non_constant_step();
    return 0;
}

