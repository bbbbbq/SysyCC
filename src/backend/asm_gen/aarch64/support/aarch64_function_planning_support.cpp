#include "backend/asm_gen/aarch64/support/aarch64_function_planning_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"

namespace sysycc {

namespace {

std::size_t align_to(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

void assign_virtual_value_location(
    std::unordered_map<const CoreIrValue *, AArch64ValueLocation> &value_locations,
    const CoreIrValue *value, AArch64VirtualReg vreg) {
    value_locations[value] =
        AArch64ValueLocation{AArch64ValueLocationKind::VirtualReg, vreg};
}

void assign_memory_value_location(
    std::unordered_map<const CoreIrValue *, AArch64ValueLocation> &value_locations,
    std::unordered_map<const CoreIrValue *, std::size_t> &aggregate_value_offsets,
    const CoreIrValue *value, AArch64VirtualReg address_vreg,
    std::size_t offset) {
    value_locations[value] =
        AArch64ValueLocation{AArch64ValueLocationKind::MemoryAddress,
                             address_vreg};
    aggregate_value_offsets[value] = offset;
}

} // namespace

bool is_supported_native_value_type(const CoreIrType *type) {
    return is_void_type(type) || is_supported_scalar_storage_type(type) ||
           is_supported_object_type(type);
}

bool instruction_has_canonical_vreg(const CoreIrInstruction &instruction) {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Phi:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::Call:
        return !is_void_type(instruction.get_type());
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::GetElementPtr:
        return true;
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::Return:
        return false;
    }
    return false;
}

std::size_t allocate_aggregate_value_slot(std::size_t &current_offset,
                                          const CoreIrType *type) {
    current_offset = align_to(current_offset, get_type_alignment(type));
    current_offset += get_type_size(type);
    return current_offset;
}

bool validate_function_lowering_readiness(const CoreIrFunction &function,
                                          AArch64FunctionPlanningContext &context) {
    if (!is_supported_native_value_type(
            function.get_function_type()->get_return_type())) {
        context.report_error(
            "unsupported return type in AArch64 native backend for function '" +
            function.get_name() + "'");
        return false;
    }
    for (const auto &parameter : function.get_parameters()) {
        if (!is_supported_native_value_type(parameter->get_type())) {
            context.report_error(
                "unsupported parameter type in AArch64 native backend for function '" +
                function.get_name() + "'");
            return false;
        }
    }
    for (const auto &stack_slot : function.get_stack_slots()) {
        if (!is_supported_object_type(stack_slot->get_allocated_type())) {
            context.report_error(
                "unsupported stack slot type in AArch64 native backend for function '" +
                function.get_name() + "'");
            return false;
        }
    }
    return true;
}

void seed_incoming_stack_argument_offsets(
    const CoreIrFunction &function, const AArch64FunctionAbiInfo &abi_info,
    std::unordered_map<const CoreIrParameter *, std::size_t>
        &incoming_stack_argument_offsets) {
    for (std::size_t index = 0; index < function.get_parameters().size(); ++index) {
        const AArch64AbiAssignment &assignment = abi_info.parameters[index];
        if (!assignment.locations.empty() &&
            assignment.locations.front().kind == AArch64AbiLocationKind::Stack) {
            incoming_stack_argument_offsets.emplace(
                function.get_parameters()[index].get(),
                assignment.locations.front().stack_offset);
        }
    }
}

void layout_stack_slots(AArch64MachineFunction &machine_function,
                        const CoreIrFunction &function,
                        std::size_t &current_offset) {
    for (const auto &stack_slot : function.get_stack_slots()) {
        current_offset = align_to(current_offset,
                                  get_type_alignment(stack_slot->get_allocated_type()));
        current_offset += get_type_size(stack_slot->get_allocated_type());
        machine_function.get_frame_info().set_stack_slot_offset(stack_slot.get(),
                                                                current_offset);
    }
}

bool seed_function_value_locations(
    const CoreIrFunction &function, AArch64MachineFunction &machine_function,
    std::unordered_map<const CoreIrValue *, AArch64ValueLocation> &value_locations,
    std::unordered_map<const CoreIrValue *, std::size_t> &aggregate_value_offsets,
    std::size_t &current_offset, AArch64FunctionPlanningContext &context) {
    for (const auto &parameter : function.get_parameters()) {
        if (!is_supported_native_value_type(parameter->get_type())) {
            context.report_error("unsupported parameter type in AArch64 native "
                                 "backend for function '" +
                                 function.get_name() + "'");
            return false;
        }
        if (is_aggregate_type(parameter->get_type())) {
            assign_memory_value_location(
                value_locations, aggregate_value_offsets, parameter.get(),
                context.create_pointer_virtual_reg(machine_function),
                allocate_aggregate_value_slot(current_offset,
                                              parameter->get_type()));
        } else {
            assign_virtual_value_location(
                value_locations, parameter.get(),
                context.create_virtual_reg(machine_function,
                                           parameter->get_type()));
        }
    }

    for (const auto &basic_block : function.get_basic_blocks()) {
        for (const auto &instruction : basic_block->get_instructions()) {
            if (!instruction_has_canonical_vreg(*instruction)) {
                continue;
            }
            if (!is_supported_native_value_type(instruction->get_type())) {
                context.report_error(
                    "unsupported Core IR value type in AArch64 native backend "
                    "for function '" +
                    function.get_name() + "'");
                return false;
            }
            if (is_aggregate_type(instruction->get_type())) {
                assign_memory_value_location(
                    value_locations, aggregate_value_offsets, instruction.get(),
                    context.create_pointer_virtual_reg(machine_function),
                    allocate_aggregate_value_slot(current_offset,
                                                  instruction->get_type()));
            } else {
                assign_virtual_value_location(
                    value_locations, instruction.get(),
                    context.create_virtual_reg(machine_function,
                                               instruction->get_type()));
            }
        }
    }
    return true;
}

void seed_call_argument_copy_slots(
    const CoreIrFunction &function,
    std::unordered_map<const CoreIrCallInst *, std::vector<std::size_t>>
        &indirect_call_argument_copy_offsets,
    std::size_t &current_offset, AArch64FunctionPlanningContext &context) {
    for (const auto &basic_block : function.get_basic_blocks()) {
        for (const auto &instruction : basic_block->get_instructions()) {
            const auto *call = dynamic_cast<const CoreIrCallInst *>(instruction.get());
            if (call == nullptr) {
                continue;
            }
            const AArch64FunctionAbiInfo abi_info = context.classify_call(*call);
            std::vector<std::size_t> offsets;
            offsets.resize(abi_info.parameters.size(), 0);
            for (std::size_t index = 0; index < abi_info.parameters.size(); ++index) {
                if (!abi_info.parameters[index].is_indirect) {
                    continue;
                }
                offsets[index] = allocate_aggregate_value_slot(
                    current_offset,
                    call->get_operands()[call->get_argument_begin_index() + index]
                        ->get_type());
            }
            if (!offsets.empty()) {
                indirect_call_argument_copy_offsets.emplace(call, std::move(offsets));
            }
        }
    }
}

void seed_promoted_stack_slots(
    const CoreIrFunction &function,
    const std::unordered_map<const CoreIrValue *, AArch64ValueLocation> &value_locations,
    std::unordered_set<const CoreIrStackSlot *> &promoted_stack_slots) {
    const CoreIrBasicBlock *entry_block =
        function.get_basic_blocks().empty() ? nullptr :
                                              function.get_basic_blocks().front().get();
    std::unordered_set<const CoreIrStackSlot *> address_taken;
    std::unordered_map<const CoreIrStackSlot *, std::size_t> direct_store_count;
    std::unordered_map<const CoreIrStackSlot *, const CoreIrStoreInst *> entry_store;

    for (const auto &basic_block : function.get_basic_blocks()) {
        for (const auto &instruction : basic_block->get_instructions()) {
            if (const auto *address_of_stack_slot =
                    dynamic_cast<const CoreIrAddressOfStackSlotInst *>(
                        instruction.get());
                address_of_stack_slot != nullptr) {
                address_taken.insert(address_of_stack_slot->get_stack_slot());
                continue;
            }
            const auto *store = dynamic_cast<const CoreIrStoreInst *>(instruction.get());
            if (store == nullptr || store->get_stack_slot() == nullptr) {
                continue;
            }
            ++direct_store_count[store->get_stack_slot()];
            if (basic_block.get() == entry_block) {
                entry_store.emplace(store->get_stack_slot(), store);
            }
        }
    }

    for (const auto &stack_slot : function.get_stack_slots()) {
        if (!is_supported_value_type(stack_slot->get_allocated_type())) {
            continue;
        }
        if (address_taken.find(stack_slot.get()) != address_taken.end()) {
            continue;
        }
        const auto count_it = direct_store_count.find(stack_slot.get());
        if (count_it == direct_store_count.end() || count_it->second != 1) {
            continue;
        }
        const auto entry_store_it = entry_store.find(stack_slot.get());
        if (entry_store_it == entry_store.end()) {
            continue;
        }
        const CoreIrValue *stored_value = entry_store_it->second->get_value();
        const auto location_it = value_locations.find(stored_value);
        if (location_it == value_locations.end() ||
            location_it->second.kind != AArch64ValueLocationKind::VirtualReg) {
            continue;
        }
        promoted_stack_slots.insert(stack_slot.get());
    }
}

} // namespace sysycc
