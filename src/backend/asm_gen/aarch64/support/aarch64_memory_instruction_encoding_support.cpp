#include "backend/asm_gen/aarch64/support/aarch64_memory_instruction_encoding_support.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include "backend/asm_gen/aarch64/support/aarch64_fp_instruction_encoding_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

std::optional<EncodedGeneralReg>
resolve_memory_base_reg(const AArch64MachineMemoryAddressOperand &memory,
                        const AArch64MachineFunction &function,
                        DiagnosticEngine &diagnostic_engine,
                        const std::string &context) {
    switch (memory.base_kind) {
    case AArch64MachineMemoryAddressOperand::BaseKind::StackPointer:
        return EncodedGeneralReg{31, true, true, false};
    case AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg:
        if (is_float_physical_reg(memory.physical_reg)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: floating-point register cannot be used as memory base in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{memory.physical_reg, true, false, false};
    case AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg: {
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(memory.virtual_reg.get_id());
        if (!physical_reg.has_value()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unresolved memory base register in " +
                    context);
            return std::nullopt;
        }
        if (is_float_physical_reg(*physical_reg)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: floating-point register cannot be used as memory base in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{*physical_reg, true, false, false};
    }
    }
    return std::nullopt;
}

bool operand_is_float_reg_like(const AArch64MachineOperand &operand) {
    if (const auto *physical_reg = operand.get_physical_reg_operand();
        physical_reg != nullptr) {
        return is_float_physical_reg(physical_reg->reg_number);
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        return virtual_reg->reg.is_floating_point();
    }
    return false;
}

bool check_signed_range(long long value, long long min_value, long long max_value,
                        DiagnosticEngine &diagnostic_engine,
                        const std::string &message) {
    if (value < min_value || value > max_value) {
        diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
        return false;
    }
    return true;
}

std::uint32_t encode_pair_word(std::uint32_t base, unsigned rt, unsigned rt2,
                               unsigned rn, long long scaled_imm7) {
    return base | ((static_cast<std::uint32_t>(scaled_imm7) & 0x7fU) << 15) |
           ((rt2 & 0x1fU) << 10) | ((rn & 0x1fU) << 5) | (rt & 0x1fU);
}

std::uint32_t encode_load_store_unsigned_word(std::uint32_t base, unsigned rt,
                                              unsigned rn, unsigned imm12) {
    return base | ((imm12 & 0xfffU) << 10) | ((rn & 0x1fU) << 5) |
           (rt & 0x1fU);
}

std::uint32_t encode_load_store_unscaled_word(std::uint32_t base, unsigned rt,
                                              unsigned rn, long long imm9) {
    return base | ((static_cast<std::uint32_t>(imm9) & 0x1ffU) << 12) |
           ((rn & 0x1fU) << 5) | (rt & 0x1fU);
}

std::uint32_t encode_load_store_register_offset_word(std::uint32_t base,
                                                     unsigned rt, unsigned rn,
                                                     unsigned rm,
                                                     bool scaled) {
    return base | ((rm & 0x1fU) << 16) |
           (static_cast<std::uint32_t>(scaled ? 1U : 0U) << 12) |
           ((rn & 0x1fU) << 5) | (rt & 0x1fU);
}

} // namespace

