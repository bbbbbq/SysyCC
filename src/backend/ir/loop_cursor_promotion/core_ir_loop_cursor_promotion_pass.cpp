#include "backend/ir/loop_cursor_promotion/core_ir_loop_cursor_promotion_pass.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::erase_instruction;

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

bool instruction_uses_stay_in_loop(const CoreIrInstruction &instruction,
                                   const CoreIrLoopInfo &loop) {
    for (const CoreIrUse &use : instruction.get_uses()) {
        CoreIrInstruction *user = use.get_user();
        if (user == nullptr ||
            !loop_contains_block(loop, user->get_parent())) {
            return false;
        }
    }
    return true;
}

CoreIrStoreInst *find_last_direct_store_to_slot(CoreIrBasicBlock &block,
                                                CoreIrStackSlot *slot) {
    auto &instructions = block.get_instructions();
    for (auto it = instructions.rbegin(); it != instructions.rend(); ++it) {
        auto *store = dynamic_cast<CoreIrStoreInst *>(it->get());
        if (store != nullptr && store->get_stack_slot() == slot) {
            return store;
        }
    }
    return nullptr;
}

struct CursorCandidate {
    CoreIrStackSlot *slot = nullptr;
    CoreIrBasicBlock *header = nullptr;
    CoreIrBasicBlock *preheader = nullptr;
    CoreIrBasicBlock *latch = nullptr;
    std::vector<CoreIrLoadInst *> loads;
    std::vector<CoreIrLoadInst *> exit_loads;
    CoreIrStoreInst *preheader_store = nullptr;
    CoreIrStoreInst *latch_store = nullptr;
};

std::size_t find_instruction_index(const CoreIrBasicBlock &block,
                                   const CoreIrInstruction *instruction) {
    const auto &instructions = block.get_instructions();
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        if (instructions[index].get() == instruction) {
            return index;
        }
    }
    return instructions.size();
}

bool block_has_direct_store_before(const CoreIrBasicBlock &block,
                                   const CoreIrInstruction *instruction,
                                   CoreIrStackSlot *slot) {
    for (const auto &instruction_ptr : block.get_instructions()) {
        CoreIrInstruction *current = instruction_ptr.get();
        if (current == instruction) {
            return false;
        }
        auto *store = dynamic_cast<CoreIrStoreInst *>(current);
        if (store != nullptr && store->get_stack_slot() == slot) {
            return true;
        }
    }
    return false;
}

std::vector<CoreIrLoadInst *> collect_header_exit_loads(
    const CoreIrLoopInfo &loop, const CoreIrCfgAnalysisResult &cfg,
    CoreIrStackSlot *slot) {
    std::vector<CoreIrLoadInst *> exit_loads;
    CoreIrBasicBlock *header = loop.get_header();
    if (slot == nullptr || header == nullptr) {
        return exit_loads;
    }

    for (CoreIrBasicBlock *exit_block : loop.get_exit_blocks()) {
        if (exit_block == nullptr ||
            loop_contains_block(loop, exit_block) ||
            cfg.get_predecessor_count(exit_block) != 1 ||
            cfg.get_predecessors(exit_block).front() != header) {
            continue;
        }
        for (const auto &instruction_ptr : exit_block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
            if (load == nullptr || load->get_stack_slot() != slot) {
                continue;
            }
            if (block_has_direct_store_before(*exit_block, load, slot)) {
                break;
            }
            exit_loads.push_back(load);
        }
    }

    return exit_loads;
}

