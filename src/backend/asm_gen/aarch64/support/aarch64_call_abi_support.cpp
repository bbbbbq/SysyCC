#include "backend/asm_gen/aarch64/support/aarch64_call_abi_support.hpp"

#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_frame_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_memory_access_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

namespace {

struct PreparedCallArgument {
    CoreIrValue *value = nullptr;
    const AArch64AbiAssignment *assignment = nullptr;
    std::size_t argument_index = 0;
    bool is_aggregate = false;
    AArch64VirtualReg value_reg{};
    AArch64VirtualReg address_reg{};
    std::size_t register_capture_frame_offset = 0;
};

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

std::size_t allocate_call_register_capture_slot(
    AArch64MachineFunction &machine_function, const CoreIrType *type) {
    AArch64FunctionFrameInfo &frame_info = machine_function.get_frame_info();
    const std::size_t size = get_storage_size(type);
    const std::size_t alignment = get_storage_alignment(type);
    std::size_t local_size = frame_info.get_local_size();
    local_size = align_to(local_size, alignment);
    local_size += size;
    frame_info.set_local_size(local_size);
    frame_info.set_frame_size(align_to(local_size, 16));
    return local_size;
}

void append_load_physical_register_argument_from_frame(
    AArch64MachineBlock &block, unsigned physical_reg,
    AArch64VirtualRegKind reg_kind, const CoreIrType *type,
    std::size_t frame_offset) {
    append_physical_frame_load(
        block.get_instructions(), physical_reg, reg_kind, get_storage_size(type),
        frame_offset, static_cast<unsigned>(AArch64PhysicalReg::X9));
}

} // namespace

