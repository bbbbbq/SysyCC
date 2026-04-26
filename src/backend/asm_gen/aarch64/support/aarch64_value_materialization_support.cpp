#include "backend/asm_gen/aarch64/support/aarch64_value_materialization_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_address_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_function_shell_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

bool materialize_constant_address(AArch64MachineBlock &machine_block,
                                  AArch64ValueMaterializationContext &context,
                                  const CoreIrConstant *constant,
                                  const AArch64VirtualReg &target_reg,
                                  AArch64MachineFunction &function) {
    if (const auto *global_address =
            dynamic_cast<const CoreIrConstantGlobalAddress *>(constant);
        global_address != nullptr) {
        if (global_address->get_global() != nullptr) {
            return materialize_global_address(machine_block, context,
                                              global_address->get_global()->get_name(),
                                              target_reg, AArch64SymbolKind::Object);
        }
        if (global_address->get_function() != nullptr) {
            return materialize_global_address(machine_block, context,
                                              global_address->get_function()->get_name(),
                                              target_reg,
                                              AArch64SymbolKind::Function);
        }
        return false;
    }
    if (const auto *block_address =
            dynamic_cast<const CoreIrConstantBlockAddress *>(constant);
        block_address != nullptr) {
        return materialize_global_address(
            machine_block, context,
            make_aarch64_function_block_label(block_address->get_function_name(),
                                              block_address->get_block_name()),
            target_reg, AArch64SymbolKind::Label);
    }
    if (const auto *gep_constant =
            dynamic_cast<const CoreIrConstantGetElementPtr *>(constant);
        gep_constant != nullptr) {
        const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
            gep_constant->get_base()->get_type());
        if (base_pointer_type == nullptr) {
            context.report_error(
                "unsupported constant gep base in AArch64 native backend");
            return false;
        }
        return materialize_gep_value(
            machine_block, context, gep_constant->get_base(),
            base_pointer_type->get_pointee_type(), gep_constant->get_indices().size(),
            [&gep_constant](std::size_t index) -> CoreIrValue * {
                return const_cast<CoreIrConstant *>(gep_constant->get_indices()[index]);
            },
            target_reg, function);
    }
    if (const auto *cast_constant = dynamic_cast<const CoreIrConstantCast *>(constant);
        cast_constant != nullptr &&
        cast_constant->get_cast_kind() == CoreIrCastKind::IntToPtr) {
        if (const auto *int_constant =
                dynamic_cast<const CoreIrConstantInt *>(cast_constant->get_operand());
            int_constant != nullptr) {
            return materialize_integer_constant(machine_block, context,
                                                cast_constant->get_type(),
                                                int_constant->get_value(), target_reg);
        }
        if (const auto *nested_cast =
                dynamic_cast<const CoreIrConstantCast *>(cast_constant->get_operand());
            nested_cast != nullptr &&
            nested_cast->get_cast_kind() == CoreIrCastKind::PtrToInt) {
            return materialize_constant_address(machine_block, context,
                                                nested_cast->get_operand(), target_reg,
                                                function);
        }
    }
    return false;
}

bool materialize_constant_cast(AArch64MachineBlock &machine_block,
                               AArch64ValueMaterializationContext &context,
                               const CoreIrConstantCast &cast_constant,
                               const AArch64VirtualReg &target_reg,
                               AArch64MachineFunction &function) {
    switch (cast_constant.get_cast_kind()) {
    case CoreIrCastKind::IntToPtr:
        return materialize_constant_address(machine_block, context, &cast_constant,
                                            target_reg, function);
    case CoreIrCastKind::PtrToInt: {
        if (dynamic_cast<const CoreIrConstantNull *>(cast_constant.get_operand()) !=
                nullptr ||
            dynamic_cast<const CoreIrConstantZeroInitializer *>(
                cast_constant.get_operand()) != nullptr) {
            return materialize_integer_constant(machine_block, context,
                                                cast_constant.get_type(), 0,
                                                target_reg);
        }
        const auto *integer_type = as_integer_type(cast_constant.get_type());
        if (integer_type == nullptr || integer_type->get_bit_width() == 0 ||
            integer_type->get_bit_width() > 64) {
            context.report_error(
                "unsupported ptrtoint constant width in AArch64 native backend");
            return false;
        }
        if (integer_type->get_bit_width() == 64) {
            return materialize_constant_address(machine_block, context,
                                                cast_constant.get_operand(),
                                                target_reg, function);
        }
        const AArch64VirtualReg temp_address =
            function.create_virtual_reg(AArch64VirtualRegKind::General64);
        if (!materialize_constant_address(machine_block, context,
                                          cast_constant.get_operand(), temp_address,
                                          function)) {
            return false;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "mov", {def_vreg_operand_as(target_reg, false),
                    use_vreg_operand_as(temp_address, false)}));
        context.apply_truncate_to_virtual_reg(machine_block, target_reg,
                                              cast_constant.get_type());
        return true;
    }
    default:
        return false;
    }
}

bool materialize_zero_vector(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &target_reg) {
    machine_block.append_instruction(AArch64MachineInstr(
        "movi", {AArch64MachineOperand::def_vector_reg(target_reg, 2, 'd'),
                 AArch64MachineOperand::immediate("#0")}));
    return true;
}

