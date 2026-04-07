#include "backend/asm_gen/aarch64/support/aarch64_frame_support.hpp"

#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

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

AArch64MachineOperand frame_memory_operand(std::size_t offset) {
    return AArch64MachineOperand::memory_address_physical_reg(
        static_cast<unsigned>(AArch64PhysicalReg::X29),
        -static_cast<long long>(offset));
}

AArch64MachineOperand memory_operand(unsigned address_reg) {
    return AArch64MachineOperand::memory_address_physical_reg(address_reg);
}

} // namespace

std::string load_mnemonic_for_kind(AArch64VirtualRegKind kind, std::size_t size) {
    if (AArch64VirtualReg(1, kind).is_floating_point()) {
        return "ldr";
    }
    const bool use_64bit = uses_general_64bit_register(kind);
    if (use_64bit || size == 8) {
        return "ldr";
    }
    if (size == 2) {
        return "ldrh";
    }
    if (size == 1) {
        return "ldrb";
    }
    return "ldr";
}

std::string store_mnemonic_for_kind(AArch64VirtualRegKind kind, std::size_t size) {
    if (AArch64VirtualReg(1, kind).is_floating_point()) {
        return "str";
    }
    const bool use_64bit = uses_general_64bit_register(kind);
    if (use_64bit || size == 8) {
        return "str";
    }
    if (size == 2) {
        return "strh";
    }
    if (size == 1) {
        return "strb";
    }
    return "str";
}

void append_materialize_physical_integer_constant(
    std::vector<AArch64MachineInstr> &instructions, unsigned physical_reg,
    std::uint64_t value) {
    const AArch64MachineOperand reg_operand =
        AArch64MachineOperand::physical_reg(physical_reg,
                                            AArch64VirtualRegKind::General64);
    if (value == 0) {
        instructions.emplace_back(
            AArch64MachineInstr("mov", {reg_operand, zero_register_operand(true)}));
        return;
    }

    for (unsigned piece = 0; piece < 4U; ++piece) {
        const std::uint16_t imm16 =
            static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
        if (piece == 0) {
            instructions.emplace_back(AArch64MachineInstr(
                "movz", {reg_operand,
                         AArch64MachineOperand::immediate("#" + std::to_string(imm16)),
                         shift_operand("lsl", piece * 16U)}));
            continue;
        }
        if (imm16 == 0) {
            continue;
        }
        instructions.emplace_back(AArch64MachineInstr(
            "movk", {reg_operand,
                     AArch64MachineOperand::immediate("#" + std::to_string(imm16)),
                     shift_operand("lsl", piece * 16U)}));
    }
}

void append_frame_address_into_physical_reg(
    std::vector<AArch64MachineInstr> &instructions, unsigned address_reg,
    std::size_t offset) {
    const AArch64MachineOperand reg_operand =
        AArch64MachineOperand::physical_reg(address_reg,
                                            AArch64VirtualRegKind::General64);
    if (offset <= 4095) {
        instructions.emplace_back(AArch64MachineInstr(
            "sub", {reg_operand,
                    AArch64MachineOperand::physical_reg(
                        static_cast<unsigned>(AArch64PhysicalReg::X29),
                        AArch64VirtualRegKind::General64),
                    AArch64MachineOperand::immediate("#" + std::to_string(offset))}));
        return;
    }
    append_materialize_physical_integer_constant(instructions, address_reg, offset);
    instructions.emplace_back(AArch64MachineInstr(
        "sub", {reg_operand,
                AArch64MachineOperand::physical_reg(
                    static_cast<unsigned>(AArch64PhysicalReg::X29),
                    AArch64VirtualRegKind::General64),
                reg_operand}));
}

void append_physical_frame_load(std::vector<AArch64MachineInstr> &instructions,
                                unsigned value_reg, AArch64VirtualRegKind kind,
                                std::size_t size, std::size_t offset,
                                unsigned address_temp_reg) {
    const std::string mnemonic = load_mnemonic_for_kind(kind, size);
    if (offset <= 255) {
        instructions.emplace_back(AArch64MachineInstr(
            mnemonic,
            {AArch64MachineOperand::physical_reg(value_reg, kind),
             frame_memory_operand(offset)}));
        return;
    }
    append_frame_address_into_physical_reg(instructions, address_temp_reg, offset);
    instructions.emplace_back(AArch64MachineInstr(
        mnemonic,
        {AArch64MachineOperand::physical_reg(value_reg, kind),
         memory_operand(address_temp_reg)}));
}

void append_physical_frame_store(std::vector<AArch64MachineInstr> &instructions,
                                 unsigned value_reg, AArch64VirtualRegKind kind,
                                 std::size_t size, std::size_t offset,
                                 unsigned address_temp_reg) {
    const std::string mnemonic = store_mnemonic_for_kind(kind, size);
    if (offset <= 255) {
        instructions.emplace_back(AArch64MachineInstr(
            mnemonic,
            {AArch64MachineOperand::physical_reg(value_reg, kind),
             frame_memory_operand(offset)}));
        return;
    }
    append_frame_address_into_physical_reg(instructions, address_temp_reg, offset);
    instructions.emplace_back(AArch64MachineInstr(
        mnemonic,
        {AArch64MachineOperand::physical_reg(value_reg, kind),
         memory_operand(address_temp_reg)}));
}

} // namespace sysycc