std::vector<CursorCandidate> collect_candidates(const CoreIrLoopInfo &loop,
                                                const CoreIrCfgAnalysisResult &cfg) {
    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *preheader = loop.get_preheader();
    if (header == nullptr || preheader == nullptr ||
        loop.get_latches().size() != 1) {
        return {};
    }

    CoreIrBasicBlock *latch = *loop.get_latches().begin();
    if (latch == nullptr || latch == header) {
        return {};
    }

    std::unordered_map<CoreIrStackSlot *, std::vector<CoreIrLoadInst *>> loads_by_slot;
    std::unordered_map<CoreIrStackSlot *, std::vector<CoreIrStoreInst *>> stores_by_slot;
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction_ptr.get());
                load != nullptr && load->get_stack_slot() != nullptr) {
                loads_by_slot[load->get_stack_slot()].push_back(load);
                continue;
            }
            if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
                store != nullptr && store->get_stack_slot() != nullptr) {
                stores_by_slot[store->get_stack_slot()].push_back(store);
            }
        }
    }

    std::vector<CursorCandidate> candidates;
    for (const auto &[slot, stores] : stores_by_slot) {
        if (slot == nullptr || stores.size() != 1) {
            continue;
        }

        std::vector<CoreIrLoadInst *> candidate_loads;
        auto loads_it = loads_by_slot.find(slot);
        if (loads_it != loads_by_slot.end()) {
            const std::size_t latch_store_index =
                find_instruction_index(*latch, stores.front());
            for (CoreIrLoadInst *load : loads_it->second) {
                if (load == nullptr || !instruction_uses_stay_in_loop(*load, loop)) {
                    candidate_loads.clear();
                    break;
                }
                if (load->get_parent() == latch &&
                    find_instruction_index(*latch, load) >= latch_store_index) {
                    candidate_loads.clear();
                    break;
                }
                candidate_loads.push_back(load);
            }
        }

        std::vector<CoreIrLoadInst *> exit_loads =
            collect_header_exit_loads(loop, cfg, slot);
        if (candidate_loads.empty() && exit_loads.empty()) {
            continue;
        }

        CoreIrStoreInst *latch_store = stores.front();
        if (latch_store == nullptr || latch_store->get_parent() != latch) {
            continue;
        }

        CoreIrStoreInst *preheader_store =
            find_last_direct_store_to_slot(*preheader, slot);
        if (preheader_store == nullptr || preheader_store->get_value() == nullptr ||
            latch_store->get_value() == nullptr) {
            continue;
        }

        candidates.push_back(CursorCandidate{slot, header, preheader, latch,
                                             std::move(candidate_loads),
                                             std::move(exit_loads),
                                             preheader_store,
                                             latch_store});
    }
    return candidates;
}

bool promote_candidate(const CursorCandidate &candidate,
                       std::size_t &phi_counter) {
    auto phi = std::make_unique<CoreIrPhiInst>(
        candidate.loads.front()->get_type(),
        candidate.slot->get_name() + ".cursor." + std::to_string(phi_counter++));
    CoreIrPhiInst *phi_ptr = static_cast<CoreIrPhiInst *>(
        candidate.header->insert_instruction_before_first_non_phi(std::move(phi)));
    if (phi_ptr == nullptr) {
        return false;
    }

    phi_ptr->add_incoming(candidate.preheader, candidate.preheader_store->get_value());
    for (CoreIrLoadInst *load : candidate.loads) {
        if (load != nullptr) {
            load->replace_all_uses_with(phi_ptr);
        }
    }
    for (CoreIrLoadInst *exit_load : candidate.exit_loads) {
        if (exit_load != nullptr) {
            exit_load->replace_all_uses_with(phi_ptr);
        }
    }
    phi_ptr->add_incoming(candidate.latch, candidate.latch_store->get_value());
    bool changed = false;
    for (CoreIrLoadInst *load : candidate.loads) {
        if (load != nullptr && load->get_parent() != nullptr) {
            changed = erase_instruction(*load->get_parent(), load) || changed;
        }
    }
    for (CoreIrLoadInst *exit_load : candidate.exit_loads) {
        if (exit_load != nullptr && exit_load->get_parent() != nullptr) {
            changed =
                erase_instruction(*exit_load->get_parent(), exit_load) || changed;
        }
    }
    return changed;
}

} // namespace

PassKind CoreIrLoopCursorPromotionPass::Kind() const {
    return PassKind::CoreIrLoopCursorPromotion;
}

const char *CoreIrLoopCursorPromotionPass::Name() const {
    return "CoreIrLoopCursorPromotionPass";
}

PassResult CoreIrLoopCursorPromotionPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager =
        build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir analysis manager");
    }

    CoreIrPassEffects effects;
    std::size_t phi_counter = 0;
    for (const auto &function : module->get_functions()) {
        const CoreIrCfgAnalysisResult &cfg =
            analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
        bool function_changed = false;

        std::vector<const CoreIrLoopInfo *> ordered_loops;
        ordered_loops.reserve(loop_info.get_loops().size());
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr != nullptr) {
                ordered_loops.push_back(loop_ptr.get());
            }
        }
        std::stable_sort(ordered_loops.begin(), ordered_loops.end(),
                         [](const CoreIrLoopInfo *lhs, const CoreIrLoopInfo *rhs) {
                             if (lhs == nullptr || rhs == nullptr) {
                                 return rhs != nullptr;
                             }
                             return lhs->get_depth() > rhs->get_depth();
                         });

        for (const CoreIrLoopInfo *loop : ordered_loops) {
            for (const CursorCandidate &candidate : collect_candidates(*loop, cfg)) {
                function_changed =
                    promote_candidate(candidate, phi_counter) || function_changed;
            }
        }

        if (function_changed) {
            effects.changed_functions.insert(function.get());
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    effects.preserved_analyses.preserve_cfg_family();
    effects.preserved_analyses.preserve_loop_family();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
