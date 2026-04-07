#include "backend/asm_gen/aarch64/support/aarch64_memory_access_support.hpp"

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

AArch64MachineOperand memory_operand(const AArch64VirtualReg &base_reg) {
    return AArch64MachineOperand::memory_address_virtual_reg(base_reg);
}

AArch64MachineOperand memory_operand(const AArch64VirtualReg &base_reg,
                                     long long immediate_offset) {
    return AArch64MachineOperand::memory_address_virtual_reg(base_reg,
                                                             immediate_offset);
}

AArch64MachineOperand frame_memory_operand(std::size_t offset) {
    return AArch64MachineOperand::memory_address_physical_reg(
        static_cast<unsigned>(AArch64PhysicalReg::X29),
        -static_cast<long long>(offset));
}

AArch64MachineOperand incoming_stack_memory_operand(std::size_t offset) {
    return AArch64MachineOperand::memory_address_physical_reg(
        static_cast<unsigned>(AArch64PhysicalReg::X29),
        static_cast<long long>(offset));
}

} // namespace

std::string load_mnemonic_for_type(const CoreIrType *type) {
    if (is_float_type(type)) {
        return "ldr";
    }
    if (is_pointer_type(type) || get_storage_size(type) == 8) {
        return "ldr";
    }
    if (get_storage_size(type) == 2) {
        return "ldrh";
    }
    if (get_storage_size(type) == 1) {
        return "ldrb";
    }
    return "ldr";
}

std::string store_mnemonic_for_type(const CoreIrType *type) {
    if (is_float_type(type)) {
        return "str";
    }
    if (is_pointer_type(type) || get_storage_size(type) == 8) {
        return "str";
    }
    if (get_storage_size(type) == 2) {
        return "strh";
    }
    if (get_storage_size(type) == 1) {
        return "strb";
    }
    return "str";
}

bool append_memory_store(AArch64MachineBlock &machine_block,
                         AArch64MemoryAccessContext &context,
                         const CoreIrType *type,
                         const AArch64MachineOperand &source_operand,
                         const AArch64VirtualReg &address_reg, std::size_t offset,
                         AArch64MachineFunction &function) {
    if (offset <= 4095) {
        machine_block.append_instruction(AArch64MachineInstr(
            store_mnemonic_for_type(type),
            {source_operand, memory_operand(address_reg,
                                            static_cast<long long>(offset))}));
        return true;
    }
    const AArch64VirtualReg offset_address_reg =
        context.create_pointer_virtual_reg(function);
    append_register_copy(machine_block, offset_address_reg, address_reg);
    if (!context.add_constant_offset(machine_block, offset_address_reg,
                                     static_cast<long long>(offset), function)) {
        return false;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        store_mnemonic_for_type(type),
        {source_operand, memory_operand(offset_address_reg)}));
    return true;
}

bool emit_zero_fill(AArch64MachineBlock &machine_block,
                    AArch64MemoryAccessContext &context,
                    const AArch64VirtualReg &address_reg, const CoreIrType *type,
                    AArch64MachineFunction &function) {
    std::size_t remaining = get_type_size(type);
    std::size_t offset = 0;
    while (remaining >= 8) {
        if (!append_memory_store(machine_block, context,
                                 context.create_fake_pointer_type(),
                                 zero_register_operand(true),
                                 address_reg, offset, function)) {
            return false;
        }
        offset += 8;
        remaining -= 8;
    }
    if (remaining >= 4) {
        static CoreIrIntegerType i32_type(32);
        if (!append_memory_store(machine_block, context, &i32_type,
                                 zero_register_operand(false),
                                 address_reg, offset, function)) {
            return false;
        }
        offset += 4;
        remaining -= 4;
    }
    if (remaining >= 2) {
        static CoreIrIntegerType i16_type(16);
        if (!append_memory_store(machine_block, context, &i16_type,
                                 zero_register_operand(false),
                                 address_reg, offset, function)) {
            return false;
        }
        offset += 2;
        remaining -= 2;
    }
    if (remaining >= 1) {
        static CoreIrIntegerType i8_type(8);
        if (!append_memory_store(machine_block, context, &i8_type,
                                 zero_register_operand(false),
                                 address_reg, offset, function)) {
            return false;
        }
    }
    return true;
}

void append_load_from_frame(AArch64MachineBlock &machine_block,
                            AArch64MemoryAccessContext &context,
                            const CoreIrType *type,
                            const AArch64VirtualReg &target_reg, std::size_t offset,
                            AArch64MachineFunction &function) {
    std::string mnemonic = "ldur";
    if (!is_float_type(type) && get_storage_size(type) == 2) {
        mnemonic = "ldurh";
    } else if (!is_float_type(type) && get_storage_size(type) == 1) {
        mnemonic = "ldurb";
    }
    if (offset <= 255) {
        machine_block.append_instruction(
            AArch64MachineInstr(mnemonic,
                                {def_vreg_operand(target_reg),
                                 frame_memory_operand(offset)}));
        return;
    }
    const AArch64VirtualReg address_reg = context.create_pointer_virtual_reg(function);
    context.append_frame_address(machine_block, address_reg, offset, function);
    machine_block.append_instruction(
        AArch64MachineInstr(load_mnemonic_for_type(type),
                            {def_vreg_operand(target_reg), memory_operand(address_reg)}));
}

