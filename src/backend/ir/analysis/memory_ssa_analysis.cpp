#include "backend/ir/analysis/memory_ssa_analysis.hpp"

#include <unordered_set>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominance_frontier_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/function_effect_summary_analysis.hpp"
#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

namespace {

bool instruction_is_memory_use(const CoreIrInstruction &instruction) noexcept {
    if (dynamic_cast<const CoreIrLoadInst *>(&instruction) != nullptr) {
        return true;
    }
    if (auto *call = dynamic_cast<const CoreIrCallInst *>(&instruction);
        call != nullptr) {
        const CoreIrMemoryBehavior behavior =
            get_core_ir_instruction_effect(*call).memory_behavior;
        return memory_behavior_reads(behavior) && !memory_behavior_writes(behavior);
    }
    return false;
}

bool instruction_is_memory_def(const CoreIrInstruction &instruction) noexcept {
    if (dynamic_cast<const CoreIrStoreInst *>(&instruction) != nullptr) {
        return true;
    }
    if (auto *call = dynamic_cast<const CoreIrCallInst *>(&instruction);
        call != nullptr) {
        return memory_behavior_writes(
            get_core_ir_instruction_effect(*call).memory_behavior);
    }
    return false;
}

void build_dominator_children(
    const CoreIrFunction &function,
    const CoreIrCfgAnalysisResult &cfg_analysis,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree,
    std::unordered_map<const CoreIrBasicBlock *,
                       std::vector<const CoreIrBasicBlock *>> &children) {
    for (const auto &block : function.get_basic_blocks()) {
        if (block != nullptr && cfg_analysis.is_reachable(block.get())) {
            children.emplace(block.get(), std::vector<const CoreIrBasicBlock *>{});
        }
    }

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        CoreIrBasicBlock *idom = dominator_tree.get_immediate_dominator(block.get());
        if (idom != nullptr) {
            children[idom].push_back(block.get());
        }
    }
}

struct RenameState {
    std::size_t next_id = 1;
    std::vector<CoreIrMemoryAccess *> stack;
    std::unordered_map<const CoreIrBasicBlock *, CoreIrMemoryPhiAccess *> phis;
    std::unordered_map<const CoreIrInstruction *, CoreIrMemoryAccess *>
        instruction_accesses;
    std::vector<std::unique_ptr<CoreIrMemoryAccess>> accesses;
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrMemoryPhiAccess *>>
        block_phis;
    std::unordered_map<const CoreIrBasicBlock *, std::vector<const CoreIrBasicBlock *>>
        dominator_children;
};

void add_phi_incoming_to_successors(const CoreIrBasicBlock &block,
                                    const CoreIrCfgAnalysisResult &cfg_analysis,
                                    RenameState &state) {
    if (state.stack.empty()) {
        return;
    }

    for (CoreIrBasicBlock *successor : cfg_analysis.get_successors(&block)) {
        auto phi_it = state.phis.find(successor);
        if (phi_it == state.phis.end() || phi_it->second == nullptr) {
            continue;
        }
        phi_it->second->add_incoming(&block, state.stack.back());
    }
}

void rename_memory_accesses(
    const CoreIrBasicBlock &block, const CoreIrCfgAnalysisResult &cfg_analysis,
    RenameState &state) {
    const std::size_t saved_depth = state.stack.size();

    auto phi_it = state.phis.find(&block);
    if (phi_it != state.phis.end() && phi_it->second != nullptr) {
        state.stack.push_back(phi_it->second);
    }

    for (const auto &instruction_ptr : block.get_instructions()) {
        const CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr) {
            continue;
        }

        if (instruction_is_memory_use(*instruction)) {
            auto access = std::make_unique<CoreIrMemoryUseAccess>(
                state.next_id++, instruction,
                state.stack.empty() ? nullptr : state.stack.back());
            CoreIrMemoryAccess *access_ptr = access.get();
            state.instruction_accesses.emplace(instruction, access_ptr);
            state.accesses.push_back(std::move(access));
            continue;
        }

        if (instruction_is_memory_def(*instruction)) {
            auto access = std::make_unique<CoreIrMemoryDefAccess>(
                state.next_id++, instruction,
                state.stack.empty() ? nullptr : state.stack.back());
            CoreIrMemoryAccess *access_ptr = access.get();
            state.instruction_accesses.emplace(instruction, access_ptr);
            state.accesses.push_back(std::move(access));
            state.stack.push_back(access_ptr);
        }
    }

    add_phi_incoming_to_successors(block, cfg_analysis, state);

    auto child_it = state.dominator_children.find(&block);
    if (child_it != state.dominator_children.end()) {
        for (const CoreIrBasicBlock *child : child_it->second) {
            rename_memory_accesses(*child, cfg_analysis, state);
        }
    }

    while (state.stack.size() > saved_depth) {
        state.stack.pop_back();
    }
}

} // namespace

