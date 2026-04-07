#include "backend/asm_gen/aarch64/support/aarch64_address_value_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_address_materialization_support.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

bool emit_address_of_stack_slot_value(
    AArch64MachineBlock &machine_block,
    AArch64ValueMaterializationContext &context,
    const CoreIrAddressOfStackSlotInst &address_of_stack_slot,
    const AArch64VirtualReg &target_reg, AArch64MachineFunction &function) {
    context.append_frame_address(
        machine_block, target_reg,
        context.get_stack_slot_offset(address_of_stack_slot.get_stack_slot()),
        function);
    return true;
}

bool emit_address_of_global_value(AArch64MachineBlock &machine_block,
                                  AArch64ValueMaterializationContext &context,
                                  const CoreIrAddressOfGlobalInst &address_of_global,
                                  const AArch64VirtualReg &target_reg) {
    return materialize_global_address(machine_block, context,
                                      address_of_global.get_global()->get_name(),
                                      target_reg);
}

bool emit_address_of_function_value(
    AArch64MachineBlock &machine_block,
    AArch64ValueMaterializationContext &context,
    const CoreIrAddressOfFunctionInst &address_of_function,
    const AArch64VirtualReg &target_reg) {
    return materialize_global_address(machine_block, context,
                                      address_of_function.get_function()->get_name(),
                                      target_reg, AArch64SymbolKind::Function);
}

bool emit_getelementptr_value(AArch64MachineBlock &machine_block,
                              AArch64ValueMaterializationContext &context,
                              const CoreIrGetElementPtrInst &gep,
                              const AArch64VirtualReg &target_reg,
                              AArch64MachineFunction &function) {
    const auto *base_pointer_type =
        dynamic_cast<const CoreIrPointerType *>(gep.get_base()->get_type());
    if (base_pointer_type == nullptr) {
        context.report_error("unsupported gep base in AArch64 native backend");
        return false;
    }
    return materialize_gep_value(
        machine_block, context, gep.get_base(),
        base_pointer_type->get_pointee_type(), gep.get_index_count(),
        [&gep](std::size_t index) -> CoreIrValue * { return gep.get_index(index); },
        target_reg, function);
}

} // namespace sysycc