bool materialize_i32x4_constant(AArch64MachineBlock &machine_block,
                                AArch64ValueMaterializationContext &context,
                                const CoreIrConstantAggregate &aggregate,
                                const AArch64VirtualReg &target_reg,
                                AArch64MachineFunction &function) {
    if (!is_i32x4_vector_type(aggregate.get_type())) {
        return false;
    }
    if (aggregate.get_elements().size() > 4) {
        context.report_error("invalid <4 x i32> aggregate constant in AArch64 native backend");
        return false;
    }
    if (!materialize_zero_vector(machine_block, target_reg)) {
        return false;
    }

    static CoreIrIntegerType i32_type(32);
    for (std::size_t lane = 0; lane < aggregate.get_elements().size(); ++lane) {
        const auto *constant_int =
            dynamic_cast<const CoreIrConstantInt *>(aggregate.get_elements()[lane]);
        if (constant_int == nullptr) {
            context.report_error(
                "unsupported non-integer <4 x i32> aggregate lane constant");
            return false;
        }
        if (constant_int->get_value() == 0) {
            continue;
        }
        const AArch64VirtualReg lane_reg =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        if (!materialize_integer_constant(machine_block, context, &i32_type,
                                          constant_int->get_value(), lane_reg)) {
            return false;
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "mov", {AArch64MachineOperand::def_vector_lane(
                        target_reg, 's', static_cast<unsigned>(lane)),
                    use_vreg_operand_as(lane_reg, false)}));
    }
    return true;
}

} // namespace

bool materialize_noncanonical_value(AArch64MachineBlock &machine_block,
                                    AArch64ValueMaterializationContext &context,
                                    const CoreIrValue *value,
                                    const AArch64VirtualReg &target_reg,
                                    AArch64MachineFunction &function) {
    if (value == nullptr) {
        context.report_error("encountered null Core IR value during AArch64 lowering");
        return false;
    }

    if (const auto *int_constant = dynamic_cast<const CoreIrConstantInt *>(value);
        int_constant != nullptr) {
        return materialize_integer_constant(machine_block, context, value->get_type(),
                                            int_constant->get_value(), target_reg);
    }
    if (const auto *float_constant = dynamic_cast<const CoreIrConstantFloat *>(value);
        float_constant != nullptr) {
        return materialize_float_constant(machine_block, context, *float_constant,
                                          target_reg, function);
    }
    if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr ||
        dynamic_cast<const CoreIrConstantZeroInitializer *>(value) != nullptr) {
        if (is_i32x4_vector_type(value->get_type())) {
            return materialize_zero_vector(machine_block, target_reg);
        }
        machine_block.append_instruction(AArch64MachineInstr(
            "mov",
            {def_vreg_operand(target_reg), zero_register_operand(target_reg.get_use_64bit())}));
        return true;
    }
    if (const auto *aggregate = dynamic_cast<const CoreIrConstantAggregate *>(value);
        aggregate != nullptr && is_i32x4_vector_type(aggregate->get_type())) {
        return materialize_i32x4_constant(machine_block, context, *aggregate,
                                          target_reg, function);
    }
    if (const auto *constant = dynamic_cast<const CoreIrConstant *>(value);
        constant != nullptr) {
        if (materialize_constant_address(machine_block, context, constant, target_reg,
                                         function)) {
            return true;
        }
    }
    if (const auto *cast_constant = dynamic_cast<const CoreIrConstantCast *>(value);
        cast_constant != nullptr) {
        if (materialize_constant_cast(machine_block, context, *cast_constant,
                                      target_reg, function)) {
            return true;
        }
    }
    if (const auto *address_of_stack_slot =
            dynamic_cast<const CoreIrAddressOfStackSlotInst *>(value);
        address_of_stack_slot != nullptr) {
        context.append_frame_address(machine_block, target_reg,
                                     context.get_stack_slot_offset(
                                         address_of_stack_slot->get_stack_slot()),
                                     function);
        return true;
    }
    if (const auto *address_of_global =
            dynamic_cast<const CoreIrAddressOfGlobalInst *>(value);
        address_of_global != nullptr) {
        return materialize_global_address(machine_block, context,
                                          address_of_global->get_global()->get_name(),
                                          target_reg, AArch64SymbolKind::Object);
    }
    if (const auto *address_of_function =
            dynamic_cast<const CoreIrAddressOfFunctionInst *>(value);
        address_of_function != nullptr) {
        return materialize_global_address(machine_block, context,
                                          address_of_function->get_function()->get_name(),
                                          target_reg, AArch64SymbolKind::Function);
    }
    if (const auto *gep_instruction = dynamic_cast<const CoreIrGetElementPtrInst *>(value);
        gep_instruction != nullptr) {
        const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
            gep_instruction->get_base()->get_type());
        if (base_pointer_type == nullptr) {
            context.report_error("unsupported gep base in AArch64 native backend");
            return false;
        }
        return materialize_gep_value(
            machine_block, context, gep_instruction->get_base(),
            base_pointer_type->get_pointee_type(), gep_instruction->get_index_count(),
            [&gep_instruction](std::size_t index) -> CoreIrValue * {
                return gep_instruction->get_index(index);
            },
            target_reg, function);
    }

    context.report_error("unsupported Core IR value in AArch64 native backend");
    return false;
}

} // namespace sysycc