CoreIrMemorySSAAnalysisResult::CoreIrMemorySSAAnalysisResult(
    const CoreIrFunction *function, CoreIrAliasAnalysisResult alias_analysis,
    std::unique_ptr<CoreIrMemoryLiveOnEntryAccess> live_on_entry,
    std::vector<std::unique_ptr<CoreIrMemoryAccess>> accesses,
    std::unordered_map<const CoreIrInstruction *, CoreIrMemoryAccess *>
        instruction_accesses,
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrMemoryPhiAccess *>>
        block_phis) noexcept
    : function_(function), alias_analysis_(std::move(alias_analysis)),
      live_on_entry_(std::move(live_on_entry)), accesses_(std::move(accesses)),
      instruction_accesses_(std::move(instruction_accesses)),
      block_phis_(std::move(block_phis)) {}

CoreIrMemoryAccess *CoreIrMemorySSAAnalysisResult::get_access_for_instruction(
    const CoreIrInstruction *instruction) const noexcept {
    auto it = instruction_accesses_.find(instruction);
    return it == instruction_accesses_.end() ? nullptr : it->second;
}

const std::vector<CoreIrMemoryPhiAccess *> &
CoreIrMemorySSAAnalysisResult::get_phis_for_block(
    const CoreIrBasicBlock *block) const {
    static const std::vector<CoreIrMemoryPhiAccess *> empty;
    auto it = block_phis_.find(block);
    return it == block_phis_.end() ? empty : it->second;
}

CoreIrMemoryAccess *CoreIrMemorySSAAnalysisResult::get_clobbering_access(
    const CoreIrInstruction *instruction) const noexcept {
    CoreIrMemoryAccess *access = get_access_for_instruction(instruction);
    if (access == nullptr) {
        return nullptr;
    }

    const CoreIrMemoryLocation *query_location =
        alias_analysis_.get_location_for_instruction(instruction);
    CoreIrMemoryAccess *cursor = nullptr;
    if (auto *use = dynamic_cast<CoreIrMemoryUseAccess *>(access); use != nullptr) {
        cursor = use->get_defining_access();
    } else if (auto *def = dynamic_cast<CoreIrMemoryDefAccess *>(access);
               def != nullptr) {
        cursor = def->get_defining_access();
    }

    while (cursor != nullptr) {
        if (dynamic_cast<CoreIrMemoryLiveOnEntryAccess *>(cursor) != nullptr ||
            dynamic_cast<CoreIrMemoryPhiAccess *>(cursor) != nullptr) {
            return cursor;
        }

        auto *def = dynamic_cast<CoreIrMemoryDefAccess *>(cursor);
        if (def == nullptr) {
            return cursor;
        }
        if (query_location == nullptr) {
            return cursor;
        }
        const CoreIrMemoryLocation *def_location =
            alias_analysis_.get_location_for_instruction(def->get_instruction());
        if (def_location == nullptr ||
            alias_core_ir_memory_locations(*query_location, *def_location) !=
                CoreIrAliasKind::NoAlias) {
            return cursor;
        }
        cursor = def->get_defining_access();
    }

    return nullptr;
}

CoreIrMemorySSAAnalysisResult CoreIrMemorySSAAnalysis::Run(
    const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree,
    const CoreIrDominanceFrontierAnalysisResult &dominance_frontier,
    const CoreIrFunctionEffectSummaryAnalysisResult & /*effect_summary*/,
    const CoreIrAliasAnalysisResult &alias_analysis) const {
    auto live_on_entry = std::make_unique<CoreIrMemoryLiveOnEntryAccess>(0);

    std::unordered_set<const CoreIrBasicBlock *> def_blocks;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            const CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction != nullptr && instruction_is_memory_def(*instruction)) {
                def_blocks.insert(block.get());
                break;
            }
        }
    }

    RenameState state;
    state.stack.push_back(live_on_entry.get());
    build_dominator_children(function, cfg_analysis, dominator_tree,
                             state.dominator_children);

    std::vector<const CoreIrBasicBlock *> worklist(def_blocks.begin(), def_blocks.end());
    std::unordered_set<const CoreIrBasicBlock *> queued(def_blocks.begin(),
                                                        def_blocks.end());
    while (!worklist.empty()) {
        const CoreIrBasicBlock *block = worklist.back();
        worklist.pop_back();
        if (block == nullptr) {
            continue;
        }
        for (CoreIrBasicBlock *frontier_block :
             dominance_frontier.get_frontier(block)) {
            if (frontier_block == nullptr ||
                state.phis.find(frontier_block) != state.phis.end()) {
                continue;
            }
            auto phi = std::make_unique<CoreIrMemoryPhiAccess>(state.next_id++,
                                                               frontier_block);
            CoreIrMemoryPhiAccess *phi_ptr = phi.get();
            state.phis.emplace(frontier_block, phi_ptr);
            state.block_phis[frontier_block].push_back(phi_ptr);
            state.accesses.push_back(std::move(phi));
            if (queued.insert(frontier_block).second) {
                worklist.push_back(frontier_block);
            }
        }
    }

    const CoreIrBasicBlock *entry = cfg_analysis.get_entry_block();
    if (entry != nullptr) {
        rename_memory_accesses(*entry, cfg_analysis, state);
    }

    return CoreIrMemorySSAAnalysisResult(
        &function, alias_analysis, std::move(live_on_entry),
        std::move(state.accesses), std::move(state.instruction_accesses),
        std::move(state.block_phis));
}

} // namespace sysycc
