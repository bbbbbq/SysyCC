#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/alias_analysis.hpp"
#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/memory_ssa_analysis.hpp"
#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
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
using sysycc::detail::are_equivalent_pointer_values;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

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

struct PendingStoreInfo {
    CoreIrStoreInst *store = nullptr;
};

struct StackSlotUseInfo {
    bool has_load = false;
    std::vector<CoreIrStoreInst *> stores;
    std::vector<CoreIrAddressOfStackSlotInst *> addresses;
};

void clear_pending_stores(std::vector<PendingStoreInfo> &pending_stores) {
    pending_stores.clear();
}

CoreIrStackSlot *get_memory_stack_slot(const CoreIrInstruction &instruction) {
    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        return load->get_stack_slot();
    }
    if (const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
        store != nullptr) {
        return store->get_stack_slot();
    }
    return nullptr;
}

CoreIrValue *get_memory_address_value(const CoreIrInstruction &instruction) {
    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        return load->get_address();
    }
    if (const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
        store != nullptr) {
        return store->get_address();
    }
    return nullptr;
}

bool instructions_share_exact_memory_access(const CoreIrInstruction &lhs,
                                            const CoreIrInstruction &rhs) {
    CoreIrStackSlot *lhs_slot = get_memory_stack_slot(lhs);
    CoreIrStackSlot *rhs_slot = get_memory_stack_slot(rhs);
    if (lhs_slot != nullptr || rhs_slot != nullptr) {
        return lhs_slot != nullptr && lhs_slot == rhs_slot && rhs_slot != nullptr;
    }

    CoreIrValue *lhs_address = get_memory_address_value(lhs);
    CoreIrValue *rhs_address = get_memory_address_value(rhs);
    return lhs_address != nullptr &&
           are_equivalent_pointer_values(lhs_address, rhs_address) &&
           rhs_address != nullptr;
}

CoreIrAliasKind get_precise_memory_alias_kind(
    const CoreIrInstruction &lhs, const CoreIrInstruction &rhs,
    const CoreIrAliasAnalysisResult &alias_analysis) {
    CoreIrAliasKind alias_kind = alias_analysis.alias_instructions(&lhs, &rhs);
    if (alias_kind != CoreIrAliasKind::MayAlias) {
        return alias_kind;
    }
    return instructions_share_exact_memory_access(lhs, rhs)
               ? CoreIrAliasKind::MustAlias
               : alias_kind;
}

bool eliminate_dead_stores(CoreIrBasicBlock &block,
                           const CoreIrAliasAnalysisResult &alias_analysis,
                           const CoreIrMemorySSAAnalysisResult &memory_ssa) {
    bool changed = false;
    std::vector<PendingStoreInfo> pending_stores;
    auto &instructions = block.get_instructions();

    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() + index);
            changed = true;
            continue;
        }

        if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction); store != nullptr) {
            for (std::size_t pending_index = 0; pending_index < pending_stores.size();) {
                const CoreIrAliasKind alias_kind = get_precise_memory_alias_kind(
                    *pending_stores[pending_index].store, *store, alias_analysis);
                if (alias_kind == CoreIrAliasKind::NoAlias) {
                    ++pending_index;
                    continue;
                }
                if (alias_kind == CoreIrAliasKind::MustAlias &&
                    pending_stores[pending_index].store != nullptr) {
                    const std::size_t previous_index =
                        find_instruction_index(block, pending_stores[pending_index].store);
                    if (previous_index < instructions.size() &&
                        erase_instruction(block, pending_stores[pending_index].store)) {
                        if (previous_index < index) {
                            --index;
                        }
                        changed = true;
                    }
                    pending_stores.erase(pending_stores.begin() +
                                         static_cast<std::ptrdiff_t>(pending_index));
                    continue;
                }
                clear_pending_stores(pending_stores);
                break;
            }
            pending_stores.push_back(PendingStoreInfo{store});
            ++index;
            continue;
        }

        const CoreIrEffectInfo effect = get_core_ir_instruction_effect(*instruction);
        if (!memory_behavior_reads(effect.memory_behavior) &&
            !memory_behavior_writes(effect.memory_behavior)) {
            ++index;
            continue;
        }

        if (memory_ssa.get_access_for_instruction(instruction) == nullptr) {
            clear_pending_stores(pending_stores);
            ++index;
            continue;
        }

        for (std::size_t pending_index = 0; pending_index < pending_stores.size();) {
            const CoreIrAliasKind alias_kind = get_precise_memory_alias_kind(
                *pending_stores[pending_index].store, *instruction, alias_analysis);
            if (alias_kind == CoreIrAliasKind::NoAlias) {
                ++pending_index;
                continue;
            }
            pending_stores.erase(pending_stores.begin() +
                                 static_cast<std::ptrdiff_t>(pending_index));
        }
        ++index;
    }

    return changed;
}

bool remove_store_only_dead_stack_slots(CoreIrFunction &function) {
    auto collect_address_only_dead_uses =
        [](CoreIrValue *value, auto &self,
           std::unordered_set<CoreIrInstruction *> &visited,
           std::vector<CoreIrInstruction *> &removable_instructions,
           std::vector<CoreIrStoreInst *> &stores) -> bool {
        if (value == nullptr) {
            return true;
        }
        for (const CoreIrUse &use : value->get_uses()) {
            CoreIrInstruction *user = use.get_user();
            if (user == nullptr) {
                continue;
            }
            if (auto *store = dynamic_cast<CoreIrStoreInst *>(user);
                store != nullptr) {
                if (store->get_address() != value) {
                    return false;
                }
                if (visited.insert(store).second) {
                    stores.push_back(store);
                }
                continue;
            }
            if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(user);
                gep != nullptr) {
                if (!visited.insert(gep).second) {
                    continue;
                }
                if (!self(gep, self, visited, removable_instructions, stores)) {
                    return false;
                }
                removable_instructions.push_back(gep);
                continue;
            }
            if (auto *cast = dynamic_cast<CoreIrCastInst *>(user);
                cast != nullptr) {
                if (!visited.insert(cast).second) {
                    continue;
                }
                if (!self(cast, self, visited, removable_instructions, stores)) {
                    return false;
                }
                removable_instructions.push_back(cast);
                continue;
            }
            return false;
        }
        return true;
    };

    bool changed = false;
    auto &stack_slots = function.get_stack_slots();
    std::unordered_map<CoreIrStackSlot *, StackSlotUseInfo> stack_slot_uses;
    stack_slot_uses.reserve(stack_slots.size());

    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }
            switch (instruction->get_opcode()) {
            case CoreIrOpcode::Load: {
                auto *load = static_cast<CoreIrLoadInst *>(instruction);
                if (load->get_stack_slot() != nullptr) {
                    stack_slot_uses[load->get_stack_slot()].has_load = true;
                }
                break;
            }
            case CoreIrOpcode::Store: {
                auto *store = static_cast<CoreIrStoreInst *>(instruction);
                if (store->get_stack_slot() != nullptr) {
                    stack_slot_uses[store->get_stack_slot()].stores.push_back(store);
                }
                break;
            }
            case CoreIrOpcode::AddressOfStackSlot: {
                auto *address =
                    static_cast<CoreIrAddressOfStackSlotInst *>(instruction);
                if (address->get_stack_slot() != nullptr) {
                    stack_slot_uses[address->get_stack_slot()].addresses.push_back(
                        address);
                }
                break;
            }
            default:
                break;
            }
        }
    }

    for (std::size_t slot_index = 0; slot_index < stack_slots.size();) {
        CoreIrStackSlot *stack_slot = stack_slots[slot_index].get();
        if (stack_slot == nullptr) {
            stack_slots.erase(stack_slots.begin() +
                              static_cast<std::ptrdiff_t>(slot_index));
            changed = true;
            continue;
        }

        bool has_load = false;
        bool has_address_use = false;
        std::vector<CoreIrStoreInst *> stores;
        std::vector<CoreIrInstruction *> removable_address_instructions;
        std::unordered_set<CoreIrInstruction *> visited_address_users;
        auto use_it = stack_slot_uses.find(stack_slot);
        if (use_it != stack_slot_uses.end()) {
            StackSlotUseInfo &use_info = use_it->second;
            has_load = use_info.has_load;
            stores = use_info.stores;
            if (!has_load) {
                for (CoreIrAddressOfStackSlotInst *address : use_info.addresses) {
                    if (address == nullptr) {
                        continue;
                    }
                    if (visited_address_users.insert(address).second) {
                        removable_address_instructions.push_back(address);
                    }
                    if (!collect_address_only_dead_uses(
                            address, collect_address_only_dead_uses,
                            visited_address_users, removable_address_instructions,
                            stores)) {
                        has_address_use = true;
                        break;
                    }
                }
            }
        }

        if (has_load || has_address_use || stores.empty()) {
            ++slot_index;
            continue;
        }

        for (CoreIrStoreInst *store : stores) {
            CoreIrBasicBlock *parent = store == nullptr ? nullptr : store->get_parent();
            if (parent != nullptr) {
                changed = erase_instruction(*parent, store) || changed;
            }
        }
        for (auto it = removable_address_instructions.rbegin();
             it != removable_address_instructions.rend(); ++it) {
            CoreIrInstruction *instruction = *it;
            CoreIrBasicBlock *parent =
                instruction == nullptr ? nullptr : instruction->get_parent();
            if (parent != nullptr) {
                changed = erase_instruction(*parent, instruction) || changed;
            }
        }
        stack_slots.erase(stack_slots.begin() + static_cast<std::ptrdiff_t>(slot_index));
        changed = true;
    }

    return changed;
}

} // namespace

PassKind CoreIrDeadStoreEliminationPass::Kind() const {
    return PassKind::CoreIrDeadStoreElimination;
}

const char *CoreIrDeadStoreEliminationPass::Name() const {
    return "CoreIrDeadStoreEliminationPass";
}

PassResult CoreIrDeadStoreEliminationPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }
    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir analysis manager");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrAliasAnalysisResult &alias_analysis =
            analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
        const CoreIrMemorySSAAnalysisResult &memory_ssa =
            analysis_manager->get_or_compute<CoreIrMemorySSAAnalysis>(*function);
        bool function_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            function_changed =
                eliminate_dead_stores(*block, alias_analysis, memory_ssa) ||
                function_changed;
        }
        function_changed =
            remove_store_only_dead_stack_slots(*function) || function_changed;
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
