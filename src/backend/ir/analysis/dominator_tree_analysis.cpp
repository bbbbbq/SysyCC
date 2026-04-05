#include "backend/ir/analysis/dominator_tree_analysis.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"

namespace sysycc {

namespace {

using BlockSet = std::unordered_set<const CoreIrBasicBlock *>;

bool block_sets_equal(const BlockSet &lhs, const BlockSet &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (const CoreIrBasicBlock *block : lhs) {
        if (rhs.find(block) == rhs.end()) {
            return false;
        }
    }
    return true;
}

BlockSet intersect_predecessor_dominators(
    const std::vector<CoreIrBasicBlock *> &predecessors,
    const std::unordered_map<const CoreIrBasicBlock *, BlockSet> &dominators,
    const CoreIrCfgAnalysisResult &cfg_analysis) {
    bool initialized = false;
    BlockSet intersection;

    for (CoreIrBasicBlock *predecessor : predecessors) {
        if (predecessor == nullptr || !cfg_analysis.is_reachable(predecessor)) {
            continue;
        }

        auto it = dominators.find(predecessor);
        if (it == dominators.end()) {
            continue;
        }

        if (!initialized) {
            intersection = it->second;
            initialized = true;
            continue;
        }

        for (auto candidate = intersection.begin(); candidate != intersection.end();) {
            if (it->second.find(*candidate) == it->second.end()) {
                candidate = intersection.erase(candidate);
                continue;
            }
            ++candidate;
        }
    }

    return intersection;
}

} // namespace

CoreIrDominatorTreeAnalysisResult::CoreIrDominatorTreeAnalysisResult(
    const CoreIrFunction *function,
    std::unordered_set<const CoreIrBasicBlock *> reachable_blocks,
    std::unordered_map<const CoreIrBasicBlock *,
                       std::unordered_set<const CoreIrBasicBlock *>>
        dominators,
    std::unordered_map<const CoreIrBasicBlock *, CoreIrBasicBlock *>
        immediate_dominators) noexcept
    : function_(function), reachable_blocks_(std::move(reachable_blocks)),
      dominators_(std::move(dominators)),
      immediate_dominators_(std::move(immediate_dominators)) {}

const std::unordered_set<const CoreIrBasicBlock *> &
CoreIrDominatorTreeAnalysisResult::empty_block_set() {
    static const std::unordered_set<const CoreIrBasicBlock *> empty;
    return empty;
}

bool CoreIrDominatorTreeAnalysisResult::is_reachable(
    const CoreIrBasicBlock *block) const noexcept {
    return reachable_blocks_.find(block) != reachable_blocks_.end();
}

bool CoreIrDominatorTreeAnalysisResult::dominates(
    const CoreIrBasicBlock *dominator, const CoreIrBasicBlock *block) const noexcept {
    if (dominator == nullptr || block == nullptr || !is_reachable(block)) {
        return false;
    }

    auto it = dominators_.find(block);
    return it != dominators_.end() &&
           it->second.find(dominator) != it->second.end();
}

CoreIrBasicBlock *CoreIrDominatorTreeAnalysisResult::get_immediate_dominator(
    const CoreIrBasicBlock *block) const noexcept {
    auto it = immediate_dominators_.find(block);
    return it == immediate_dominators_.end() ? nullptr : it->second;
}

const std::unordered_set<const CoreIrBasicBlock *> &
CoreIrDominatorTreeAnalysisResult::get_dominators(
    const CoreIrBasicBlock *block) const {
    auto it = dominators_.find(block);
    return it == dominators_.end() ? empty_block_set() : it->second;
}

CoreIrDominatorTreeAnalysisResult CoreIrDominatorTreeAnalysis::Run(
    const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis) const {
    const CoreIrBasicBlock *entry_block = cfg_analysis.get_entry_block();

    BlockSet all_reachable;
    std::vector<const CoreIrBasicBlock *> reachable_blocks;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        all_reachable.insert(block.get());
        reachable_blocks.push_back(block.get());
    }

    if (entry_block == nullptr || all_reachable.empty()) {
        return CoreIrDominatorTreeAnalysisResult(&function, {}, {}, {});
    }

    std::unordered_map<const CoreIrBasicBlock *, BlockSet> dominators;
    for (const CoreIrBasicBlock *block : reachable_blocks) {
        dominators.emplace(block, all_reachable);
    }
    dominators[entry_block] = BlockSet{entry_block};

    bool changed = true;
    while (changed) {
        changed = false;
        for (const CoreIrBasicBlock *block : reachable_blocks) {
            if (block == entry_block) {
                continue;
            }

            BlockSet next_dominators = intersect_predecessor_dominators(
                cfg_analysis.get_predecessors(block), dominators, cfg_analysis);
            next_dominators.insert(block);

            auto current = dominators.find(block);
            if (current == dominators.end() ||
                block_sets_equal(current->second, next_dominators)) {
                continue;
            }
            current->second = std::move(next_dominators);
            changed = true;
        }
    }

    std::unordered_map<const CoreIrBasicBlock *, CoreIrBasicBlock *>
        immediate_dominators;
    for (const CoreIrBasicBlock *block : reachable_blocks) {
        if (block == entry_block) {
            continue;
        }

        CoreIrBasicBlock *best = nullptr;
        std::size_t best_depth = 0;
        const auto dominator_it = dominators.find(block);
        if (dominator_it == dominators.end()) {
            continue;
        }

        for (const CoreIrBasicBlock *candidate : dominator_it->second) {
            if (candidate == block) {
                continue;
            }
            const std::size_t candidate_depth = dominators[candidate].size();
            if (best == nullptr || candidate_depth > best_depth) {
                best = const_cast<CoreIrBasicBlock *>(candidate);
                best_depth = candidate_depth;
            }
        }

        if (best != nullptr) {
            immediate_dominators.emplace(block, best);
        }
    }

    return CoreIrDominatorTreeAnalysisResult(
        &function, std::move(all_reachable), std::move(dominators),
        std::move(immediate_dominators));
}

} // namespace sysycc
