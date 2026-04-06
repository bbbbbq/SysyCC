#include "backend/ir/lcssa/core_ir_lcssa_pass.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
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

bool loop_contains_block(const CoreIrLoopInfo &loop,
                         const CoreIrBasicBlock *block) {
    return block != nullptr &&
           loop.get_blocks().find(const_cast<CoreIrBasicBlock *>(block)) !=
               loop.get_blocks().end();
}

CoreIrBasicBlock *get_use_location_block(const CoreIrUse &use) {
    CoreIrInstruction *user = use.get_user();
    if (user == nullptr) {
        return nullptr;
    }
    if (auto *phi = dynamic_cast<CoreIrPhiInst *>(user); phi != nullptr) {
        return phi->get_incoming_block(use.get_operand_index());
    }
    return user->get_parent();
}

bool instruction_has_external_use(const CoreIrInstruction &instruction,
                                  const CoreIrLoopInfo &loop) {
    for (const CoreIrUse &use : instruction.get_uses()) {
        if (!loop_contains_block(loop, get_use_location_block(use))) {
            return true;
        }
    }
    return false;
}

std::vector<CoreIrBasicBlock *> collect_inside_predecessors(
    const CoreIrCfgAnalysisResult &cfg, const CoreIrLoopInfo &loop,
    CoreIrBasicBlock *exit_block) {
    std::vector<CoreIrBasicBlock *> predecessors;
    for (CoreIrBasicBlock *predecessor : cfg.get_predecessors(exit_block)) {
        if (loop_contains_block(loop, predecessor)) {
            predecessors.push_back(predecessor);
        }
    }
    return predecessors;
}

bool dominates_all_inside_predecessors(
    const CoreIrDominatorTreeAnalysisResult &dominator_tree,
    const CoreIrInstruction &instruction,
    const std::vector<CoreIrBasicBlock *> &inside_predecessors) {
    CoreIrBasicBlock *def_block = instruction.get_parent();
    if (def_block == nullptr) {
        return false;
    }
    for (CoreIrBasicBlock *predecessor : inside_predecessors) {
        if (predecessor == nullptr ||
            !dominator_tree.dominates(def_block, predecessor)) {
            return false;
        }
    }
    return true;
}

CoreIrPhiInst *create_lcssa_phi(CoreIrBasicBlock &exit_block,
                                CoreIrInstruction &instruction,
                                const std::vector<CoreIrBasicBlock *> &inside_predecessors) {
    auto phi = std::make_unique<CoreIrPhiInst>(
        instruction.get_type(), instruction.get_name() + ".lcssa");
    phi->set_source_span(instruction.get_source_span());
    CoreIrPhiInst *phi_ptr = phi.get();
    for (CoreIrBasicBlock *predecessor : inside_predecessors) {
        phi_ptr->add_incoming(predecessor, &instruction);
    }
    exit_block.insert_instruction_before_first_non_phi(std::move(phi));
    return phi_ptr;
}

bool rewrite_external_uses_through_exit_phi(CoreIrInstruction &instruction,
                                            const CoreIrLoopInfo &loop,
                                            CoreIrPhiInst &phi) {
    bool changed = false;
    const std::vector<CoreIrUse> uses = instruction.get_uses();
    for (const CoreIrUse &use : uses) {
        CoreIrInstruction *user = use.get_user();
        if (user == nullptr || user == &phi) {
            continue;
        }
        if (loop_contains_block(loop, get_use_location_block(use))) {
            continue;
        }
        user->set_operand(use.get_operand_index(), &phi);
        changed = true;
    }
    return changed;
}

bool run_lcssa_on_loop(CoreIrFunction &function, const CoreIrLoopInfo &loop,
                       const CoreIrCfgAnalysisResult &cfg,
                       const CoreIrDominatorTreeAnalysisResult &dominator_tree) {
    if (loop.get_exit_blocks().size() != 1) {
        return false;
    }

    CoreIrBasicBlock *exit_block = *loop.get_exit_blocks().begin();
    if (exit_block == nullptr) {
        return false;
    }

    const std::vector<CoreIrBasicBlock *> inside_predecessors =
        collect_inside_predecessors(cfg, loop, exit_block);
    if (inside_predecessors.empty()) {
        return false;
    }

    bool changed = false;
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr || instruction->get_type() == nullptr ||
                instruction->get_type()->get_kind() == CoreIrTypeKind::Void ||
                !instruction_has_external_use(*instruction, loop) ||
                !dominates_all_inside_predecessors(dominator_tree, *instruction,
                                                  inside_predecessors)) {
                continue;
            }

            CoreIrPhiInst *phi =
                create_lcssa_phi(*exit_block, *instruction, inside_predecessors);
            changed = rewrite_external_uses_through_exit_phi(*instruction, loop, *phi) ||
                      changed;
        }
    }

    return changed;
}

} // namespace

PassKind CoreIrLcssaPass::Kind() const { return PassKind::CoreIrLcssa; }

const char *CoreIrLcssaPass::Name() const { return "CoreIrLcssaPass"; }

PassResult CoreIrLcssaPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir lcssa dependencies");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrCfgAnalysisResult &cfg =
            analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
        const CoreIrDominatorTreeAnalysisResult &dominator_tree =
            analysis_manager->get_or_compute<CoreIrDominatorTreeAnalysis>(*function);
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);

        std::vector<const CoreIrLoopInfo *> ordered_loops;
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr != nullptr) {
                ordered_loops.push_back(loop_ptr.get());
            }
        }
        std::sort(ordered_loops.begin(), ordered_loops.end(),
                  [](const CoreIrLoopInfo *lhs, const CoreIrLoopInfo *rhs) {
                      return lhs->get_depth() > rhs->get_depth();
                  });

        bool function_changed = false;
        for (const CoreIrLoopInfo *loop : ordered_loops) {
            function_changed =
                run_lcssa_on_loop(*function, *loop, cfg, dominator_tree) ||
                function_changed;
        }
        if (function_changed) {
            effects.changed_functions.insert(function.get());
        }
    }

    effects.preserved_analyses = effects.has_changes()
                                     ? CoreIrPreservedAnalyses::preserve_all()
                                     : CoreIrPreservedAnalyses::preserve_all();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc

