#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::paths_overlap;
using sysycc::detail::trace_stack_slot_prefix;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

struct AddressValueKey {
    CoreIrOpcode opcode = CoreIrOpcode::AddressOfFunction;
    const void *entity = nullptr;
    const CoreIrType *type = nullptr;

    bool operator==(const AddressValueKey &other) const noexcept {
        return opcode == other.opcode && entity == other.entity &&
               type == other.type;
    }
};

struct AddressValueKeyHash {
    std::size_t operator()(const AddressValueKey &key) const noexcept {
        std::size_t hash = static_cast<std::size_t>(key.opcode);
        hash ^= reinterpret_cast<std::uintptr_t>(key.entity) + 0x9e3779b9U +
                (hash << 6U) + (hash >> 2U);
        hash ^= reinterpret_cast<std::uintptr_t>(key.type) + 0x9e3779b9U +
                (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

struct ExactPathValueKey {
    CoreIrStackSlot *stack_slot = nullptr;
    std::vector<std::uint64_t> path;
    const CoreIrType *type = nullptr;

    bool operator==(const ExactPathValueKey &other) const noexcept {
        return stack_slot == other.stack_slot && type == other.type &&
               path == other.path;
    }
};

struct ExactPathValueKeyHash {
    std::size_t operator()(const ExactPathValueKey &key) const noexcept {
        std::size_t hash = reinterpret_cast<std::uintptr_t>(key.stack_slot);
        hash ^= reinterpret_cast<std::uintptr_t>(key.type) + 0x9e3779b9U +
                (hash << 6U) + (hash >> 2U);
        for (std::uint64_t element : key.path) {
            hash ^= static_cast<std::size_t>(element) + 0x9e3779b9U +
                    (hash << 6U) + (hash >> 2U);
        }
        return hash;
    }
};

bool erase_instruction(CoreIrBasicBlock &block, CoreIrInstruction *instruction) {
    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [instruction](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == instruction;
        });
    if (it == instructions.end()) {
        return false;
    }
    (*it)->detach_operands();
    instructions.erase(it);
    return true;
}

bool instruction_precedes_in_block(const CoreIrInstruction &lhs,
                                   const CoreIrInstruction &rhs) {
    if (lhs.get_parent() == nullptr || lhs.get_parent() != rhs.get_parent()) {
        return false;
    }
    for (const auto &instruction_ptr : lhs.get_parent()->get_instructions()) {
        if (instruction_ptr.get() == &lhs) {
            return true;
        }
        if (instruction_ptr.get() == &rhs) {
            return false;
        }
    }
    return false;
}

CoreIrStackSlot *trace_root_stack_slot(CoreIrValue *value) {
    if (auto *address = dynamic_cast<CoreIrAddressOfStackSlotInst *>(value);
        address != nullptr) {
        return address->get_stack_slot();
    }
    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(value);
    if (gep == nullptr) {
        return nullptr;
    }
    return trace_root_stack_slot(gep->get_base());
}

bool is_safe_address_user(CoreIrInstruction &user, std::size_t operand_index) {
    if (auto *load = dynamic_cast<CoreIrLoadInst *>(&user); load != nullptr) {
        return operand_index == 0 && load->get_address() != nullptr;
    }
    if (auto *store = dynamic_cast<CoreIrStoreInst *>(&user); store != nullptr) {
        return operand_index == 1 && store->get_address() != nullptr;
    }
    if (dynamic_cast<CoreIrGetElementPtrInst *>(&user) != nullptr) {
        return operand_index == 0;
    }
    return false;
}

bool address_chain_has_only_safe_users(
    CoreIrValue *value, std::unordered_set<CoreIrInstruction *> &visiting) {
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return false;
    }
    if (!visiting.insert(instruction).second) {
        return true;
    }
    for (const CoreIrUse &use : instruction->get_uses()) {
        CoreIrInstruction *user = use.get_user();
        if (user == nullptr) {
            visiting.erase(instruction);
            return false;
        }
        if (!is_safe_address_user(*user, use.get_operand_index())) {
            visiting.erase(instruction);
            return false;
        }
        if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(user);
            gep != nullptr &&
            !address_chain_has_only_safe_users(gep, visiting)) {
            visiting.erase(instruction);
            return false;
        }
    }
    visiting.erase(instruction);
    return true;
}

bool address_chain_has_only_safe_users(CoreIrValue *value) {
    std::unordered_set<CoreIrInstruction *> visiting;
    return address_chain_has_only_safe_users(value, visiting);
}

std::optional<ExactPathValueKey> collect_exact_path_key(CoreIrValue *address,
                                                        const CoreIrType *type) {
    CoreIrStackSlot *stack_slot = nullptr;
    std::vector<std::uint64_t> path;
    bool exact_path = true;
    if (!trace_stack_slot_prefix(address, stack_slot, path, exact_path) ||
        stack_slot == nullptr || !exact_path || type == nullptr ||
        !address_chain_has_only_safe_users(address)) {
        return std::nullopt;
    }
    return ExactPathValueKey{stack_slot, std::move(path), type};
}

