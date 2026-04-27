#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

const CoreIrConstantInt *as_integer_constant(const CoreIrValue *value) {
    return dynamic_cast<const CoreIrConstantInt *>(value);
}

std::vector<CoreIrBasicBlock *> collect_terminator_successors(
    CoreIrBasicBlock &block) {
    std::vector<CoreIrBasicBlock *> successors;
    if (block.get_instructions().empty()) {
        return successors;
    }

    CoreIrInstruction *terminator = block.get_instructions().back().get();
    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator); jump != nullptr) {
        if (jump->get_target_block() != nullptr) {
            successors.push_back(jump->get_target_block());
        }
        return successors;
    }

    if (auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
        cond_jump != nullptr) {
        if (cond_jump->get_true_block() != nullptr) {
            successors.push_back(cond_jump->get_true_block());
        }
        if (cond_jump->get_false_block() != nullptr &&
            cond_jump->get_false_block() != cond_jump->get_true_block()) {
            successors.push_back(cond_jump->get_false_block());
        }
        return successors;
    }

    if (auto *indirect_jump = dynamic_cast<CoreIrIndirectJumpInst *>(terminator);
        indirect_jump != nullptr) {
        for (CoreIrBasicBlock *target : indirect_jump->get_target_blocks()) {
            if (target != nullptr &&
                std::find(successors.begin(), successors.end(), target) ==
                    successors.end()) {
                successors.push_back(target);
            }
        }
    }
    return successors;
}

void detach_block(CoreIrBasicBlock &block) {
    for (const auto &instruction : block.get_instructions()) {
        if (instruction != nullptr) {
            instruction->detach_operands();
        }
    }
}

void remove_phi_incoming_from_predecessor(CoreIrBasicBlock *successor,
                                          CoreIrBasicBlock *predecessor) {
    if (successor == nullptr || predecessor == nullptr) {
        return;
    }
    for (const auto &instruction : successor->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction.get());
        if (phi == nullptr) {
            break;
        }
        phi->remove_incoming_block(predecessor);
    }
}

void remove_phi_incoming_from_predecessors(
    CoreIrBasicBlock *successor,
    const std::unordered_set<CoreIrBasicBlock *> &predecessors) {
    if (successor == nullptr || predecessors.empty()) {
        return;
    }
    for (const auto &instruction : successor->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction.get());
        if (phi == nullptr) {
            break;
        }
        phi->remove_incoming_blocks(predecessors);
    }
}

bool successor_has_phi_predecessor(CoreIrBasicBlock *successor,
                                   CoreIrBasicBlock *predecessor) {
    if (successor == nullptr || predecessor == nullptr) {
        return false;
    }
    for (const auto &instruction : successor->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction.get());
        if (phi == nullptr) {
            break;
        }
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) == predecessor) {
                return true;
            }
        }
    }
    return false;
}

bool rewrite_phi_predecessor(CoreIrBasicBlock *successor,
                             CoreIrBasicBlock *old_predecessor,
                             CoreIrBasicBlock *new_predecessor) {
    if (successor == nullptr || old_predecessor == nullptr ||
        new_predecessor == nullptr) {
        return false;
    }

    if (old_predecessor == new_predecessor) {
        return true;
    }

    for (const auto &instruction : successor->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction.get());
        if (phi == nullptr) {
            break;
        }
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) != old_predecessor) {
                continue;
            }
            for (std::size_t other = 0; other < phi->get_incoming_count(); ++other) {
                if (other == index) {
                    continue;
                }
                if (phi->get_incoming_block(other) == new_predecessor) {
                    return false;
                }
            }
        }
    }

    bool changed = false;
    for (const auto &instruction : successor->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction.get());
        if (phi == nullptr) {
            break;
        }
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) == old_predecessor) {
                phi->set_incoming_block(index, new_predecessor);
                changed = true;
            }
        }
    }
    return changed;
}

bool retarget_block_successor(CoreIrBasicBlock &block, CoreIrBasicBlock *old_target,
                              CoreIrBasicBlock *new_target) {
    if (old_target == nullptr || new_target == nullptr ||
        block.get_instructions().empty()) {
        return false;
    }

    bool changed = false;
    CoreIrInstruction *terminator = block.get_instructions().back().get();
    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator); jump != nullptr) {
        if (jump->get_target_block() == old_target) {
            jump->set_target_block(new_target);
            changed = true;
        }
        return changed;
    }

    auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
    if (cond_jump == nullptr) {
        return false;
    }
    if (cond_jump->get_true_block() == old_target) {
        cond_jump->set_true_block(new_target);
        changed = true;
    }
    if (cond_jump->get_false_block() == old_target) {
        cond_jump->set_false_block(new_target);
        changed = true;
    }
    return changed;
}

