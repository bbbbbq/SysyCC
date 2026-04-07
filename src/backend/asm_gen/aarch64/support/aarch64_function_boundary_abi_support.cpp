#include "backend/asm_gen/aarch64/support/aarch64_function_boundary_abi_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

bool lower_function_entry_parameters(
    AArch64MachineBlock &prologue_block, const CoreIrFunction &function,
    const AArch64FunctionAbiInfo &abi_info, AArch64MachineFunction &machine_function,
    AArch64AbiEmissionContext &context) {
    for (std::size_t index = 0; index < function.get_parameters().size(); ++index) {
        const CoreIrParameter *parameter = function.get_parameters()[index].get();
        const AArch64AbiAssignment &assignment = abi_info.parameters[index];
        if (assignment.locations.empty()) {
            context.report_error("missing AArch64 ABI parameter assignment");
            return false;
        }
        if (is_aggregate_type(parameter->get_type())) {
            AArch64VirtualReg parameter_address;
            if (!context.materialize_canonical_memory_address(
                    prologue_block, parameter, parameter_address) ||
                !copy_aggregate_from_assignment_to_memory(
                    prologue_block, assignment, parameter->get_type(),
                    parameter_address, machine_function, context)) {
                return false;
            }
            continue;
        }

        AArch64VirtualReg parameter_reg;
        if (!context.require_canonical_vreg(parameter, parameter_reg)) {
            return false;
        }
        const AArch64AbiLocation &location = assignment.locations.front();
        if (location.kind == AArch64AbiLocationKind::GeneralRegister ||
            location.kind == AArch64AbiLocationKind::FloatingRegister) {
            context.append_copy_from_physical_reg(prologue_block, parameter_reg,
                                                  location.physical_reg,
                                                  location.reg_kind);
        } else {
            context.append_load_from_incoming_stack_arg(
                prologue_block, parameter->get_type(), parameter_reg,
                location.stack_offset, machine_function);
        }
        context.apply_truncate_to_virtual_reg(prologue_block, parameter_reg,
                                              parameter->get_type());
    }
    return true;
}

bool emit_function_return(AArch64MachineFunction &machine_function,
                          AArch64MachineBlock &machine_block,
                          const CoreIrReturnInst &return_inst,
                          const AArch64FunctionAbiInfo &abi_info,
                          const AArch64VirtualReg &indirect_result_address,
                          AArch64AbiEmissionContext &context) {
    if (return_inst.get_return_value() != nullptr) {
        if (is_aggregate_type(return_inst.get_return_value()->get_type())) {
            AArch64VirtualReg return_address;
            if (!context.ensure_value_in_memory_address(
                    machine_block, return_inst.get_return_value(), return_address)) {
                return false;
            }
            if (abi_info.return_value.is_indirect) {
                if (!indirect_result_address.is_valid() ||
                    !context.emit_memory_copy(
                        machine_block, indirect_result_address, return_address,
                        return_inst.get_return_value()->get_type(),
                        machine_function)) {
                    return false;
                }
            } else if (!copy_aggregate_from_memory_to_assignment(
                           machine_block, abi_info.return_value,
                           return_inst.get_return_value()->get_type(), return_address,
                           machine_function, context)) {
                return false;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "b", {AArch64MachineOperand::label(
                         machine_function.get_epilogue_label())}));
            return true;
        }

        AArch64VirtualReg return_reg;
        if (!context.ensure_value_in_vreg(machine_block,
                                          return_inst.get_return_value(),
                                          return_reg)) {
            return false;
        }
        if (abi_info.return_value.locations.size() != 1) {
            context.report_error(
                "aggregate function return ABI lowering is not wired yet in this "
                "native backend path");
            return false;
        }
        const AArch64AbiLocation &return_location =
            abi_info.return_value.locations.front();
        context.append_copy_to_physical_reg(machine_block,
                                            return_location.physical_reg,
                                            return_location.reg_kind,
                                            return_reg);
    }
    machine_block.append_instruction(AArch64MachineInstr(
        "b",
        {AArch64MachineOperand::label(machine_function.get_epilogue_label())}));
    return true;
}

} // namespace sysycc