void invalidate_available_exact_path_values_for_root(
    CoreIrStackSlot *stack_slot, const std::vector<std::uint64_t> &path,
    std::unordered_map<ExactPathValueKey, CoreIrValue *, ExactPathValueKeyHash>
        &available_exact_path_values) {
    if (stack_slot == nullptr) {
        available_exact_path_values.clear();
        return;
    }
    for (auto it = available_exact_path_values.begin();
         it != available_exact_path_values.end();) {
        if (it->first.stack_slot == stack_slot &&
            paths_overlap(it->first.path, path)) {
            it = available_exact_path_values.erase(it);
            continue;
        }
        ++it;
    }
}

void invalidate_available_loads_for_store(
    const CoreIrStoreInst &store,
    std::unordered_map<CoreIrStackSlot *, CoreIrValue *> &available_loads,
    std::unordered_map<ExactPathValueKey, CoreIrValue *, ExactPathValueKeyHash>
        &available_exact_path_values) {
    if (store.get_stack_slot() != nullptr) {
        available_loads.erase(store.get_stack_slot());
        invalidate_available_exact_path_values_for_root(
            store.get_stack_slot(), {}, available_exact_path_values);
        return;
    }

    CoreIrStackSlot *root_slot = nullptr;
    std::vector<std::uint64_t> path;
    bool exact_path = true;
    if (trace_stack_slot_prefix(store.get_address(), root_slot, path, exact_path) &&
        root_slot != nullptr) {
        available_loads.erase(root_slot);
        invalidate_available_exact_path_values_for_root(
            root_slot, path, available_exact_path_values);
        return;
    }

    available_loads.clear();
    available_exact_path_values.clear();
}

struct ImmutableEntrySlotInfo {
    CoreIrStoreInst *store = nullptr;
    bool invalid = false;
};

bool propagate_immutable_entry_slots(CoreIrFunction &function) {
    CoreIrBasicBlock *entry =
        function.get_basic_blocks().empty() ? nullptr
                                            : function.get_basic_blocks().front().get();
    if (entry == nullptr) {
        return false;
    }

    std::unordered_map<CoreIrStackSlot *, ImmutableEntrySlotInfo> slot_infos;
    for (const auto &instruction_ptr : entry->get_instructions()) {
        auto *store = dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
        if (store == nullptr || store->get_stack_slot() == nullptr) {
            continue;
        }
        ImmutableEntrySlotInfo &info = slot_infos[store->get_stack_slot()];
        if (info.store != nullptr) {
            info.invalid = true;
        } else {
            info.store = store;
        }
    }

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

            if (auto *address = dynamic_cast<CoreIrAddressOfStackSlotInst *>(instruction);
                address != nullptr) {
                auto it = slot_infos.find(address->get_stack_slot());
                if (it == slot_infos.end()) {
                    continue;
                }
                for (const CoreIrUse &use : address->get_uses()) {
                    if (use.get_user() == nullptr ||
                        !is_safe_address_user(*use.get_user(), use.get_operand_index())) {
                        it->second.invalid = true;
                    }
                }
                continue;
            }

            if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(instruction);
                gep != nullptr) {
                if (CoreIrStackSlot *root_slot = trace_root_stack_slot(gep); root_slot != nullptr) {
                    if (auto it = slot_infos.find(root_slot); it != slot_infos.end()) {
                        for (const CoreIrUse &use : gep->get_uses()) {
                            if (use.get_user() == nullptr ||
                                !is_safe_address_user(*use.get_user(),
                                                      use.get_operand_index())) {
                                it->second.invalid = true;
                            }
                        }
                    }
                }
            }

            if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction); store != nullptr) {
                if (store->get_stack_slot() != nullptr) {
                    auto it = slot_infos.find(store->get_stack_slot());
                    if (it != slot_infos.end() && it->second.store != store) {
                        it->second.invalid = true;
                    }
                } else if (CoreIrStackSlot *root_slot =
                               trace_root_stack_slot(store->get_address());
                           root_slot != nullptr) {
                    if (auto it = slot_infos.find(root_slot); it != slot_infos.end()) {
                        it->second.invalid = true;
                    }
                }
                continue;
            }

            if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction); load != nullptr &&
                load->get_address() != nullptr) {
                if (CoreIrStackSlot *root_slot = trace_root_stack_slot(load->get_address());
                    root_slot != nullptr) {
                    if (auto it = slot_infos.find(root_slot); it != slot_infos.end()) {
                        it->second.invalid = true;
                    }
                }
            }
        }
    }

    bool changed = false;
    for (const auto &[slot, info] : slot_infos) {
        if (slot == nullptr || info.invalid || info.store == nullptr ||
            info.store->get_value() == nullptr) {
            continue;
        }
        for (const auto &block_ptr : function.get_basic_blocks()) {
            CoreIrBasicBlock *block = block_ptr.get();
            if (block == nullptr) {
                continue;
            }
            auto &instructions = block->get_instructions();
            std::size_t index = 0;
            while (index < instructions.size()) {
                auto *load = dynamic_cast<CoreIrLoadInst *>(instructions[index].get());
                if (load == nullptr || load->get_stack_slot() != slot ||
                    (block == entry &&
                     !instruction_precedes_in_block(*info.store, *load))) {
                    ++index;
                    continue;
                }
                load->replace_all_uses_with(info.store->get_value());
                erase_instruction(*block, load);
                changed = true;
            }
        }
    }

    return changed;
}

