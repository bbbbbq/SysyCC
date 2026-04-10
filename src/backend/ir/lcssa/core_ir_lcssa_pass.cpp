#include "backend/ir/lcssa/core_ir_lcssa_pass.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
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

PassResult fail_missing_core_ir(CompilerContext &context,
                                const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
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

std::vector<CoreIrBasicBlock *>
collect_inside_predecessors(const CoreIrCfgAnalysisResult &cfg,
                            const CoreIrLoopInfo &loop,
                            CoreIrBasicBlock *exit_block) {
    std::vector<CoreIrBasicBlock *> predecessors;
    for (CoreIrBasicBlock *predecessor : cfg.get_predecessors(exit_block)) {
        if (loop_contains_block(loop, predecessor)) {
            predecessors.push_back(predecessor);
        }
    }
    return predecessors;
}

bool exit_block_has_outside_predecessor(const CoreIrCfgAnalysisResult &cfg,
                                        const CoreIrLoopInfo &loop,
                                        CoreIrBasicBlock *exit_block) {
    for (CoreIrBasicBlock *predecessor : cfg.get_predecessors(exit_block)) {
        if (!loop_contains_block(loop, predecessor)) {
            return true;
        }
    }
    return false;
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

CoreIrBasicBlock *find_unique_exit_for_use(
    const CoreIrUse &use,
    const std::unordered_map<CoreIrBasicBlock *,
                             std::vector<CoreIrBasicBlock *>>
        &inside_predecessors_by_exit,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree) {
    CoreIrBasicBlock *use_block = get_use_location_block(use);
    if (use_block == nullptr) {
        return nullptr;
    }

    auto direct_it = inside_predecessors_by_exit.find(use_block);
    if (direct_it != inside_predecessors_by_exit.end()) {
        return direct_it->first;
    }

    CoreIrBasicBlock *selected_exit = nullptr;
    for (const auto &entry : inside_predecessors_by_exit) {
        CoreIrBasicBlock *exit_block = entry.first;
        if (exit_block == nullptr ||
            !dominator_tree.dominates(exit_block, use_block)) {
            continue;
        }
        if (selected_exit != nullptr) {
            return nullptr;
        }
        selected_exit = exit_block;
    }
    return selected_exit;
}

CoreIrPhiInst *
create_lcssa_phi(CoreIrBasicBlock &exit_block, CoreIrInstruction &instruction,
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

bool rewrite_external_uses_through_exit_phi(
    CoreIrInstruction &instruction, const CoreIrLoopInfo &loop,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree,
    const std::unordered_map<CoreIrBasicBlock *,
                             std::vector<CoreIrBasicBlock *>>
        &inside_predecessors_by_exit) {
    bool changed = false;
    const std::vector<CoreIrUse> uses = instruction.get_uses();
    std::unordered_map<CoreIrBasicBlock *, CoreIrPhiInst *> exit_phis;
    for (const CoreIrUse &use : uses) {
        CoreIrInstruction *user = use.get_user();
        if (user == nullptr) {
            continue;
        }
        if (loop_contains_block(loop, get_use_location_block(use))) {
            continue;
        }

        CoreIrBasicBlock *exit_block = find_unique_exit_for_use(
            use, inside_predecessors_by_exit, dominator_tree);
        if (exit_block == nullptr) {
            continue;
        }

        const auto inside_it = inside_predecessors_by_exit.find(exit_block);
        if (inside_it == inside_predecessors_by_exit.end() ||
            !dominates_all_inside_predecessors(dominator_tree, instruction,
                                               inside_it->second)) {
            continue;
        }

        CoreIrPhiInst *phi = nullptr;
        auto phi_it = exit_phis.find(exit_block);
        if (phi_it == exit_phis.end()) {
            phi = create_lcssa_phi(*exit_block, instruction, inside_it->second);
            exit_phis.emplace(exit_block, phi);
        } else {
            phi = phi_it->second;
        }
        if (phi == nullptr || user == phi) {
            continue;
        }
        user->set_operand(use.get_operand_index(), phi);
        changed = true;
    }
    return changed;
}

bool run_lcssa_on_loop(
    CoreIrFunction &function, const CoreIrLoopInfo &loop,
    const CoreIrCfgAnalysisResult &cfg,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree) {
    std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        inside_predecessors_by_exit;
    for (CoreIrBasicBlock *exit_block : loop.get_exit_blocks()) {
        if (exit_block == nullptr ||
            exit_block_has_outside_predecessor(cfg, loop, exit_block)) {
            continue;
        }
        std::vector<CoreIrBasicBlock *> inside_predecessors =
            collect_inside_predecessors(cfg, loop, exit_block);
        if (inside_predecessors.empty()) {
            continue;
        }
        inside_predecessors_by_exit.emplace(exit_block,
                                            std::move(inside_predecessors));
    }
    if (inside_predecessors_by_exit.empty()) {
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
                !instruction_has_external_use(*instruction, loop)) {
                continue;
            }

            changed = rewrite_external_uses_through_exit_phi(
                          *instruction, loop, dominator_tree,
                          inside_predecessors_by_exit) ||
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
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager =
        build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir lcssa dependencies");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrCfgAnalysisResult &cfg =
            analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
        const CoreIrDominatorTreeAnalysisResult &dominator_tree =
            analysis_manager->get_or_compute<CoreIrDominatorTreeAnalysis>(
                *function);
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
                                     ? CoreIrPreservedAnalyses::preserve_none()
                                     : CoreIrPreservedAnalyses::preserve_all();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
