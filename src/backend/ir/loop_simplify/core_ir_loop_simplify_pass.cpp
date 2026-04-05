#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

bool block_has_phi(const CoreIrBasicBlock &block) {
    for (const auto &instruction : block.get_instructions()) {
        if (instruction == nullptr) {
            continue;
        }
        if (instruction->get_opcode() == CoreIrOpcode::Phi) {
            return true;
        }
        break;
    }
    return false;
}

bool redirect_successor_edge(CoreIrBasicBlock &block, CoreIrBasicBlock *from,
                             CoreIrBasicBlock *to) {
    if (block.get_instructions().empty()) {
        return false;
    }
    CoreIrInstruction *terminator = block.get_instructions().back().get();
    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator); jump != nullptr) {
        if (jump->get_target_block() == from) {
            jump->set_target_block(to);
            return true;
        }
        return false;
    }
    auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
    if (cond_jump == nullptr) {
        return false;
    }
    bool changed = false;
    if (cond_jump->get_true_block() == from) {
        cond_jump->set_true_block(to);
        changed = true;
    }
    if (cond_jump->get_false_block() == from) {
        cond_jump->set_false_block(to);
        changed = true;
    }
    return changed;
}

CoreIrBasicBlock *insert_new_block_before(CoreIrFunction &function,
                                          CoreIrBasicBlock *anchor,
                                          std::unique_ptr<CoreIrBasicBlock> block) {
    if (anchor == nullptr || block == nullptr) {
        return nullptr;
    }
    block->set_parent(&function);
    CoreIrBasicBlock *block_ptr = block.get();
    auto &blocks = function.get_basic_blocks();
    auto it = std::find_if(blocks.begin(), blocks.end(),
                           [anchor](const std::unique_ptr<CoreIrBasicBlock> &candidate) {
                               return candidate.get() == anchor;
                           });
    blocks.insert(it, std::move(block));
    return block_ptr;
}

bool ensure_preheader(CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg,
                      CoreIrLoopInfo &loop, const CoreIrType *void_type,
                      std::size_t &counter) {
    if (loop.get_preheader() != nullptr) {
        return false;
    }
    CoreIrBasicBlock *header = loop.get_header();
    if (header == nullptr || block_has_phi(*header)) {
        return false;
    }
    std::vector<CoreIrBasicBlock *> outside_preds;
    for (CoreIrBasicBlock *pred : cfg.get_predecessors(header)) {
        if (pred != nullptr && loop.get_blocks().find(pred) == loop.get_blocks().end()) {
            outside_preds.push_back(pred);
        }
    }
    if (outside_preds.empty()) {
        auto preheader = std::make_unique<CoreIrBasicBlock>(
            header->get_name() + ".preheader." + std::to_string(counter++));
        CoreIrBasicBlock *preheader_ptr =
            insert_new_block_before(function, header, std::move(preheader));
        if (preheader_ptr == nullptr) {
            return false;
        }
        preheader_ptr->create_instruction<CoreIrJumpInst>(void_type, header);
        loop.set_preheader(preheader_ptr);
        return true;
    }
    if (outside_preds.size() == 1 &&
        cfg.get_successors(outside_preds.front()).size() == 1) {
        loop.set_preheader(outside_preds.front());
        return false;
    }

    auto preheader = std::make_unique<CoreIrBasicBlock>(
        header->get_name() + ".preheader." + std::to_string(counter++));
    CoreIrBasicBlock *preheader_ptr =
        insert_new_block_before(function, header, std::move(preheader));
    if (preheader_ptr == nullptr) {
        return false;
    }
    preheader_ptr->create_instruction<CoreIrJumpInst>(void_type, header);
    for (CoreIrBasicBlock *pred : outside_preds) {
        redirect_successor_edge(*pred, header, preheader_ptr);
    }
    loop.set_preheader(preheader_ptr);
    return true;
}

bool ensure_single_latch(CoreIrFunction &function, CoreIrLoopInfo &loop,
                         const CoreIrType *void_type, std::size_t &counter) {
    if (loop.get_latches().size() <= 1 || block_has_phi(*loop.get_header())) {
        return false;
    }
    auto latch = std::make_unique<CoreIrBasicBlock>(
        loop.get_header()->get_name() + ".latch." + std::to_string(counter++));
    latch->set_parent(&function);
    CoreIrBasicBlock *latch_ptr = latch.get();
    latch_ptr->append_instruction(
        std::make_unique<CoreIrJumpInst>(void_type, loop.get_header()));
    function.get_basic_blocks().push_back(std::move(latch));
    for (CoreIrBasicBlock *old_latch : loop.get_latches()) {
        redirect_successor_edge(*old_latch, loop.get_header(), latch_ptr);
    }
    loop.mutable_latches().clear();
    loop.mutable_latches().insert(latch_ptr);
    loop.mutable_blocks().insert(latch_ptr);
    return true;
}

bool ensure_dedicated_exits(CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg,
                            CoreIrLoopInfo &loop, const CoreIrType *void_type,
                            std::size_t &counter) {
    bool changed = false;
    for (CoreIrBasicBlock *exit_block : loop.get_exit_blocks()) {
        if (exit_block == nullptr || block_has_phi(*exit_block)) {
            continue;
        }
        std::vector<CoreIrBasicBlock *> inside_preds;
        std::vector<CoreIrBasicBlock *> outside_preds;
        for (CoreIrBasicBlock *pred : cfg.get_predecessors(exit_block)) {
            if (pred == nullptr) {
                continue;
            }
            if (loop.get_blocks().find(pred) != loop.get_blocks().end()) {
                inside_preds.push_back(pred);
            } else {
                outside_preds.push_back(pred);
            }
        }
        if (inside_preds.empty() || outside_preds.empty()) {
            continue;
        }

        auto dedicated_exit = std::make_unique<CoreIrBasicBlock>(
            exit_block->get_name() + ".dedicated." + std::to_string(counter++));
        dedicated_exit->set_parent(&function);
        CoreIrBasicBlock *dedicated_exit_ptr = dedicated_exit.get();
        dedicated_exit_ptr->append_instruction(
            std::make_unique<CoreIrJumpInst>(void_type, exit_block));
        function.get_basic_blocks().push_back(std::move(dedicated_exit));
        for (CoreIrBasicBlock *pred : inside_preds) {
            redirect_successor_edge(*pred, exit_block, dedicated_exit_ptr);
        }
        changed = true;
    }
    return changed;
}

} // namespace

PassKind CoreIrLoopSimplifyPass::Kind() const { return PassKind::CoreIrLoopSimplify; }

const char *CoreIrLoopSimplifyPass::Name() const { return "CoreIrLoopSimplifyPass"; }

PassResult CoreIrLoopSimplifyPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrContext *core_ir_context = build_result->get_context();
    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (core_ir_context == nullptr || analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir loop simplify dependencies");
    }
    const CoreIrType *void_type = core_ir_context->create_type<CoreIrVoidType>();

    CoreIrPassEffects effects;
    std::size_t counter = 0;
    for (const auto &function : module->get_functions()) {
        const CoreIrCfgAnalysisResult &cfg =
            analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);

        bool function_changed = false;
        for (const auto &loop_ptr : loop_info.get_loops()) {
            function_changed = ensure_preheader(*function, cfg, *loop_ptr, void_type,
                                                counter) ||
                               function_changed;
            function_changed = ensure_single_latch(*function, *loop_ptr, void_type,
                                                   counter) ||
                               function_changed;
            function_changed = ensure_dedicated_exits(*function, cfg, *loop_ptr,
                                                      void_type, counter) ||
                               function_changed;
        }
        if (function_changed) {
            effects.changed_functions.insert(function.get());
            effects.cfg_changed_functions.insert(function.get());
        }
    }
    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
