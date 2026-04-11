#include "backend/asm_gen/aarch64/support/aarch64_call_abi_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

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

    for (std::size_t index = call.get_argument_begin_index();
         index < arguments.size(); ++index) {
        CoreIrValue *argument = arguments[index];
        const std::size_t argument_index = index - call.get_argument_begin_index();
        const AArch64AbiAssignment &assignment = abi_info.parameters[argument_index];

        if (is_aggregate_type(argument->get_type())) {
            AArch64VirtualReg argument_address;
            if (!context.ensure_value_in_memory_address(machine_block, argument,
                                                        argument_address) ||
                !copy_aggregate_from_memory_to_assignment(
                    machine_block, assignment, argument->get_type(), argument_address,
                    machine_function, context, indirect_copy_offsets,
                    stack_base_address.has_value() ? &*stack_base_address : nullptr,
                    argument_index)) {
                return false;
            }
            continue;
        }

        AArch64VirtualReg arg_value;
        if (!context.ensure_value_in_vreg(machine_block, argument, arg_value)) {
            return false;
        }
        const AArch64AbiLocation &location = assignment.locations.front();
        if (location.kind == AArch64AbiLocationKind::GeneralRegister ||
            location.kind == AArch64AbiLocationKind::FloatingRegister) {
            context.append_copy_to_physical_reg(machine_block, location.physical_reg,
                                                location.reg_kind, arg_value);
            continue;
        }
        if (!stack_base_address.has_value() ||
            !context.append_store_to_address(machine_block, argument->get_type(),
                                             arg_value, *stack_base_address,
                                             location.stack_offset,
                                             machine_function)) {
            context.report_error(
                "missing outgoing stack base address for stack-passed AArch64 "
                "call argument");
            return false;
        }
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
