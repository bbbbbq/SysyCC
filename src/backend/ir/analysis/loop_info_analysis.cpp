#include "backend/ir/analysis/loop_info_analysis.hpp"

#include <algorithm>
#include <vector>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

CoreIrLoopInfo *CoreIrLoopInfoAnalysisResult::create_loop(CoreIrBasicBlock *header) {
    loops_.push_back(std::make_unique<CoreIrLoopInfo>(header));
    CoreIrLoopInfo *loop = loops_.back().get();
    header_to_loop_[header] = loop;
    return loop;
}

CoreIrLoopInfo *
CoreIrLoopInfoAnalysisResult::get_loop_for_header(const CoreIrBasicBlock *header) const noexcept {
    auto it = header_to_loop_.find(header);
    return it == header_to_loop_.end() ? nullptr : it->second;
}

CoreIrLoopInfoAnalysisResult CoreIrLoopInfoAnalysis::Run(
    const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree) const {
    CoreIrLoopInfoAnalysisResult result(&function);

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        for (CoreIrBasicBlock *successor : cfg_analysis.get_successors(block.get())) {
            if (successor == nullptr || !cfg_analysis.is_reachable(successor) ||
                !dominator_tree.dominates(successor, block.get())) {
                continue;
            }
            CoreIrLoopInfo *loop = result.get_loop_for_header(successor);
            if (loop == nullptr) {
                loop = result.create_loop(successor);
                loop->mutable_blocks().insert(successor);
            }
            loop->mutable_latches().insert(block.get());

            std::vector<CoreIrBasicBlock *> worklist{block.get()};
            while (!worklist.empty()) {
                CoreIrBasicBlock *current = worklist.back();
                worklist.pop_back();
                if (current == nullptr || !loop->mutable_blocks().insert(current).second ||
                    current == successor) {
                    continue;
                }
                for (CoreIrBasicBlock *predecessor :
                     cfg_analysis.get_predecessors(current)) {
                    if (predecessor != nullptr) {
                        worklist.push_back(predecessor);
                    }
                }
            }
        }
    }

    for (const auto &loop_ptr : result.get_loops()) {
        CoreIrLoopInfo &loop = *loop_ptr;
        for (CoreIrBasicBlock *block : loop.get_blocks()) {
            for (CoreIrBasicBlock *successor : cfg_analysis.get_successors(block)) {
                if (successor != nullptr &&
                    loop.get_blocks().find(successor) == loop.get_blocks().end()) {
                    loop.mutable_exit_blocks().insert(successor);
                }
            }
        }

        std::vector<CoreIrBasicBlock *> outside_predecessors;
        for (CoreIrBasicBlock *predecessor :
             cfg_analysis.get_predecessors(loop.get_header())) {
            if (predecessor != nullptr &&
                loop.get_blocks().find(predecessor) == loop.get_blocks().end()) {
                outside_predecessors.push_back(predecessor);
            }
        }
        if (outside_predecessors.size() == 1 &&
            cfg_analysis.get_successors(outside_predecessors.front()).size() == 1) {
            loop.set_preheader(outside_predecessors.front());
        }
    }

    for (const auto &loop_ptr : result.get_loops()) {
        CoreIrLoopInfo *parent = nullptr;
        for (const auto &candidate_ptr : result.get_loops()) {
            if (loop_ptr.get() == candidate_ptr.get()) {
                continue;
            }
            const auto &blocks = loop_ptr->get_blocks();
            const auto &candidate_blocks = candidate_ptr->get_blocks();
            if (blocks.size() >= candidate_blocks.size()) {
                continue;
            }
            bool contained = true;
            for (CoreIrBasicBlock *block : blocks) {
                if (candidate_blocks.find(block) == candidate_blocks.end()) {
                    contained = false;
                    break;
                }
            }
            if (!contained) {
                continue;
            }
            if (parent == nullptr ||
                candidate_blocks.size() < parent->get_blocks().size()) {
                parent = candidate_ptr.get();
            }
        }
        loop_ptr->set_parent_loop(parent);
        if (parent != nullptr) {
            parent->mutable_subloops().push_back(loop_ptr.get());
            loop_ptr->set_depth(parent->get_depth() + 1);
        }
    }

    return result;
}

} // namespace sysycc