bool emit_call_with_abi(AArch64MachineBlock &machine_block, const CoreIrCallInst &call,
                        const AArch64FunctionAbiInfo &abi_info,
                        AArch64MachineFunction &machine_function,
                        AArch64AbiEmissionContext &context,
                        const std::vector<std::size_t> *indirect_copy_offsets) {
    const auto &arguments = call.get_operands();
    const std::size_t stack_arg_bytes = compute_call_stack_arg_bytes(abi_info);
    std::optional<AArch64VirtualReg> stack_base_address;
    if (stack_arg_bytes > 0) {
        stack_base_address = context.prepare_stack_argument_area(
            machine_block, stack_arg_bytes, machine_function);
    }

    if (abi_info.return_value.is_indirect && !is_void_type(call.get_type())) {
        AArch64VirtualReg result_address;
        if (!context.materialize_canonical_memory_address(machine_block, &call,
                                                          result_address)) {
            return false;
        }
        context.append_copy_to_physical_reg(
            machine_block, static_cast<unsigned>(AArch64PhysicalReg::X8),
            AArch64VirtualRegKind::General64, result_address);
    }

    std::vector<PreparedCallArgument> prepared_arguments;
    prepared_arguments.reserve(arguments.size() - call.get_argument_begin_index());
    for (std::size_t index = call.get_argument_begin_index();
         index < arguments.size(); ++index) {
        CoreIrValue *argument = arguments[index];
        const std::size_t argument_index = index - call.get_argument_begin_index();
        const AArch64AbiAssignment &assignment = abi_info.parameters[argument_index];
        PreparedCallArgument prepared_argument{
            .value = argument,
            .assignment = &assignment,
            .argument_index = argument_index,
            .is_aggregate = is_aggregate_type(argument->get_type()),
        };

        if (prepared_argument.is_aggregate) {
            if (!context.ensure_value_in_memory_address(machine_block, argument,
                                                        prepared_argument.address_reg)) {
                context.report_error(
                    "failed to materialize aggregate call argument #" +
                    std::to_string(argument_index) + " for AArch64 call");
                return false;
            }
        } else if (!context.ensure_value_in_vreg(machine_block, argument,
                                                 prepared_argument.value_reg)) {
            return false;
        }

        prepared_arguments.push_back(prepared_argument);
    }

    for (PreparedCallArgument &prepared_argument : prepared_arguments) {
        const AArch64AbiAssignment &assignment = *prepared_argument.assignment;
        const AArch64AbiLocation &location = assignment.locations.front();
        if (location.kind != AArch64AbiLocationKind::Stack) {
            continue;
        }
        if (prepared_argument.is_aggregate) {
            if (!copy_aggregate_from_memory_to_assignment(
                    machine_block, assignment, prepared_argument.value->get_type(),
                    prepared_argument.address_reg, machine_function, context,
                    indirect_copy_offsets,
                    stack_base_address.has_value() ? &*stack_base_address : nullptr,
                    prepared_argument.argument_index)) {
                return false;
            }
            continue;
        }
        if (!stack_base_address.has_value() ||
            !context.append_store_to_address(machine_block,
                                             prepared_argument.value->get_type(),
                                             prepared_argument.value_reg,
                                             *stack_base_address,
                                             location.stack_offset,
                                             machine_function)) {
            context.report_error(
                "missing outgoing stack base address for stack-passed AArch64 "
                "call argument");
            return false;
        }
    }

    for (PreparedCallArgument &prepared_argument : prepared_arguments) {
        const AArch64AbiAssignment &assignment = *prepared_argument.assignment;
        const AArch64AbiLocation &location = assignment.locations.front();
        if (location.kind == AArch64AbiLocationKind::Stack) {
            continue;
        }
        if (prepared_argument.is_aggregate) {
            if (!copy_aggregate_from_memory_to_assignment(
                    machine_block, assignment, prepared_argument.value->get_type(),
                    prepared_argument.address_reg, machine_function, context,
                    indirect_copy_offsets,
                    stack_base_address.has_value() ? &*stack_base_address : nullptr,
                    prepared_argument.argument_index)) {
                return false;
            }
            continue;
        }
        if (location.kind == AArch64AbiLocationKind::GeneralRegister ||
            location.kind == AArch64AbiLocationKind::FloatingRegister) {
            const std::size_t frame_offset =
                allocate_call_register_capture_slot(machine_function,
                                                    prepared_argument.value->get_type());
            append_store_to_frame(machine_block, context,
                                  prepared_argument.value->get_type(),
                                  prepared_argument.value_reg, frame_offset,
                                  machine_function);
            prepared_argument.register_capture_frame_offset = frame_offset;
            continue;
        }
    }

    for (const PreparedCallArgument &prepared_argument : prepared_arguments) {
        const AArch64AbiAssignment &assignment = *prepared_argument.assignment;
        const AArch64AbiLocation &location = assignment.locations.front();
        if (location.kind != AArch64AbiLocationKind::GeneralRegister &&
            location.kind != AArch64AbiLocationKind::FloatingRegister) {
            continue;
        }
        if (prepared_argument.is_aggregate) {
            continue;
        }
        append_load_physical_register_argument_from_frame(
            machine_block, location.physical_reg, location.reg_kind,
            prepared_argument.value->get_type(),
            prepared_argument.register_capture_frame_offset);
    }

    if (!call.get_is_direct_call()) {
        AArch64VirtualReg callee_reg;
        if (!context.ensure_value_in_vreg(machine_block, call.get_callee_value(),
                                          callee_reg) ||
            !context.emit_indirect_call(machine_block, callee_reg)) {
            return false;
        }
    } else {
        context.emit_direct_call(machine_block, call.get_callee_name());
    }

    if (stack_arg_bytes > 0) {
        context.finish_stack_argument_area(machine_block, stack_arg_bytes);
    }

    if (is_void_type(call.get_type())) {
        return true;
    }

    if (is_aggregate_type(call.get_type())) {
        if (abi_info.return_value.is_indirect) {
            return true;
        }
        AArch64VirtualReg result_address;
        if (!context.materialize_canonical_memory_address(machine_block, &call,
                                                          result_address) ||
            !copy_aggregate_from_assignment_to_memory(machine_block,
                                                      abi_info.return_value,
                                                      call.get_type(),
                                                      result_address,
                                                      machine_function, context)) {
            return false;
        }
        return true;
    }

    AArch64VirtualReg result_reg;
    if (!context.require_canonical_vreg(&call, result_reg)) {
        return false;
    }
    const AArch64AbiLocation &return_location =
        abi_info.return_value.locations.front();
    context.append_copy_from_physical_reg(machine_block, result_reg,
                                          return_location.physical_reg,
                                          return_location.reg_kind);
    return true;
}

} // namespace sysycc
