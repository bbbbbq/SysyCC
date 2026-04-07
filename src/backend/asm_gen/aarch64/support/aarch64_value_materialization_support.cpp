#include "backend/asm_gen/aarch64/support/aarch64_value_materialization_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_address_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_constant_materialization_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

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
        machine_block.append_instruction(AArch64MachineInstr(
            "mov",
            {def_vreg_operand(target_reg), zero_register_operand(target_reg.get_use_64bit())}));
        return true;
    }
    if (const auto *global_address =
            dynamic_cast<const CoreIrConstantGlobalAddress *>(value);
        global_address != nullptr) {
        return materialize_global_address(machine_block, context,
                                          global_address->get_global()->get_name(),
                                          target_reg, AArch64SymbolKind::Object);
    }
    if (const auto *gep_constant = dynamic_cast<const CoreIrConstantGetElementPtr *>(value);
        gep_constant != nullptr) {
        const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
            gep_constant->get_base()->get_type());
        if (base_pointer_type == nullptr) {
            context.report_error("unsupported constant gep base in AArch64 native backend");
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