bool simplify_redundant_cond_jumps(CoreIrFunction &function) {
    bool changed = false;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(
            block->get_instructions().back().get());
        if (cond_jump == nullptr ||
            cond_jump->get_true_block() != cond_jump->get_false_block()) {
            continue;
        }
        CoreIrBasicBlock *target = cond_jump->get_true_block();
        cond_jump->detach_operands();
        auto replacement = std::make_unique<CoreIrJumpInst>(
            cond_jump->get_type(), target);
        replacement->set_source_span(cond_jump->get_source_span());
        replacement->set_parent(block.get());
        block->get_instructions().back() = std::move(replacement);
        changed = true;
    }
    return changed;
}

bool simplify_constant_cond_jumps(CoreIrFunction &function) {
    bool changed = false;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(
            block->get_instructions().back().get());
        if (cond_jump == nullptr) {
            continue;
        }
        const auto *constant = as_integer_constant(cond_jump->get_condition());
        if (constant == nullptr) {
            continue;
        }
        CoreIrBasicBlock *target =
            constant->get_value() == 0 ? cond_jump->get_false_block()
                                       : cond_jump->get_true_block();
        CoreIrBasicBlock *removed_successor =
            constant->get_value() == 0 ? cond_jump->get_true_block()
                                       : cond_jump->get_false_block();
        remove_phi_incoming_from_predecessor(removed_successor, block.get());
        cond_jump->detach_operands();
        auto replacement = std::make_unique<CoreIrJumpInst>(
            cond_jump->get_type(), target);
        replacement->set_source_span(cond_jump->get_source_span());
        replacement->set_parent(block.get());
        block->get_instructions().back() = std::move(replacement);
        changed = true;
    }
    return changed;
}

CoreIrCfgAnalysisResult compute_cfg(CoreIrFunction &function) {
    CoreIrCfgAnalysis analysis;
    return analysis.Run(function);
}

bool remove_one_trampoline_block(CoreIrFunction &function,
                                 const CoreIrCfgAnalysisResult &cfg_analysis) {
    if (function.get_basic_blocks().empty()) {
        return false;
    }

    CoreIrBasicBlock *entry_block = function.get_basic_blocks().front().get();
    auto &blocks = function.get_basic_blocks();
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        CoreIrBasicBlock *block = it->get();
        if (block == nullptr || block == entry_block ||
            block->get_instructions().size() != 1) {
            continue;
        }

        auto *jump =
            dynamic_cast<CoreIrJumpInst *>(block->get_instructions().front().get());
        if (jump == nullptr || jump->get_target_block() == nullptr ||
            jump->get_target_block() == block) {
            continue;
        }

        const auto &predecessors = cfg_analysis.get_predecessors(block);
        if (predecessors.size() != 1 || predecessors.front() == nullptr) {
            continue;
        }

        CoreIrBasicBlock *predecessor = predecessors.front();
        CoreIrBasicBlock *successor = jump->get_target_block();
        if (predecessor == block || predecessor == successor) {
            continue;
        }
        if (successor_has_phi_predecessor(successor, predecessor)) {
            continue;
        }
        if (!retarget_block_successor(*predecessor, block, successor)) {
            continue;
        }
        rewrite_phi_predecessor(successor, block, predecessor);
        detach_block(*block);
        blocks.erase(it);
        return true;
    }

    return false;
}

bool remove_unreachable_blocks(CoreIrFunction &function,
                               const CoreIrCfgAnalysisResult &cfg_analysis) {
    const CoreIrBasicBlock *entry_block = cfg_analysis.get_entry_block();
    auto &blocks = function.get_basic_blocks();
    std::vector<CoreIrBasicBlock *> unreachable_blocks;
    unreachable_blocks.reserve(blocks.size());
    for (const auto &block : blocks) {
        if (block == nullptr || block.get() == entry_block ||
            cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        unreachable_blocks.push_back(block.get());
    }

    if (!unreachable_blocks.empty()) {
        for (const auto &block : blocks) {
            if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
                continue;
            }
            std::unordered_set<CoreIrBasicBlock *> unreachable_predecessors;
            for (CoreIrBasicBlock *predecessor :
                 cfg_analysis.get_predecessors(block.get())) {
                if (predecessor != nullptr &&
                    !cfg_analysis.is_reachable(predecessor)) {
                    unreachable_predecessors.insert(predecessor);
                }
            }
            remove_phi_incoming_from_predecessors(block.get(),
                                                  unreachable_predecessors);
        }
    }

    const auto new_end = std::remove_if(
        blocks.begin(), blocks.end(),
        [&cfg_analysis, entry_block](const std::unique_ptr<CoreIrBasicBlock> &block) {
            if (block == nullptr) {
                return true;
            }
            if (block.get() == entry_block || cfg_analysis.is_reachable(block.get())) {
                return false;
            }
            detach_block(*block);
            return true;
        });
    if (new_end == blocks.end()) {
        return false;
    }
    blocks.erase(new_end, blocks.end());
    return true;
}

