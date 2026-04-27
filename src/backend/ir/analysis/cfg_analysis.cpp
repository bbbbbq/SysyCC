#include "backend/ir/analysis/cfg_analysis.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

namespace {

void connect_blocks_dense(
    const std::unordered_map<const CoreIrBasicBlock *, std::size_t> &block_indices,
    std::vector<std::vector<CoreIrBasicBlock *>> &predecessors,
    std::vector<std::vector<CoreIrBasicBlock *>> &successors,
    std::unordered_set<std::uint64_t> &edges, CoreIrBasicBlock *from,
    CoreIrBasicBlock *to) {
    if (from == nullptr || to == nullptr) {
        return;
    }

    auto from_it = block_indices.find(from);
    auto to_it = block_indices.find(to);
    if (from_it == block_indices.end() || to_it == block_indices.end()) {
        return;
    }

    const std::size_t from_index = from_it->second;
    const std::size_t to_index = to_it->second;
    const auto edge_key =
        (static_cast<std::uint64_t>(from_index) << 32U) |
        static_cast<std::uint64_t>(to_index);
    if (!edges.insert(edge_key).second) {
        return;
    }

    successors[from_index].push_back(to);
    predecessors[to_index].push_back(from);
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
    const auto &function_blocks = function.get_basic_blocks();
    std::vector<CoreIrBasicBlock *> blocks;
    blocks.reserve(function_blocks.size());
    std::unordered_map<const CoreIrBasicBlock *, std::size_t> block_indices;
    block_indices.reserve(function_blocks.size());

    for (const auto &block : function_blocks) {
        if (block == nullptr) {
            continue;
        }
        const std::size_t index = blocks.size();
        blocks.push_back(block.get());
        block_indices.emplace(block.get(), index);
    }

    std::vector<std::vector<CoreIrBasicBlock *>> predecessor_vectors(blocks.size());
    std::vector<std::vector<CoreIrBasicBlock *>> successor_vectors(blocks.size());
    std::unordered_set<std::uint64_t> edges;
    edges.reserve(blocks.size() * 2 + 1);

    CoreIrBasicBlock *entry_block = blocks.empty() ? nullptr : blocks.front();

    for (const auto &block : function_blocks) {
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }

        CoreIrInstruction *terminator = block->get_instructions().back().get();
        switch (terminator->get_opcode()) {
        case CoreIrOpcode::Jump: {
            auto *jump = static_cast<CoreIrJumpInst *>(terminator);
            connect_blocks_dense(block_indices, predecessor_vectors, successor_vectors,
                                 edges, block.get(), jump->get_target_block());
            break;
        }
        case CoreIrOpcode::CondJump: {
            auto *cond_jump = static_cast<CoreIrCondJumpInst *>(terminator);
            connect_blocks_dense(block_indices, predecessor_vectors, successor_vectors,
                                 edges, block.get(), cond_jump->get_true_block());
            connect_blocks_dense(block_indices, predecessor_vectors, successor_vectors,
                                 edges, block.get(), cond_jump->get_false_block());
            break;
        }
        case CoreIrOpcode::IndirectJump: {
            auto *indirect_jump = static_cast<CoreIrIndirectJumpInst *>(terminator);
            for (CoreIrBasicBlock *target : indirect_jump->get_target_blocks()) {
                connect_blocks_dense(block_indices, predecessor_vectors,
                                     successor_vectors, edges, block.get(), target);
            }
            break;
        }
        case CoreIrOpcode::Phi:
        case CoreIrOpcode::Binary:
        case CoreIrOpcode::Unary:
        case CoreIrOpcode::Compare:
        case CoreIrOpcode::Select:
        case CoreIrOpcode::Cast:
        case CoreIrOpcode::ExtractElement:
        case CoreIrOpcode::InsertElement:
        case CoreIrOpcode::ShuffleVector:
        case CoreIrOpcode::VectorReduceAdd:
        case CoreIrOpcode::AddressOfFunction:
        case CoreIrOpcode::AddressOfGlobal:
        case CoreIrOpcode::AddressOfStackSlot:
        case CoreIrOpcode::GetElementPtr:
        case CoreIrOpcode::Load:
        case CoreIrOpcode::Store:
        case CoreIrOpcode::Call:
        case CoreIrOpcode::Return:
            break;
        }
    }

    std::unordered_set<const CoreIrBasicBlock *> reachable_blocks;
    reachable_blocks.reserve(blocks.size());
    if (entry_block != nullptr) {
        std::vector<std::size_t> worklist{0};
        std::vector<bool> reachable_flags(blocks.size(), false);
        while (!worklist.empty()) {
            const std::size_t block_index = worklist.back();
            worklist.pop_back();
            if (block_index >= blocks.size() || reachable_flags[block_index]) {
                continue;
            }
            reachable_flags[block_index] = true;
            CoreIrBasicBlock *block = blocks[block_index];
            reachable_blocks.insert(block);

            for (CoreIrBasicBlock *successor : successor_vectors[block_index]) {
                auto successor_it = block_indices.find(successor);
                if (successor_it != block_indices.end()) {
                    worklist.push_back(successor_it->second);
                }
            }
        }
    }

    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        predecessors;
    std::unordered_map<const CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        successors;
    predecessors.reserve(blocks.size());
    successors.reserve(blocks.size());
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        predecessors.emplace(blocks[index], std::move(predecessor_vectors[index]));
        successors.emplace(blocks[index], std::move(successor_vectors[index]));
    }

    return CoreIrCfgAnalysisResult(&function, entry_block, std::move(predecessors),
                                   std::move(successors),
                                   std::move(reachable_blocks));
}

} // namespace sysycc
