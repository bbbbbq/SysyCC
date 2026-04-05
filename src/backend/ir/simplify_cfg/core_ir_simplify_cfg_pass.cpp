#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "backend/ir/analysis/analysis_manager.hpp"
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

void detach_block(CoreIrBasicBlock &block) {
    for (const auto &instruction : block.get_instructions()) {
        if (instruction != nullptr) {
            instruction->detach_operands();
        }
    }
}

std::unordered_map<CoreIrBasicBlock *, CoreIrBasicBlock *>
collect_trampoline_blocks(CoreIrFunction &function) {
    std::unordered_map<CoreIrBasicBlock *, CoreIrBasicBlock *> trampoline_blocks;
    if (function.get_basic_blocks().empty()) {
        return trampoline_blocks;
    }

    CoreIrBasicBlock *entry_block = function.get_basic_blocks().front().get();
    for (const auto &block : function.get_basic_blocks()) {
        if (block.get() == nullptr || block.get() == entry_block) {
            continue;
        }
        if (block->get_instructions().size() != 1) {
            continue;
        }
        auto *jump =
            dynamic_cast<CoreIrJumpInst *>(block->get_instructions().front().get());
        if (jump == nullptr || jump->get_target_block() == nullptr ||
            jump->get_target_block() == block.get()) {
            continue;
        }
        trampoline_blocks.emplace(block.get(), jump->get_target_block());
    }
    return trampoline_blocks;
}

CoreIrBasicBlock *resolve_trampoline_target(
    CoreIrBasicBlock *block,
    const std::unordered_map<CoreIrBasicBlock *, CoreIrBasicBlock *> &trampoline_blocks) {
    std::unordered_set<CoreIrBasicBlock *> seen;
    CoreIrBasicBlock *current = block;
    while (current != nullptr) {
        auto it = trampoline_blocks.find(current);
        if (it == trampoline_blocks.end()) {
            return current;
        }
        if (!seen.insert(current).second) {
            return current;
        }
        current = it->second;
    }
    return current;
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
        cond_jump->detach_operands();
        auto replacement =
            std::make_unique<CoreIrJumpInst>(cond_jump->get_type(), target);
        replacement->set_source_span(cond_jump->get_source_span());
        replacement->set_parent(block.get());
        block->get_instructions().back() = std::move(replacement);
        changed = true;
    }
    return changed;
}

bool remove_trampoline_blocks(CoreIrFunction &function) {
    auto trampoline_blocks = collect_trampoline_blocks(function);
    if (trampoline_blocks.empty()) {
        return false;
    }

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        CoreIrInstruction *terminator = block->get_instructions().back().get();
        if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator);
            jump != nullptr) {
            jump->set_target_block(resolve_trampoline_target(
                jump->get_target_block(), trampoline_blocks));
        } else if (auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
                   cond_jump != nullptr) {
            cond_jump->set_true_block(resolve_trampoline_target(
                cond_jump->get_true_block(), trampoline_blocks));
            cond_jump->set_false_block(resolve_trampoline_target(
                cond_jump->get_false_block(), trampoline_blocks));
        }
    }

    auto &blocks = function.get_basic_blocks();
    blocks.erase(
        std::remove_if(blocks.begin(), blocks.end(),
                       [&trampoline_blocks](const std::unique_ptr<CoreIrBasicBlock> &block) {
                           if (block == nullptr) {
                               return true;
                           }
                           if (trampoline_blocks.find(block.get()) ==
                               trampoline_blocks.end()) {
                               return false;
                           }
                           detach_block(*block);
                           return true;
                       }),
        blocks.end());
    return true;
}

bool remove_unreachable_blocks(CoreIrAnalysisManager &analysis_manager,
                               CoreIrFunction &function) {
    const CoreIrCfgAnalysisResult &cfg_analysis =
        analysis_manager.get_or_compute<CoreIrCfgAnalysis>(function);
    const CoreIrBasicBlock *entry_block = cfg_analysis.get_entry_block();
    auto &blocks = function.get_basic_blocks();
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

bool merge_linear_blocks(CoreIrAnalysisManager &analysis_manager,
                         CoreIrFunction &function) {
    const CoreIrCfgAnalysisResult &cfg_analysis =
        analysis_manager.get_or_compute<CoreIrCfgAnalysis>(function);
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
        for (auto &instruction : successor_instructions) {
            instruction->set_parent(block);
            block->get_instructions().push_back(std::move(instruction));
        }
        successor_instructions.clear();
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

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir analysis manager");
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &function : build_result->get_module()->get_functions()) {
            bool function_changed = false;

            if (simplify_constant_cond_jumps(*function)) {
                build_result->invalidate_core_ir_analyses(*function);
                function_changed = true;
            }
            if (simplify_redundant_cond_jumps(*function)) {
                build_result->invalidate_core_ir_analyses(*function);
                function_changed = true;
            }
            if (remove_trampoline_blocks(*function)) {
                build_result->invalidate_core_ir_analyses(*function);
                function_changed = true;
            }
            if (remove_unreachable_blocks(*analysis_manager, *function)) {
                build_result->invalidate_core_ir_analyses(*function);
                function_changed = true;
            }
            if (merge_linear_blocks(*analysis_manager, *function)) {
                build_result->invalidate_core_ir_analyses(*function);
                function_changed = true;
            }

            changed = function_changed || changed;
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
