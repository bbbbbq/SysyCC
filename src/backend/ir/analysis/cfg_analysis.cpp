#include "backend/ir/analysis/cfg_analysis.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

namespace {

void append_unique_block(std::vector<CoreIrBasicBlock *> &blocks,
                         CoreIrBasicBlock *block) {
    if (block == nullptr ||
        std::find(blocks.begin(), blocks.end(), block) != blocks.end()) {
        return;
    }
    blocks.push_back(block);
}

void connect_blocks(
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>> &
        predecessors,
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>> &
        successors,
    CoreIrBasicBlock *from, CoreIrBasicBlock *to) {
    if (from == nullptr || to == nullptr) {
        return;
    }

    auto successor_it = successors.find(from);
    auto predecessor_it = predecessors.find(to);
    if (successor_it == successors.end() || predecessor_it == predecessors.end()) {
        return;
    }

    append_unique_block(successor_it->second, to);
    append_unique_block(predecessor_it->second, from);
}

} // namespace

CoreIrCfgAnalysisResult::CoreIrCfgAnalysisResult(
    const CoreIrFunction *function, const CoreIrBasicBlock *entry_block,
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        predecessors,
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        successors,
    std::unordered_set<const CoreIrBasicBlock *> reachable_blocks) noexcept
    : function_(function), entry_block_(entry_block),
      predecessors_(std::move(predecessors)), successors_(std::move(successors)),
      reachable_blocks_(std::move(reachable_blocks)) {}

const std::vector<CoreIrBasicBlock *> &
CoreIrCfgAnalysisResult::empty_block_list() {
    static const std::vector<CoreIrBasicBlock *> empty;
    return empty;
}

const std::vector<CoreIrBasicBlock *> &
CoreIrCfgAnalysisResult::get_predecessors(const CoreIrBasicBlock *block) const {
    auto it = predecessors_.find(block);
    return it == predecessors_.end() ? empty_block_list() : it->second;
}

const std::vector<CoreIrBasicBlock *> &
CoreIrCfgAnalysisResult::get_successors(const CoreIrBasicBlock *block) const {
    auto it = successors_.find(block);
    return it == successors_.end() ? empty_block_list() : it->second;
}

std::size_t
CoreIrCfgAnalysisResult::get_predecessor_count(const CoreIrBasicBlock *block) const {
    return get_predecessors(block).size();
}

bool CoreIrCfgAnalysisResult::has_block(const CoreIrBasicBlock *block) const noexcept {
    return predecessors_.find(block) != predecessors_.end();
}

bool CoreIrCfgAnalysisResult::is_reachable(const CoreIrBasicBlock *block) const noexcept {
    return reachable_blocks_.find(block) != reachable_blocks_.end();
}

CoreIrCfgAnalysisResult CoreIrCfgAnalysis::Run(const CoreIrFunction &function) const {
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        predecessors;
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        successors;

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        predecessors.emplace(block.get(), std::vector<CoreIrBasicBlock *>{});
        successors.emplace(block.get(), std::vector<CoreIrBasicBlock *>{});
    }

    CoreIrBasicBlock *entry_block =
        function.get_basic_blocks().empty() ? nullptr
                                            : function.get_basic_blocks().front().get();

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }

        CoreIrInstruction *terminator = block->get_instructions().back().get();
        if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator); jump != nullptr) {
            connect_blocks(predecessors, successors, block.get(),
                           jump->get_target_block());
            continue;
        }

        if (auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
            cond_jump != nullptr) {
            connect_blocks(predecessors, successors, block.get(),
                           cond_jump->get_true_block());
            connect_blocks(predecessors, successors, block.get(),
                           cond_jump->get_false_block());
        }
    }

    std::unordered_set<const CoreIrBasicBlock *> reachable_blocks;
    if (entry_block != nullptr) {
        std::vector<CoreIrBasicBlock *> worklist{entry_block};
        while (!worklist.empty()) {
            CoreIrBasicBlock *block = worklist.back();
            worklist.pop_back();
            if (block == nullptr || !reachable_blocks.insert(block).second) {
                continue;
            }

            const auto successor_it = successors.find(block);
            if (successor_it == successors.end()) {
                continue;
            }
            for (CoreIrBasicBlock *successor : successor_it->second) {
                if (successor != nullptr) {
                    worklist.push_back(successor);
                }
            }
        }
    }

    return CoreIrCfgAnalysisResult(&function, entry_block, std::move(predecessors),
                                   std::move(successors),
                                   std::move(reachable_blocks));
}

} // namespace sysycc
