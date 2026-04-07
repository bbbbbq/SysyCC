#include "backend/asm_gen/aarch64/support/aarch64_memory_instruction_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_memory_value_lowering_support.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

bool emit_load_instruction(AArch64MachineBlock &machine_block,
                           AArch64MemoryInstructionLoweringContext &context,
                           AArch64MemoryValueLoweringContext &memory_value_context,
                           const CoreIrLoadInst &load,
                           AArch64MachineFunction &function) {
    if (load.get_stack_slot() != nullptr &&
        context.is_promoted_stack_slot(load.get_stack_slot())) {
        AArch64VirtualReg value_reg;
        if (!context.require_canonical_vreg(&load, value_reg)) {
            return false;
        }
        const std::optional<AArch64VirtualReg> promoted_value =
            context.get_promoted_stack_slot_value(load.get_stack_slot());
        if (!promoted_value.has_value()) {
            context.report_error(
                "promoted stack slot loaded before it has a canonical value");
            return false;
        }
        if (promoted_value->get_id() != value_reg.get_id()) {
            context.append_register_copy(machine_block, value_reg, *promoted_value);
        }
        return true;
    }

    return emit_nonpromoted_load(machine_block, memory_value_context, load,
                                 function);
}

bool emit_store_instruction(AArch64MachineBlock &machine_block,
                            AArch64MemoryInstructionLoweringContext &context,
                            AArch64MemoryValueLoweringContext &memory_value_context,
                            const CoreIrStoreInst &store,
                            AArch64MachineFunction &function) {
    if (store.get_stack_slot() != nullptr &&
        context.is_promoted_stack_slot(store.get_stack_slot())) {
        AArch64VirtualReg value_reg;
        if (!context.ensure_value_in_vreg(machine_block, store.get_value(),
                                          value_reg)) {
            return false;
        }
        context.set_promoted_stack_slot_value(store.get_stack_slot(), value_reg);
        return true;
    }

    return emit_nonpromoted_store(machine_block, memory_value_context, store,
                                  function);
}

} // namespace sysycc
