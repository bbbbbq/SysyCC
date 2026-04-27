#include "backend/ir/analysis/dominator_tree_analysis.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"

namespace sysycc {

namespace {

using BlockSet = std::unordered_set<const CoreIrBasicBlock *>;

void build_reverse_postorder(const CoreIrBasicBlock *entry_block,
                             const CoreIrCfgAnalysisResult &cfg_analysis,
                             std::vector<const CoreIrBasicBlock *> &rpo) {
    if (entry_block == nullptr) {
        return;
    }

    std::vector<const CoreIrBasicBlock *> postorder;
    std::unordered_set<const CoreIrBasicBlock *> visited;
    std::vector<std::pair<const CoreIrBasicBlock *, std::size_t>> stack;
    stack.emplace_back(entry_block, 0);
    visited.insert(entry_block);

    while (!stack.empty()) {
        const CoreIrBasicBlock *block = stack.back().first;
        std::size_t &next_successor_index = stack.back().second;
        const auto &successors = cfg_analysis.get_successors(block);

        while (next_successor_index < successors.size()) {
            CoreIrBasicBlock *successor = successors[next_successor_index++];
            if (successor == nullptr || !cfg_analysis.is_reachable(successor) ||
                !visited.insert(successor).second) {
                continue;
            }
            stack.emplace_back(successor, 0);
            block = nullptr;
            break;
        }

        if (block == nullptr) {
            continue;
        }

        postorder.push_back(block);
        stack.pop_back();
    }

    rpo.assign(postorder.rbegin(), postorder.rend());
}

std::size_t intersect_idom_chains(
    std::size_t lhs, std::size_t rhs, const std::vector<std::size_t> &idoms,
    std::size_t invalid_index) {
    std::size_t left = lhs;
    std::size_t right = rhs;
    while (left != invalid_index && right != invalid_index && left != right) {
        while (left != invalid_index && right != invalid_index && left > right) {
            left = idoms[left];
        }
        while (left != invalid_index && right != invalid_index && right > left) {
            right = idoms[right];
        }
    }
    return left == right ? left : invalid_index;
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
    if (dominator == block) {
        return true;
    }

    const CoreIrBasicBlock *cursor = block;
    while (cursor != nullptr) {
        auto it = immediate_dominators_.find(cursor);
        if (it == immediate_dominators_.end()) {
            return false;
        }
        cursor = it->second;
        if (cursor == dominator) {
            return true;
        }
    }
    return false;
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
    if (it != dominators_.end()) {
        return it->second;
    }
    if (block == nullptr || !is_reachable(block)) {
        return empty_block_set();
    }

    BlockSet block_dominators;
    const CoreIrBasicBlock *cursor = block;
    std::unordered_set<const CoreIrBasicBlock *> seen;
    while (cursor != nullptr && seen.insert(cursor).second) {
        block_dominators.insert(cursor);
        auto idom_it = immediate_dominators_.find(cursor);
        if (idom_it == immediate_dominators_.end()) {
            break;
        }
        cursor = idom_it->second;
    }

    auto inserted = dominators_.emplace(block, std::move(block_dominators));
    return inserted.first->second;
}

CoreIrDominatorTreeAnalysisResult CoreIrDominatorTreeAnalysis::Run(
    const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis) const {
    const CoreIrBasicBlock *entry_block = cfg_analysis.get_entry_block();

    BlockSet all_reachable;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        all_reachable.insert(block.get());
    }

    if (entry_block == nullptr || all_reachable.empty()) {
        return CoreIrDominatorTreeAnalysisResult(&function, {}, {}, {});
    }

    std::vector<const CoreIrBasicBlock *> rpo;
    build_reverse_postorder(entry_block, cfg_analysis, rpo);
    if (rpo.empty()) {
        return CoreIrDominatorTreeAnalysisResult(&function, {}, {}, {});
    }

    std::unordered_map<const CoreIrBasicBlock *, std::size_t> block_to_index;
    block_to_index.reserve(rpo.size());
    for (std::size_t index = 0; index < rpo.size(); ++index) {
        block_to_index.emplace(rpo[index], index);
    }

    const std::size_t invalid_index = rpo.size();
    std::vector<std::size_t> idoms(rpo.size(), invalid_index);
    idoms[0] = 0;

    // Cooper-Harvey-Kennedy style immediate-dominator iteration over RPO.
    // This avoids materializing and intersecting full dominator sets while the
    // fixed point converges, which is critical for large dispatch functions.
    bool changed = true;
    while (changed) {
        changed = false;
        for (const CoreIrBasicBlock *block : rpo) {
            if (block == entry_block) {
                continue;
            }

            std::size_t new_idom = invalid_index;
            for (CoreIrBasicBlock *predecessor : cfg_analysis.get_predecessors(block)) {
                if (predecessor == nullptr || !cfg_analysis.is_reachable(predecessor)) {
                    continue;
                }
                auto pred_index_it = block_to_index.find(predecessor);
                if (pred_index_it == block_to_index.end() ||
                    idoms[pred_index_it->second] == invalid_index) {
                    continue;
                }
                new_idom = new_idom == invalid_index
                               ? pred_index_it->second
                               : intersect_idom_chains(pred_index_it->second,
                                                       new_idom, idoms,
                                                       invalid_index);
            }
            if (new_idom == invalid_index) {
                continue;
            }

            const std::size_t block_index = block_to_index[block];
            if (idoms[block_index] == new_idom) {
                continue;
            }
            idoms[block_index] = new_idom;
            changed = true;
        }
    }

    std::unordered_map<const CoreIrBasicBlock *, CoreIrBasicBlock *>
        immediate_dominators;
    immediate_dominators.reserve(rpo.size());
    for (std::size_t index = 0; index < rpo.size(); ++index) {
        const std::size_t idom_index = idoms[index];
        if (index == 0 || idom_index == invalid_index || idom_index == index) {
            continue;
        }
        immediate_dominators.emplace(
            rpo[index], const_cast<CoreIrBasicBlock *>(rpo[idom_index]));
    }

    return CoreIrDominatorTreeAnalysisResult(
        &function, std::move(all_reachable), {},
        std::move(immediate_dominators));
}

} // namespace sysycc
