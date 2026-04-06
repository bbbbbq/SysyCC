#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
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
                available_loads.erase(store->get_stack_slot());
            } else {
                available_loads.clear();
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
            }
            ++index;
            continue;
        }

        if (dynamic_cast<CoreIrCallInst *>(instruction) != nullptr) {
            available_loads.clear();
        }

        ++index;
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
        bool function_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            function_changed = propagate_load_copies(*block) || function_changed;
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
