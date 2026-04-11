#include "backend/asm_gen/aarch64/support/aarch64_fp_instruction_encoding_support.hpp"

namespace sysycc {

bool is_supported_scalar_fp_kind(AArch64VirtualRegKind kind) {
    return kind == AArch64VirtualRegKind::Float16 ||
           kind == AArch64VirtualRegKind::Float32 ||
           kind == AArch64VirtualRegKind::Float64;
}

std::size_t scalar_fp_size(AArch64VirtualRegKind kind) {
    switch (kind) {
    case AArch64VirtualRegKind::Float16:
        return 2;
    case AArch64VirtualRegKind::Float32:
        return 4;
    case AArch64VirtualRegKind::Float64:
        return 8;
    default:
        return 0;
    }
}

std::optional<std::uint32_t> fp_reg_move_base(AArch64VirtualRegKind kind) {
    switch (kind) {
    case AArch64VirtualRegKind::Float16:
        return 0x1EE04000U;
    case AArch64VirtualRegKind::Float32:
        return 0x1E204000U;
    case AArch64VirtualRegKind::Float64:
        return 0x1E604000U;
    default:
        return std::nullopt;
    }
}

std::optional<std::uint32_t>
fp_gp_move_base(AArch64VirtualRegKind fp_kind, bool gp_is_64bit, bool gp_to_fp) {
    if (gp_to_fp) {
        if (fp_kind == AArch64VirtualRegKind::Float32 && !gp_is_64bit) {
            return 0x1E270000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64 && gp_is_64bit) {
            return 0x9E670000U;
        }
        return std::nullopt;
    }
    if (fp_kind == AArch64VirtualRegKind::Float32 && !gp_is_64bit) {
        return 0x1E260000U;
    }
    if (fp_kind == AArch64VirtualRegKind::Float64 && gp_is_64bit) {
        return 0x9E660000U;
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
fp_binary_base(AArch64MachineOpcode opcode, AArch64VirtualRegKind kind) {
    if (kind == AArch64VirtualRegKind::Float16) {
        if (opcode == AArch64MachineOpcode::FloatAdd)
            return 0x1EE02800U;
        if (opcode == AArch64MachineOpcode::FloatSub)
            return 0x1EE03800U;
        if (opcode == AArch64MachineOpcode::FloatMul)
            return 0x1EE00800U;
        if (opcode == AArch64MachineOpcode::FloatDiv)
            return 0x1EE01800U;
    }
    if (kind == AArch64VirtualRegKind::Float32) {
        if (opcode == AArch64MachineOpcode::FloatAdd)
            return 0x1E202800U;
        if (opcode == AArch64MachineOpcode::FloatSub)
            return 0x1E203800U;
        if (opcode == AArch64MachineOpcode::FloatMul)
            return 0x1E200800U;
        if (opcode == AArch64MachineOpcode::FloatDiv)
            return 0x1E201800U;
    }
    if (kind == AArch64VirtualRegKind::Float64) {
        if (opcode == AArch64MachineOpcode::FloatAdd)
            return 0x1E602800U;
        if (opcode == AArch64MachineOpcode::FloatSub)
            return 0x1E603800U;
        if (opcode == AArch64MachineOpcode::FloatMul)
            return 0x1E600800U;
        if (opcode == AArch64MachineOpcode::FloatDiv)
            return 0x1E601800U;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> fcmp_base(AArch64VirtualRegKind kind) {
    switch (kind) {
    case AArch64VirtualRegKind::Float16:
        return 0x1EE02000U;
    case AArch64VirtualRegKind::Float32:
        return 0x1E202000U;
    case AArch64VirtualRegKind::Float64:
        return 0x1E602000U;
    default:
        return std::nullopt;
    }
}

std::optional<std::uint32_t>
int_to_fp_base(AArch64MachineOpcode opcode, AArch64VirtualRegKind fp_kind,
               bool src_is_64bit) {
    if (opcode == AArch64MachineOpcode::SignedIntToFloat) {
        if (fp_kind == AArch64VirtualRegKind::Float16) {
            return src_is_64bit ? 0x9EE20000U : 0x1EE20000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float32) {
            return src_is_64bit ? 0x9E220000U : 0x1E220000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64) {
            return src_is_64bit ? 0x9E620000U : 0x1E620000U;
        }
    } else if (opcode == AArch64MachineOpcode::UnsignedIntToFloat) {
        if (fp_kind == AArch64VirtualRegKind::Float16) {
            return src_is_64bit ? 0x9EE30000U : 0x1EE30000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float32) {
            return src_is_64bit ? 0x9E230000U : 0x1E230000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64) {
            return src_is_64bit ? 0x9E630000U : 0x1E630000U;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
fp_to_int_base(AArch64MachineOpcode opcode, AArch64VirtualRegKind fp_kind,
               bool dst_is_64bit) {
    if (opcode == AArch64MachineOpcode::FloatToSignedInt) {
        if (fp_kind == AArch64VirtualRegKind::Float16) {
            return dst_is_64bit ? 0x9EF80000U : 0x1EF80000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float32) {
            return dst_is_64bit ? 0x9E380000U : 0x1E380000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64) {
            return dst_is_64bit ? 0x9E780000U : 0x1E780000U;
        }
    } else if (opcode == AArch64MachineOpcode::FloatToUnsignedInt) {
        if (fp_kind == AArch64VirtualRegKind::Float16) {
            return dst_is_64bit ? 0x9EF90000U : 0x1EF90000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float32) {
            return dst_is_64bit ? 0x9E390000U : 0x1E390000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64) {
            return dst_is_64bit ? 0x9E790000U : 0x1E790000U;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
fp_convert_base(AArch64VirtualRegKind dst_kind, AArch64VirtualRegKind src_kind) {
    if (dst_kind == AArch64VirtualRegKind::Float16 &&
        src_kind == AArch64VirtualRegKind::Float32) {
        return 0x1E23C000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float32 &&
        src_kind == AArch64VirtualRegKind::Float16) {
        return 0x1EE24000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float16 &&
        src_kind == AArch64VirtualRegKind::Float64) {
        return 0x1E63C000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float64 &&
        src_kind == AArch64VirtualRegKind::Float16) {
        return 0x1EE2C000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float64 &&
        src_kind == AArch64VirtualRegKind::Float32) {
        return 0x1E22C000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float32 &&
        src_kind == AArch64VirtualRegKind::Float64) {
        return 0x1E624000U;
    }
    return std::nullopt;
}

} // namespace sysycc