bool merge_linear_blocks(CoreIrFunction &function,
                         const CoreIrCfgAnalysisResult &cfg_analysis) {
    if (function.get_basic_blocks().empty()) {
        return false;
    }

    CoreIrBasicBlock *entry_block = function.get_basic_blocks().front().get();
    auto &blocks = function.get_basic_blocks();
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        CoreIrBasicBlock *block = blocks[index].get();
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        auto *jump =
            dynamic_cast<CoreIrJumpInst *>(block->get_instructions().back().get());
        if (jump == nullptr) {
            continue;
        }

        CoreIrBasicBlock *successor = jump->get_target_block();
        if (successor == nullptr || successor == block || successor == entry_block ||
            cfg_analysis.get_predecessor_count(successor) != 1) {
            continue;
        }
        const std::vector<CoreIrBasicBlock *> successor_successors =
            collect_terminator_successors(*successor);
        bool can_rewrite_successor_phis = true;
        for (CoreIrBasicBlock *successor_successor : successor_successors) {
            if (successor_successor == nullptr || successor_successor == successor ||
                successor_successor == block) {
                can_rewrite_successor_phis = false;
                break;
            }
            if (successor_has_phi_predecessor(successor_successor, block)) {
                can_rewrite_successor_phis = false;
                break;
            }
        }
        if (!can_rewrite_successor_phis) {
            continue;
        }

        auto successor_it = std::find_if(
            blocks.begin(), blocks.end(),
            [successor](const std::unique_ptr<CoreIrBasicBlock> &candidate) {
                return candidate.get() == successor;
            });
        if (successor_it == blocks.end()) {
            continue;
        }

        jump->detach_operands();
        block->get_instructions().pop_back();
        auto &successor_instructions = successor->get_instructions();
        while (!successor_instructions.empty()) {
            auto *phi =
                dynamic_cast<CoreIrPhiInst *>(successor_instructions.front().get());
            if (phi == nullptr) {
                break;
            }
            CoreIrValue *replacement =
                phi->get_incoming_count() == 1 ? phi->get_incoming_value(0) : nullptr;
            if (replacement == nullptr) {
                return false;
            }
            phi->replace_all_uses_with(replacement);
            phi->detach_operands();
            successor_instructions.erase(successor_instructions.begin());
        }
        for (CoreIrBasicBlock *successor_successor : successor_successors) {
            rewrite_phi_predecessor(successor_successor, successor, block);
        }
        for (auto &instruction : successor_instructions) {
            instruction->set_parent(block);
            block->get_instructions().push_back(std::move(instruction));
        }
        successor_instructions.clear();
        detach_block(*successor);
        blocks.erase(successor_it);
        return true;
    }

    return false;
}

} // namespace

PassKind CoreIrSimplifyCfgPass::Kind() const {
    return PassKind::CoreIrSimplifyCfg;
}

const char *CoreIrSimplifyCfgPass::Name() const {
    return "CoreIrSimplifyCfgPass";
}

PassResult CoreIrSimplifyCfgPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    if (build_result == nullptr || build_result->get_module() == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrPassEffects effects;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &function : build_result->get_module()->get_functions()) {
            bool function_changed = false;
            CoreIrCfgAnalysisResult cfg_analysis = compute_cfg(*function);

            if (simplify_constant_cond_jumps(*function)) {
                function_changed = true;
                cfg_analysis = compute_cfg(*function);
            }
            if (simplify_redundant_cond_jumps(*function)) {
                function_changed = true;
                cfg_analysis = compute_cfg(*function);
            }
            if (remove_one_trampoline_block(*function, cfg_analysis)) {
                function_changed = true;
                cfg_analysis = compute_cfg(*function);
            }
            if (remove_unreachable_blocks(*function, cfg_analysis)) {
                function_changed = true;
                cfg_analysis = compute_cfg(*function);
            }
            if (merge_linear_blocks(*function, cfg_analysis)) {
                function_changed = true;
                cfg_analysis = compute_cfg(*function);
            }
            if (function_changed) {
                effects.changed_functions.insert(function.get());
                effects.cfg_changed_functions.insert(function.get());
            }

            changed = function_changed || changed;
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
