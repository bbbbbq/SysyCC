#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/induction_var_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/analysis/scalar_evolution_lite_analysis.hpp"
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

void test_reports_addrec_and_constant_trip_count() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("scev_counted");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *ten = context->create_constant<CoreIrConstantInt>(i32_type, 10);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, ten);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *offset = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "offset", iv, two);
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
    const CoreIrLoopInfo &loop = *loop_info.get_loops().front();

    CoreIrInductionVarAnalysis iv_runner;
    const CoreIrInductionVarAnalysisResult iv_result =
        iv_runner.Run(*function, cfg, dom, loop_info);
    CoreIrScalarEvolutionLiteAnalysis scev_runner;
    const CoreIrScalarEvolutionLiteAnalysisResult scev =
        scev_runner.Run(*function, cfg, loop_info, iv_result);

    const CoreIrCanonicalInductionVarInfo *iv_info =
        scev.get_canonical_induction_var(loop);
    assert(iv_info != nullptr);
    assert(scev.get_constant_trip_count(loop).has_value());
    assert(*scev.get_constant_trip_count(loop) == 10);
    const CoreIrBackedgeTakenCountInfo *backedge =
        scev.get_backedge_taken_count(loop);
    assert(backedge != nullptr && backedge->has_symbolic_count);

    const CoreIrScevExpr iv_expr = scev.get_expr(iv, loop);
    assert(iv_expr.kind == CoreIrScevExprKind::AddRec);
    assert(iv_expr.step == 1);

    const CoreIrScevExpr offset_expr = scev.get_expr(offset, loop);
    assert(offset_expr.kind == CoreIrScevExprKind::AddRec);
    assert(offset_expr.step == 1);

    assert(scev.is_loop_invariant(ten, loop));
}

void test_reports_symbolic_non_constant_trip_count() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *module = context->create_module<CoreIrModule>("scev_symbolic");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *bound_param =
        function->create_parameter<CoreIrParameter>(i32_type, "bound");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, bound_param);
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
    const CoreIrLoopInfo &loop = *loop_info.get_loops().front();

    CoreIrInductionVarAnalysis iv_runner;
    const CoreIrInductionVarAnalysisResult iv_result =
        iv_runner.Run(*function, cfg, dom, loop_info);
    CoreIrScalarEvolutionLiteAnalysis scev_runner;
    const CoreIrScalarEvolutionLiteAnalysisResult scev =
        scev_runner.Run(*function, cfg, loop_info, iv_result);

    const CoreIrBackedgeTakenCountInfo *backedge =
        scev.get_backedge_taken_count(loop);
    assert(backedge != nullptr && backedge->has_symbolic_count);
    assert(!backedge->constant_trip_count.has_value());
    assert(scev.is_loop_invariant(bound_param, loop));
}

} // namespace

int main() {
    test_reports_addrec_and_constant_trip_count();
    test_reports_symbolic_non_constant_trip_count();
    return 0;
}

