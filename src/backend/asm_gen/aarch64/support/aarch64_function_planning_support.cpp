#include "backend/asm_gen/aarch64/support/aarch64_function_planning_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

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

} // namespace sysycc