std::optional<EncodedInstruction> encode_memory_family_instruction(
    const AArch64MachineInstr &instruction, const AArch64MachineFunction &function,
    const FunctionScanInfo &scan_info, std::size_t pc_offset,
    DiagnosticEngine &diagnostic_engine,
    const AArch64MemoryInstructionEncodingContext &context) {
    (void)scan_info;
    EncodedInstruction encoded;
    const auto unsupported = [&](const std::string &detail)
        -> std::optional<EncodedInstruction> {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 direct object writer: unsupported instruction '" +
                instruction.get_mnemonic() + "' (" + detail + ")");
        return std::nullopt;
    };

    const AArch64MachineOpcode opcode = instruction.get_opcode();
    const std::string &mnemonic = instruction.get_mnemonic();
    const auto &operands = instruction.get_operands();

    if (opcode == AArch64MachineOpcode::StorePair ||
        opcode == AArch64MachineOpcode::LoadPair) {
        if (operands.size() != 3) {
            return unsupported("pair load/store operand shape");
        }
        const auto rt =
            context.resolve_general_reg_operand == nullptr
                ? std::optional<EncodedGeneralReg>{}
                : context.resolve_general_reg_operand(
                      operands[0], function, false, false, diagnostic_engine,
                      mnemonic + " rt");
        const auto rt2 =
            context.resolve_general_reg_operand == nullptr
                ? std::optional<EncodedGeneralReg>{}
                : context.resolve_general_reg_operand(
                      operands[1], function, false, false, diagnostic_engine,
                      mnemonic + " rt2");
        const auto *memory = operands[2].get_memory_address_operand();
        if (!rt.has_value() || !rt2.has_value() || !rt->use_64bit ||
            !rt2->use_64bit || memory == nullptr) {
            return std::nullopt;
        }
        if (memory->get_symbolic_offset() != nullptr) {
            return unsupported("symbolic pair load/store");
        }
        const auto base = resolve_memory_base_reg(*memory, function,
                                                  diagnostic_engine,
                                                  mnemonic + " base");
        if (!base.has_value()) {
            return std::nullopt;
        }
        const long long offset = memory->get_immediate_offset().value_or(0);
        if ((offset % 8) != 0 ||
            !check_signed_range(offset / 8, -64, 63, diagnostic_engine,
                                "AArch64 direct object writer: pair load/store offset out of range")) {
            return std::nullopt;
        }
        std::uint32_t base_word =
            opcode == AArch64MachineOpcode::StorePair ? 0xA9000000U : 0xA9400000U;
        if (memory->address_mode ==
            AArch64MachineMemoryAddressOperand::AddressMode::PreIndex) {
            base_word = opcode == AArch64MachineOpcode::StorePair ? 0xA9800000U
                                                                  : 0xA9C00000U;
        } else if (memory->address_mode ==
                   AArch64MachineMemoryAddressOperand::AddressMode::PostIndex) {
            base_word = opcode == AArch64MachineOpcode::StorePair ? 0xA8800000U
                                                                  : 0xA8C00000U;
        }
        encoded.word = encode_pair_word(base_word, rt->code, rt2->code, base->code,
                                        offset / 8);
        return encoded;
    }

    if (opcode != AArch64MachineOpcode::Load &&
        opcode != AArch64MachineOpcode::Store &&
        opcode != AArch64MachineOpcode::LoadByte &&
        opcode != AArch64MachineOpcode::StoreByte &&
        opcode != AArch64MachineOpcode::LoadHalf &&
        opcode != AArch64MachineOpcode::StoreHalf &&
        opcode != AArch64MachineOpcode::LoadUnscaled &&
        opcode != AArch64MachineOpcode::StoreUnscaled &&
        opcode != AArch64MachineOpcode::LoadByteUnscaled &&
        opcode != AArch64MachineOpcode::StoreByteUnscaled &&
        opcode != AArch64MachineOpcode::LoadHalfUnscaled &&
        opcode != AArch64MachineOpcode::StoreHalfUnscaled) {
        return std::nullopt;
    }

    if (operands.size() != 2) {
        return unsupported("load/store operand shape");
    }
    const bool is_load =
        opcode == AArch64MachineOpcode::Load ||
        opcode == AArch64MachineOpcode::LoadByte ||
        opcode == AArch64MachineOpcode::LoadHalf ||
        opcode == AArch64MachineOpcode::LoadUnscaled ||
        opcode == AArch64MachineOpcode::LoadByteUnscaled ||
        opcode == AArch64MachineOpcode::LoadHalfUnscaled;
    const AArch64MachineOperand &value_operand = operands[0];
    const auto *memory = operands[1].get_memory_address_operand();
    if (memory == nullptr) {
        return unsupported("load/store memory operand");
    }

    std::optional<EncodedGeneralReg> general_value;
    std::optional<EncodedFloatReg> float_value;
    bool is_float = false;
    if (operand_is_float_reg_like(value_operand)) {
        float_value =
            context.resolve_float_reg_operand == nullptr
                ? std::optional<EncodedFloatReg>{}
                : context.resolve_float_reg_operand(value_operand, function,
                                                    diagnostic_engine, mnemonic);
        is_float = float_value.has_value();
    } else {
        general_value =
            context.resolve_general_reg_operand == nullptr
                ? std::optional<EncodedGeneralReg>{}
                : context.resolve_general_reg_operand(
                      value_operand, function, false, false, diagnostic_engine,
                      mnemonic + " value");
    }
    if (!is_float && !general_value.has_value()) {
        return std::nullopt;
    }

    unsigned rt = is_float ? float_value->code : general_value->code;
    const bool use_64bit = is_float ? true : general_value->use_64bit;
    unsigned access_size = 4;
    std::uint32_t unsigned_base = 0;
    std::uint32_t unscaled_base = 0;
    if (opcode == AArch64MachineOpcode::LoadByte ||
        opcode == AArch64MachineOpcode::StoreByte ||
        opcode == AArch64MachineOpcode::LoadByteUnscaled ||
        opcode == AArch64MachineOpcode::StoreByteUnscaled) {
        access_size = 1;
        unsigned_base = is_load ? 0x39400000U : 0x39000000U;
        unscaled_base = is_load ? 0x38400000U : 0x38000000U;
    } else if (opcode == AArch64MachineOpcode::LoadHalf ||
               opcode == AArch64MachineOpcode::StoreHalf ||
               opcode == AArch64MachineOpcode::LoadHalfUnscaled ||
               opcode == AArch64MachineOpcode::StoreHalfUnscaled) {
        access_size = 2;
        unsigned_base = is_load ? 0x79400000U : 0x79000000U;
        unscaled_base = is_load ? 0x78400000U : 0x78000000U;
    } else if (is_float) {
        if (float_value->kind == AArch64VirtualRegKind::Float128) {
            access_size = 16;
            unsigned_base = is_load ? 0x3DC00000U : 0x3D800000U;
            unscaled_base = is_load ? 0x3CC00000U : 0x3C800000U;
        } else if (float_value->kind == AArch64VirtualRegKind::Float16) {
            access_size = static_cast<unsigned>(scalar_fp_size(float_value->kind));
            unsigned_base = is_load ? 0x7D400000U : 0x7D000000U;
            unscaled_base = is_load ? 0x7C400000U : 0x7C000000U;
        } else if (float_value->kind == AArch64VirtualRegKind::Float32) {
            access_size = static_cast<unsigned>(scalar_fp_size(float_value->kind));
            unsigned_base = is_load ? 0xBD400000U : 0xBD000000U;
            unscaled_base = is_load ? 0xBC400000U : 0xBC000000U;
        } else if (float_value->kind == AArch64VirtualRegKind::Float64) {
            access_size = static_cast<unsigned>(scalar_fp_size(float_value->kind));
            unsigned_base = is_load ? 0xFD400000U : 0xFD000000U;
            unscaled_base = is_load ? 0xFC400000U : 0xFC000000U;
        } else {
            return unsupported("floating load/store kind");
        }
    } else if (use_64bit) {
        access_size = 8;
        unsigned_base = is_load ? 0xF9400000U : 0xF9000000U;
        unscaled_base = is_load ? 0xF8400000U : 0xF8000000U;
    } else {
        access_size = 4;
        unsigned_base = is_load ? 0xB9400000U : 0xB9000000U;
        unscaled_base = is_load ? 0xB8400000U : 0xB8000000U;
    }

    const auto base = resolve_memory_base_reg(*memory, function, diagnostic_engine,
                                              mnemonic + " base");
    if (!base.has_value()) {
        return std::nullopt;
    }
    const unsigned rn = base->code;
    if (const auto *symbolic = memory->get_symbolic_offset(); symbolic != nullptr) {
        if (!is_load || !use_64bit ||
            symbolic->modifier != AArch64MachineSymbolReference::Modifier::GotLo12) {
            return unsupported("symbolic memory offset");
        }
        encoded.word = encode_load_store_unsigned_word(unsigned_base, rt, rn, 0);
        encoded.relocations.push_back(AArch64RelocationRecord{
            AArch64RelocationKind::GotLo12, symbolic->target, pc_offset});
        return encoded;
    }
    if (const auto *register_offset = memory->get_register_offset();
        register_offset != nullptr) {
        if (is_float || register_offset->kind != AArch64VirtualRegKind::General64 ||
            memory->address_mode !=
                AArch64MachineMemoryAddressOperand::AddressMode::Offset) {
            return unsupported("register-offset load/store");
        }
        if (is_float_physical_reg(register_offset->reg_number)) {
            return unsupported("floating-point register offset");
        }
        unsigned required_scaled_shift = 0;
        if (access_size == 2) {
            required_scaled_shift = 1;
        } else if (access_size == 4) {
            required_scaled_shift = 2;
        } else if (access_size == 8) {
            required_scaled_shift = 3;
        }
        const bool scaled = register_offset->shift_amount != 0;
        if (register_offset->shift_kind != AArch64ShiftKind::Lsl ||
            (register_offset->shift_amount != 0 &&
             register_offset->shift_amount != required_scaled_shift)) {
            return unsupported("register-offset shift");
        }

        std::uint32_t register_offset_base = 0;
        if (opcode == AArch64MachineOpcode::LoadByte) {
            register_offset_base = 0x38606800U;
        } else if (opcode == AArch64MachineOpcode::StoreByte) {
            register_offset_base = 0x38206800U;
        } else if (opcode == AArch64MachineOpcode::LoadHalf) {
            register_offset_base = 0x78606800U;
        } else if (opcode == AArch64MachineOpcode::StoreHalf) {
            register_offset_base = 0x78206800U;
        } else if (opcode == AArch64MachineOpcode::Load && use_64bit) {
            register_offset_base = 0xF8606800U;
        } else if (opcode == AArch64MachineOpcode::Store && use_64bit) {
            register_offset_base = 0xF8206800U;
        } else if (opcode == AArch64MachineOpcode::Load) {
            register_offset_base = 0xB8606800U;
        } else if (opcode == AArch64MachineOpcode::Store) {
            register_offset_base = 0xB8206800U;
        } else {
            return unsupported("register-offset opcode");
        }
        encoded.word = encode_load_store_register_offset_word(
            register_offset_base, rt, rn, register_offset->reg_number, scaled);
        return encoded;
    }

    const long long offset = memory->get_immediate_offset().value_or(0);
    const bool force_unscaled =
        opcode == AArch64MachineOpcode::LoadUnscaled ||
        opcode == AArch64MachineOpcode::StoreUnscaled ||
        opcode == AArch64MachineOpcode::LoadByteUnscaled ||
        opcode == AArch64MachineOpcode::StoreByteUnscaled ||
        opcode == AArch64MachineOpcode::LoadHalfUnscaled ||
        opcode == AArch64MachineOpcode::StoreHalfUnscaled || offset < 0 ||
        (offset % static_cast<long long>(access_size)) != 0;
    if (force_unscaled) {
        if (!check_signed_range(offset, -256, 255, diagnostic_engine,
                                "AArch64 direct object writer: unscaled load/store offset out of range")) {
            return std::nullopt;
        }
        encoded.word = encode_load_store_unscaled_word(unscaled_base, rt, rn, offset);
        return encoded;
    }
    const long long scaled = offset / static_cast<long long>(access_size);
    if (!check_signed_range(scaled, 0, 4095, diagnostic_engine,
                            "AArch64 direct object writer: unsigned load/store offset out of range")) {
        return std::nullopt;
    }
    encoded.word = encode_load_store_unsigned_word(
        unsigned_base, rt, rn, static_cast<unsigned>(scaled));
    return encoded;
}

} // namespace sysycc
