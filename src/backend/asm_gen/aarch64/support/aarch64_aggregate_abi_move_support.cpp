#include "backend/asm_gen/aarch64/support/aarch64_aggregate_abi_move_support.hpp"

#include <algorithm>

#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

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

} // namespace

std::size_t compute_call_stack_arg_bytes(const AArch64FunctionAbiInfo &abi_info) {
    std::size_t stack_arg_bytes = 0;
    for (const AArch64AbiAssignment &assignment : abi_info.parameters) {
        for (const AArch64AbiLocation &location : assignment.locations) {
            if (location.kind != AArch64AbiLocationKind::Stack) {
                continue;
            }
            stack_arg_bytes = std::max(
                stack_arg_bytes,
                align_to(location.stack_offset + assignment.stack_size, 16));
        }
    }
    return stack_arg_bytes;
}

const CoreIrType *type_for_abi_location(const AArch64AbiLocation &location) {
    static CoreIrIntegerType i8_type(8);
    static CoreIrIntegerType i16_type(16);
    static CoreIrIntegerType i32_type(32);
    static CoreIrIntegerType i64_type(64);
    static CoreIrFloatType f16_type(CoreIrFloatKind::Float16);
    static CoreIrFloatType f32_type(CoreIrFloatKind::Float32);
    static CoreIrFloatType f64_type(CoreIrFloatKind::Float64);
    static CoreIrFloatType f128_type(CoreIrFloatKind::Float128);

    switch (location.reg_kind) {
    case AArch64VirtualRegKind::Float16:
        return &f16_type;
    case AArch64VirtualRegKind::Float32:
        return &f32_type;
    case AArch64VirtualRegKind::Float64:
        return &f64_type;
    case AArch64VirtualRegKind::Float128:
        return &f128_type;
    case AArch64VirtualRegKind::General64:
        if (location.size >= 8) {
            return &i64_type;
        }
        [[fallthrough]];
    case AArch64VirtualRegKind::General32:
        if (location.size <= 1) {
            return &i8_type;
        }
        if (location.size <= 2) {
            return &i16_type;
        }
        if (location.size <= 4) {
            return &i32_type;
        }
        return &i64_type;
    }
    return &i64_type;
}

bool copy_aggregate_from_assignment_to_memory(
    AArch64MachineBlock &machine_block, const AArch64AbiAssignment &assignment,
    const CoreIrType *value_type, const AArch64VirtualReg &destination_address,
    AArch64MachineFunction &function, AArch64AbiEmissionContext &context) {
    if (assignment.is_indirect) {
        const AArch64AbiLocation &location = assignment.locations.front();
        AArch64VirtualReg source_address =
            context.create_pointer_virtual_reg(function);
        if (location.kind == AArch64AbiLocationKind::GeneralRegister) {
            context.append_copy_from_physical_reg(machine_block, source_address,
                                                  location.physical_reg,
                                                  location.reg_kind);
        } else if (location.kind == AArch64AbiLocationKind::Stack) {
            context.append_load_from_incoming_stack_arg(
                machine_block, context.create_fake_pointer_type(), source_address,
                location.stack_offset, function);
        } else {
            context.report_error("unsupported indirect aggregate ABI source");
            return false;
        }
        return context.emit_memory_copy(machine_block, destination_address,
                                        source_address, value_type, function);
    }

    for (const AArch64AbiLocation &location : assignment.locations) {
        if (location.kind == AArch64AbiLocationKind::Stack) {
            AArch64VirtualReg source_address =
                context.create_pointer_virtual_reg(function);
            if (!context.materialize_incoming_stack_address(
                    machine_block, source_address, location.stack_offset, function) ||
                !context.emit_memory_copy(machine_block, destination_address,
                                          source_address, value_type, function)) {
                return false;
            }
            continue;
        }
        AArch64VirtualReg temp = function.create_virtual_reg(location.reg_kind);
        context.append_copy_from_physical_reg(machine_block, temp,
                                              location.physical_reg,
                                              location.reg_kind);
        if (!context.append_store_to_address(machine_block,
                                             type_for_abi_location(location), temp,
                                             destination_address,
                                             location.source_offset, function)) {
            return false;
        }
    }
    return true;
}

bool copy_aggregate_from_memory_to_assignment(
    AArch64MachineBlock &machine_block, const AArch64AbiAssignment &assignment,
    const CoreIrType *value_type, const AArch64VirtualReg &source_address,
    AArch64MachineFunction &function, AArch64AbiEmissionContext &context,
    const std::vector<std::size_t> *indirect_call_argument_copy_offsets,
    const AArch64VirtualReg *stack_base_address, std::size_t argument_index) {
    if (assignment.is_indirect) {
        if (indirect_call_argument_copy_offsets == nullptr ||
            argument_index >= indirect_call_argument_copy_offsets->size() ||
            (*indirect_call_argument_copy_offsets)[argument_index] == 0) {
            context.report_error("missing aggregate call copy slot for indirect "
                                 "AArch64 argument");
            return false;
        }
        AArch64VirtualReg copy_address =
            context.create_pointer_virtual_reg(function);
        context.append_frame_address(machine_block, copy_address,
                                     (*indirect_call_argument_copy_offsets)[argument_index],
                                     function);
        if (!context.emit_memory_copy(machine_block, copy_address, source_address,
                                      value_type, function)) {
            return false;
        }
        const AArch64AbiLocation &location = assignment.locations.front();
        if (location.kind == AArch64AbiLocationKind::GeneralRegister) {
            context.append_copy_to_physical_reg(machine_block,
                                                location.physical_reg,
                                                location.reg_kind, copy_address);
            return true;
        }
        if (location.kind == AArch64AbiLocationKind::Stack &&
            stack_base_address != nullptr) {
            return context.append_store_to_address(
                machine_block, context.create_fake_pointer_type(), copy_address,
                *stack_base_address, location.stack_offset, function);
        }
        context.report_error(
            "missing outgoing stack base address for a stack-indirect "
            "aggregate call argument in the AArch64 native backend");
        return false;
    }

    for (const AArch64AbiLocation &location : assignment.locations) {
        AArch64VirtualReg temp = function.create_virtual_reg(location.reg_kind);
        if (!context.append_load_from_address(machine_block,
                                              type_for_abi_location(location), temp,
                                              source_address, location.source_offset,
                                              function)) {
            return false;
        }
        if (location.kind == AArch64AbiLocationKind::GeneralRegister ||
            location.kind == AArch64AbiLocationKind::FloatingRegister) {
            context.append_copy_to_physical_reg(machine_block,
                                                location.physical_reg,
                                                location.reg_kind, temp);
            continue;
        }
        if (stack_base_address == nullptr ||
            !context.append_store_to_address(machine_block,
                                             type_for_abi_location(location), temp,
                                             *stack_base_address,
                                             location.stack_offset, function)) {
            return false;
        }
    }
    return true;
}

} // namespace sysycc
