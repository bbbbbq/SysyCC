#include <cassert>
#include <string>

#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"
#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/gvn/core_ir_gvn_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/licm/core_ir_licm_pass.hpp"
#include "backend/ir/local_cse/core_ir_local_cse_pass.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/sccp/core_ir_sccp_pass.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "backend/ir/lower/lower_ir_pass.hpp"
#include "compiler/complier.hpp"
#include "compiler/complier_option.hpp"
#include "compiler/pass/pass.hpp"

using namespace sysycc;

namespace {

std::string get_expected_target_triple() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "arm64-apple-macosx15.0.0";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "x86_64-apple-macosx10.15.0";
#elif defined(__aarch64__)
    return "aarch64-unknown-linux-gnu";
#elif defined(__x86_64__)
    return "x86_64-unknown-linux-gnu";
#else
    return "";
#endif
}

std::string get_expected_target_datalayout() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "e-m:o-i64:64-i128:128-n32:64-S128-Fn32";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "e-m:o-i64:64-f80:128-n8:16:32:64-S128";
#elif defined(__aarch64__)
    return "e-m:e-i64:64-i128:128-n32:64-S128";
#elif defined(__x86_64__)
    return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
#else
    return "";
#endif
}

} // namespace

int main(int argc, char **argv) {
    assert(argc == 2);

    ComplierOption option(argv[1]);
    option.set_stop_after_stage(StopAfterStage::Semantic);

    Complier complier(option);
    PassResult frontend_result = complier.Run();
    assert(frontend_result.ok);

    CompilerContext &context = complier.get_context();
    PassManager pass_manager;
    pass_manager.AddPass(std::make_unique<BuildCoreIrPass>());
    pass_manager.AddPass(std::make_unique<CoreIrCanonicalizePass>());
    pass_manager.AddPass(std::make_unique<CoreIrSimplifyCfgPass>());
    pass_manager.AddPass(std::make_unique<CoreIrLoopSimplifyPass>());
    pass_manager.AddPass(std::make_unique<CoreIrInstCombinePass>());
    pass_manager.AddPass(std::make_unique<CoreIrStackSlotForwardPass>());
    pass_manager.AddPass(std::make_unique<CoreIrDeadStoreEliminationPass>());
    pass_manager.AddPass(std::make_unique<CoreIrInstCombinePass>());
    pass_manager.AddPass(std::make_unique<CoreIrMem2RegPass>());

    std::vector<std::unique_ptr<Pass>> post_ssa_fixed_point_passes;
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrCopyPropagationPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrInstCombinePass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrSccpPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrSimplifyCfgPass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrLicmPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrLocalCsePass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrGvnPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrInstCombinePass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrConstFoldPass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrDcePass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrSimplifyCfgPass>());
    pass_manager.AddCoreIrFixedPointGroup(std::move(post_ssa_fixed_point_passes),
                                          4);
    pass_manager.AddPass(std::make_unique<LowerIrPass>());

    assert(pass_manager.Run(context).ok);

    const IRResult *ir_result = context.get_ir_result();
    assert(ir_result != nullptr);
    assert(ir_result->get_kind() == IrKind::LLVM);

    std::string expected;
    const std::string target_datalayout = get_expected_target_datalayout();
    const std::string target_triple = get_expected_target_triple();
    if (!target_datalayout.empty()) {
        expected += "target datalayout = \"" + target_datalayout + "\"\n";
    }
    if (!target_triple.empty()) {
        expected += "target triple = \"" + target_triple + "\"\n";
    }
    if (!target_datalayout.empty() || !target_triple.empty()) {
        expected += "\n";
    }
    expected +=
        "@g = global i32 1\n"
        "\n"
        "define i32 @main() {\n"
        "entry:\n"
        "  %value.addr = alloca i32\n"
        "  %t0 = load i32, ptr @g\n"
        "  store i32 %t0, ptr %value.addr\n"
        "  %t1.raw = icmp slt i32 %t0, 2\n"
        "  br i1 %t1.raw, label %if.then0, label %if.end0\n"
        "if.then0:\n"
        "  %t2 = load i32, ptr %value.addr\n"
        "  %t3 = add i32 %t2, 3\n"
        "  ret i32 %t3\n"
        "if.end0:\n"
        "  ret i32 0\n"
        "}\n";

    assert(ir_result->get_text() == expected);
    return 0;
}
