#include "backend/ir/pipeline/core_ir_pass_pipeline.hpp"

#include <memory>
#include <vector>

#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/gvn/core_ir_gvn_pass.hpp"
#include "backend/ir/indvar_simplify/core_ir_indvar_simplify_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/lcssa/core_ir_lcssa_pass.hpp"
#include "backend/ir/licm/core_ir_licm_pass.hpp"
#include "backend/ir/local_cse/core_ir_local_cse_pass.hpp"
#include "backend/ir/loop_idiom/core_ir_loop_idiom_pass.hpp"
#include "backend/ir/loop_memory_promotion/core_ir_loop_memory_promotion_pass.hpp"
#include "backend/ir/loop_rotate/core_ir_loop_rotate_pass.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/loop_unroll/core_ir_loop_unroll_pass.hpp"
#include "backend/ir/lower/lower_ir_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/sccp/core_ir_sccp_pass.hpp"
#include "backend/ir/simple_loop_unswitch/core_ir_simple_loop_unswitch_pass.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "backend/ir/sroa/core_ir_sroa_pass.hpp"
#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"
#include "compiler/pass/pass.hpp"

namespace sysycc {

namespace {

void append_pre_ssa_pipeline(PassManager &pass_manager) {
    pass_manager.AddPass(std::make_unique<BuildCoreIrPass>());
    pass_manager.AddPass(std::make_unique<CoreIrCanonicalizePass>());
    pass_manager.AddPass(std::make_unique<CoreIrSimplifyCfgPass>());
    pass_manager.AddPass(std::make_unique<CoreIrLoopSimplifyPass>());
    pass_manager.AddPass(std::make_unique<CoreIrSroaPass>());
    pass_manager.AddPass(std::make_unique<CoreIrInstCombinePass>());
    pass_manager.AddPass(std::make_unique<CoreIrStackSlotForwardPass>());
    pass_manager.AddPass(std::make_unique<CoreIrDeadStoreEliminationPass>());
    pass_manager.AddPass(std::make_unique<CoreIrInstCombinePass>());
    pass_manager.AddPass(std::make_unique<CoreIrMem2RegPass>());
}

void append_post_ssa_fixed_point_pipeline(PassManager &pass_manager) {
    std::vector<std::unique_ptr<Pass>> post_ssa_fixed_point_passes;
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrCopyPropagationPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrInstCombinePass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrSccpPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrSimplifyCfgPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrLoopSimplifyPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrLoopRotatePass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrLcssaPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrIndVarSimplifyPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrLoopMemoryPromotionPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrInstCombinePass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrLoopIdiomPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrSimplifyCfgPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrLoopSimplifyPass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrLcssaPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrSimpleLoopUnswitchPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrSimplifyCfgPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrLoopSimplifyPass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrLcssaPass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrLicmPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrLoopUnrollPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrCopyPropagationPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrInstCombinePass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrLocalCsePass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrGvnPass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrConstFoldPass>());
    post_ssa_fixed_point_passes.push_back(std::make_unique<CoreIrDcePass>());
    post_ssa_fixed_point_passes.push_back(
        std::make_unique<CoreIrSimplifyCfgPass>());
    pass_manager.AddCoreIrFixedPointGroup(std::move(post_ssa_fixed_point_passes),
                                          4);
}

void append_lowering_pipeline(PassManager &pass_manager) {
    pass_manager.AddPass(std::make_unique<LowerIrPass>());
}

} // namespace

void append_default_core_ir_pipeline(PassManager &pass_manager) {
    append_pre_ssa_pipeline(pass_manager);
    append_post_ssa_fixed_point_pipeline(pass_manager);
    append_lowering_pipeline(pass_manager);
}

} // namespace sysycc