bool propagate_address_value(CoreIrBasicBlock &block, CoreIrInstruction *instruction,
                             std::unordered_map<AddressValueKey, CoreIrInstruction *,
                                                AddressValueKeyHash> &available_addresses) {
    AddressValueKey key;
    if (auto *address_of_function =
            dynamic_cast<CoreIrAddressOfFunctionInst *>(instruction);
        address_of_function != nullptr) {
        key = AddressValueKey{CoreIrOpcode::AddressOfFunction,
                              address_of_function->get_function(),
                              address_of_function->get_type()};
    } else if (auto *address_of_global =
                   dynamic_cast<CoreIrAddressOfGlobalInst *>(instruction);
               address_of_global != nullptr) {
        key = AddressValueKey{CoreIrOpcode::AddressOfGlobal,
                              address_of_global->get_global(),
                              address_of_global->get_type()};
    } else if (auto *address_of_stack_slot =
                   dynamic_cast<CoreIrAddressOfStackSlotInst *>(instruction);
               address_of_stack_slot != nullptr) {
        key = AddressValueKey{CoreIrOpcode::AddressOfStackSlot,
                              address_of_stack_slot->get_stack_slot(),
                              address_of_stack_slot->get_type()};
    } else {
        return false;
    }

    auto it = available_addresses.find(key);
    if (it == available_addresses.end()) {
        available_addresses.emplace(key, instruction);
        return false;
    }

    instruction->replace_all_uses_with(it->second);
    erase_instruction(block, instruction);
    return true;
}

bool propagate_load_copies(CoreIrBasicBlock &block) {
    bool changed = false;
    std::unordered_map<CoreIrStackSlot *, CoreIrValue *> available_loads;
    std::unordered_map<ExactPathValueKey, CoreIrValue *, ExactPathValueKeyHash>
        available_exact_path_values;
    std::unordered_map<AddressValueKey, CoreIrInstruction *, AddressValueKeyHash>
        available_addresses;
    auto &instructions = block.get_instructions();

    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() + index);
            changed = true;
            continue;
        }

        if (propagate_address_value(block, instruction, available_addresses)) {
            changed = true;
            continue;
        }

        if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction); store != nullptr) {
            if (store->get_stack_slot() != nullptr) {
                available_loads[store->get_stack_slot()] = store->get_value();
                invalidate_available_exact_path_values_for_root(
                    store->get_stack_slot(), {}, available_exact_path_values);
            } else {
                invalidate_available_loads_for_store(*store, available_loads,
                                                    available_exact_path_values);
                const std::optional<ExactPathValueKey> key =
                    collect_exact_path_key(
                        store->get_address(),
                        store->get_value() == nullptr ? nullptr
                                                      : store->get_value()->get_type());
                if (key.has_value() && store->get_value() != nullptr) {
                    available_exact_path_values[*key] = store->get_value();
                }
            }
            ++index;
            continue;
        }

        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction); load != nullptr) {
            if (load->get_stack_slot() != nullptr) {
                auto it = available_loads.find(load->get_stack_slot());
                if (it != available_loads.end() && it->second != nullptr) {
                    load->replace_all_uses_with(it->second);
                    erase_instruction(block, load);
                    changed = true;
                    continue;
                }
                available_loads[load->get_stack_slot()] = load;
            } else {
                const std::optional<ExactPathValueKey> key =
                    collect_exact_path_key(load->get_address(), load->get_type());
                if (key.has_value()) {
                    auto it = available_exact_path_values.find(*key);
                    if (it != available_exact_path_values.end() &&
                        it->second != nullptr) {
                        load->replace_all_uses_with(it->second);
                        erase_instruction(block, load);
                        changed = true;
                        continue;
                    }
                    available_exact_path_values[*key] = load;
                }
            }
            ++index;
            continue;
        }

        if (dynamic_cast<CoreIrCallInst *>(instruction) != nullptr) {
            available_loads.clear();
            available_exact_path_values.clear();
        }

        ++index;
    }

    return changed;
}

bool propagate_load_copies(CoreIrFunction &function) {
    bool changed = propagate_immutable_entry_slots(function);
    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }
        changed = propagate_load_copies(*block) || changed;
    }

    return changed;
}

} // namespace

PassKind CoreIrCopyPropagationPass::Kind() const {
    return PassKind::CoreIrCopyPropagation;
}

const char *CoreIrCopyPropagationPass::Name() const {
    return "CoreIrCopyPropagationPass";
}

PassResult CoreIrCopyPropagationPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const bool function_changed = propagate_load_copies(*function);
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
