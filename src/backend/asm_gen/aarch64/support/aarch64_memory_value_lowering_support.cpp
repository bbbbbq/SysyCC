#include "backend/asm_gen/aarch64/support/aarch64_memory_value_lowering_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_memory_access_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"

namespace sysycc {

bool emit_nonpromoted_load(AArch64MachineBlock &machine_block,
                           AArch64MemoryValueLoweringContext &context,
                           const CoreIrLoadInst &load,
                           AArch64MachineFunction &function) {
    if (is_aggregate_type(load.get_type())) {
        AArch64VirtualReg destination_address;
        if (!context.materialize_canonical_memory_address(machine_block, &load,
                                                          destination_address)) {
            return false;
        }
        AArch64VirtualReg source_address = context.create_pointer_virtual_reg(function);
        if (load.get_stack_slot() != nullptr) {
            context.append_frame_address(machine_block, source_address,
                                         context.get_stack_slot_offset(
                                             load.get_stack_slot()),
                                         function);
        } else if (!context.ensure_value_in_vreg(machine_block, load.get_address(),
                                                 source_address)) {
            return false;
        }
        return emit_memory_copy(machine_block, context, destination_address,
                                source_address, load.get_type(), function);
    }

    AArch64VirtualReg value_reg;
    if (!context.require_canonical_vreg(&load, value_reg)) {
        return false;
    }
    if (load.get_stack_slot() != nullptr) {
        append_load_from_frame(machine_block, context, load.get_type(), value_reg,
                               context.get_stack_slot_offset(load.get_stack_slot()),
                               function);
        return true;
    }
    AArch64VirtualReg address_reg;
    if (!context.ensure_value_in_vreg(machine_block, load.get_address(), address_reg)) {
        return false;
    }
    return append_load_from_address(machine_block, context, load.get_type(), value_reg,
                                    address_reg, 0, function);
}

bool emit_nonpromoted_store(AArch64MachineBlock &machine_block,
                            AArch64MemoryValueLoweringContext &context,
                            const CoreIrStoreInst &store,
                            AArch64MachineFunction &function) {
    if (const auto *zero_initializer =
            dynamic_cast<const CoreIrConstantZeroInitializer *>(store.get_value());
        zero_initializer != nullptr) {
        AArch64VirtualReg address_reg;
        if (store.get_stack_slot() != nullptr) {
            address_reg = context.create_pointer_virtual_reg(function);
            context.append_frame_address(machine_block, address_reg,
                                         context.get_stack_slot_offset(
                                             store.get_stack_slot()),
                                         function);
        } else if (!context.ensure_value_in_vreg(machine_block, store.get_address(),
                                                 address_reg)) {
            return false;
        }
        return emit_zero_fill(machine_block, context, address_reg,
                              zero_initializer->get_type(), function);
    }

    if (is_aggregate_type(store.get_value()->get_type())) {
        AArch64VirtualReg source_address;
        if (!context.ensure_value_in_memory_address(machine_block, store.get_value(),
                                                    source_address)) {
            return false;
        }
        AArch64VirtualReg destination_address =
            context.create_pointer_virtual_reg(function);
        if (store.get_stack_slot() != nullptr) {
            context.append_frame_address(machine_block, destination_address,
                                         context.get_stack_slot_offset(
                                             store.get_stack_slot()),
                                         function);
        } else if (!context.ensure_value_in_vreg(machine_block, store.get_address(),
                                                 destination_address)) {
            return false;
        }
        return emit_memory_copy(machine_block, context, destination_address,
                                source_address, store.get_value()->get_type(),
                                function);
    }

    AArch64VirtualReg value_reg;
    if (!context.ensure_value_in_vreg(machine_block, store.get_value(), value_reg)) {
        return false;
    }
    if (store.get_stack_slot() != nullptr) {
        append_store_to_frame(machine_block, context, store.get_value()->get_type(),
                              value_reg,
                              context.get_stack_slot_offset(store.get_stack_slot()),
                              function);
        return true;
    }
    AArch64VirtualReg address_reg;
    if (!context.ensure_value_in_vreg(machine_block, store.get_address(), address_reg)) {
        return false;
    }
    return append_store_to_address(machine_block, context,
                                   store.get_value()->get_type(), value_reg,
                                   address_reg, 0, function);
}

} // namespace sysycc