void append_store_to_frame(AArch64MachineBlock &machine_block,
                           AArch64MemoryAccessContext &context,
                           const CoreIrType *type,
                           const AArch64VirtualReg &source_reg, std::size_t offset,
                           AArch64MachineFunction &function) {
    std::string mnemonic = "stur";
    if (!is_float_type(type) && get_storage_size(type) == 2) {
        mnemonic = "sturh";
    } else if (!is_float_type(type) && get_storage_size(type) == 1) {
        mnemonic = "sturb";
    }
    if (offset <= 255) {
        machine_block.append_instruction(
            AArch64MachineInstr(mnemonic,
                                {use_vreg_operand(source_reg),
                                 frame_memory_operand(offset)}));
        return;
    }
    const AArch64VirtualReg address_reg = context.create_pointer_virtual_reg(function);
    context.append_frame_address(machine_block, address_reg, offset, function);
    machine_block.append_instruction(
        AArch64MachineInstr(store_mnemonic_for_type(type),
                            {use_vreg_operand(source_reg), memory_operand(address_reg)}));
}

void append_load_from_incoming_stack_arg(AArch64MachineBlock &machine_block,
                                         AArch64MemoryAccessContext &context,
                                         const CoreIrType *type,
                                         const AArch64VirtualReg &target_reg,
                                         std::size_t offset,
                                         AArch64MachineFunction &function) {
    if (offset <= 4095) {
        machine_block.append_instruction(
            AArch64MachineInstr(load_mnemonic_for_type(type),
                                {def_vreg_operand(target_reg),
                                 incoming_stack_memory_operand(offset)}));
        return;
    }
    const AArch64VirtualReg address_reg = context.create_pointer_virtual_reg(function);
    machine_block.append_instruction(AArch64MachineInstr(
        "mov", {def_vreg_operand(address_reg),
                AArch64MachineOperand::physical_reg(
                    static_cast<unsigned>(AArch64PhysicalReg::X29),
                    AArch64VirtualRegKind::General64)}));
    context.add_constant_offset(machine_block, address_reg,
                                static_cast<long long>(offset), function);
    machine_block.append_instruction(
        AArch64MachineInstr(load_mnemonic_for_type(type),
                            {def_vreg_operand(target_reg), memory_operand(address_reg)}));
}

bool append_load_from_address(AArch64MachineBlock &machine_block,
                              AArch64MemoryAccessContext &context,
                              const CoreIrType *type,
                              const AArch64VirtualReg &target_reg,
                              const AArch64VirtualReg &address_reg,
                              std::size_t offset,
                              AArch64MachineFunction &function) {
    if (offset <= 4095) {
        machine_block.append_instruction(AArch64MachineInstr(
            load_mnemonic_for_type(type),
            {def_vreg_operand(target_reg),
             memory_operand(address_reg, static_cast<long long>(offset))}));
        return true;
    }
    const AArch64VirtualReg offset_address_reg =
        context.create_pointer_virtual_reg(function);
    append_register_copy(machine_block, offset_address_reg, address_reg);
    if (!context.add_constant_offset(machine_block, offset_address_reg,
                                     static_cast<long long>(offset), function)) {
        return false;
    }
    machine_block.append_instruction(AArch64MachineInstr(
        load_mnemonic_for_type(type),
        {def_vreg_operand(target_reg), memory_operand(offset_address_reg)}));
    return true;
}

bool append_store_to_address(AArch64MachineBlock &machine_block,
                             AArch64MemoryAccessContext &context,
                             const CoreIrType *type,
                             const AArch64VirtualReg &source_reg,
                             const AArch64VirtualReg &address_reg,
                             std::size_t offset,
                             AArch64MachineFunction &function) {
    return append_memory_store(machine_block, context, type,
                               use_vreg_operand(source_reg),
                               address_reg, offset, function);
}

bool emit_memory_copy(AArch64MachineBlock &machine_block,
                      AArch64MemoryAccessContext &context,
                      const AArch64VirtualReg &destination_address,
                      const AArch64VirtualReg &source_address,
                      const CoreIrType *type, AArch64MachineFunction &function) {
    std::size_t remaining = get_type_size(type);
    std::size_t offset = 0;
    while (remaining >= 8) {
        const AArch64VirtualReg temp =
            function.create_virtual_reg(AArch64VirtualRegKind::General64);
        if (!append_load_from_address(machine_block, context,
                                      context.create_fake_pointer_type(), temp,
                                      source_address, offset, function) ||
            !append_store_to_address(machine_block, context,
                                     context.create_fake_pointer_type(), temp,
                                     destination_address, offset, function)) {
            return false;
        }
        remaining -= 8;
        offset += 8;
    }
    if (remaining >= 4) {
        static CoreIrIntegerType i32_type(32);
        const AArch64VirtualReg temp =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        if (!append_load_from_address(machine_block, context, &i32_type, temp,
                                      source_address, offset, function) ||
            !append_store_to_address(machine_block, context, &i32_type, temp,
                                     destination_address, offset, function)) {
            return false;
        }
        remaining -= 4;
        offset += 4;
    }
    if (remaining >= 2) {
        static CoreIrIntegerType i16_type(16);
        const AArch64VirtualReg temp =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        if (!append_load_from_address(machine_block, context, &i16_type, temp,
                                      source_address, offset, function) ||
            !append_store_to_address(machine_block, context, &i16_type, temp,
                                     destination_address, offset, function)) {
            return false;
        }
        remaining -= 2;
        offset += 2;
    }
    if (remaining >= 1) {
        static CoreIrIntegerType i8_type(8);
        const AArch64VirtualReg temp =
            function.create_virtual_reg(AArch64VirtualRegKind::General32);
        if (!append_load_from_address(machine_block, context, &i8_type, temp,
                                      source_address, offset, function) ||
            !append_store_to_address(machine_block, context, &i8_type, temp,
                                     destination_address, offset, function)) {
            return false;
        }
    }
    return true;
}

} // namespace sysycc
