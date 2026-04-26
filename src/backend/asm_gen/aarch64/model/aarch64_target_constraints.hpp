#pragma once

#include <algorithm>
#include <array>
#include <set>

#include "backend/asm_gen/aarch64/model/aarch64_register_info.hpp"

namespace sysycc {

inline constexpr std::array<unsigned, 7>
    kAArch64CallerSavedAllocatableGeneralPhysicalRegs = {
        static_cast<unsigned>(AArch64PhysicalReg::X9),
        static_cast<unsigned>(AArch64PhysicalReg::X10),
        static_cast<unsigned>(AArch64PhysicalReg::X11),
        static_cast<unsigned>(AArch64PhysicalReg::X12),
        static_cast<unsigned>(AArch64PhysicalReg::X13),
        static_cast<unsigned>(AArch64PhysicalReg::X14),
        static_cast<unsigned>(AArch64PhysicalReg::X15),
    };

inline constexpr std::array<unsigned, 5>
    kAArch64CalleeSavedAllocatableGeneralPhysicalRegs = {
        static_cast<unsigned>(AArch64PhysicalReg::X19),
        static_cast<unsigned>(AArch64PhysicalReg::X20),
        static_cast<unsigned>(AArch64PhysicalReg::X21),
        static_cast<unsigned>(AArch64PhysicalReg::X22),
        static_cast<unsigned>(AArch64PhysicalReg::X23),
    };

inline constexpr std::array<unsigned, 18>
    kAArch64CallerSavedAllocatableFloatPhysicalRegs = {
        // v0/v1 are still reserved for backend helper/scratch sequences.  The
        // remaining argument registers are safe caller-saved colors and are
        // important for wide vector-PHI kernels.
        static_cast<unsigned>(AArch64PhysicalReg::V2),
        static_cast<unsigned>(AArch64PhysicalReg::V3),
        static_cast<unsigned>(AArch64PhysicalReg::V4),
        static_cast<unsigned>(AArch64PhysicalReg::V5),
        static_cast<unsigned>(AArch64PhysicalReg::V6),
        static_cast<unsigned>(AArch64PhysicalReg::V7),
        static_cast<unsigned>(AArch64PhysicalReg::V16),
        static_cast<unsigned>(AArch64PhysicalReg::V17),
        static_cast<unsigned>(AArch64PhysicalReg::V18),
        static_cast<unsigned>(AArch64PhysicalReg::V19),
        static_cast<unsigned>(AArch64PhysicalReg::V20),
        static_cast<unsigned>(AArch64PhysicalReg::V21),
        static_cast<unsigned>(AArch64PhysicalReg::V22),
        static_cast<unsigned>(AArch64PhysicalReg::V23),
        static_cast<unsigned>(AArch64PhysicalReg::V24),
        static_cast<unsigned>(AArch64PhysicalReg::V25),
        static_cast<unsigned>(AArch64PhysicalReg::V26),
        static_cast<unsigned>(AArch64PhysicalReg::V27),
    };

inline constexpr std::array<unsigned, 8>
    kAArch64CalleeSavedAllocatableFloatPhysicalRegs = {
        static_cast<unsigned>(AArch64PhysicalReg::V8),
        static_cast<unsigned>(AArch64PhysicalReg::V9),
        static_cast<unsigned>(AArch64PhysicalReg::V10),
        static_cast<unsigned>(AArch64PhysicalReg::V11),
        static_cast<unsigned>(AArch64PhysicalReg::V12),
        static_cast<unsigned>(AArch64PhysicalReg::V13),
        static_cast<unsigned>(AArch64PhysicalReg::V14),
        static_cast<unsigned>(AArch64PhysicalReg::V15),
    };

inline constexpr std::array<unsigned, 4> kAArch64SpillScratchGeneralPhysicalRegs = {
    static_cast<unsigned>(AArch64PhysicalReg::X24),
    static_cast<unsigned>(AArch64PhysicalReg::X25),
    static_cast<unsigned>(AArch64PhysicalReg::X26),
    static_cast<unsigned>(AArch64PhysicalReg::X27),
};

inline constexpr unsigned kAArch64SpillAddressPhysicalReg =
    static_cast<unsigned>(AArch64PhysicalReg::X28);

inline constexpr std::array<unsigned, 4> kAArch64SpillScratchFloatPhysicalRegs = {
    static_cast<unsigned>(AArch64PhysicalReg::V28),
    static_cast<unsigned>(AArch64PhysicalReg::V29),
    static_cast<unsigned>(AArch64PhysicalReg::V30),
    static_cast<unsigned>(AArch64PhysicalReg::V31),
};

inline constexpr std::array<unsigned, 11>
    kAArch64FixedBorrowableGeneralPhysicalRegs = {
        static_cast<unsigned>(AArch64PhysicalReg::X0),
        static_cast<unsigned>(AArch64PhysicalReg::X1),
        static_cast<unsigned>(AArch64PhysicalReg::X2),
        static_cast<unsigned>(AArch64PhysicalReg::X3),
        static_cast<unsigned>(AArch64PhysicalReg::X4),
        static_cast<unsigned>(AArch64PhysicalReg::X5),
        static_cast<unsigned>(AArch64PhysicalReg::X6),
        static_cast<unsigned>(AArch64PhysicalReg::X7),
        static_cast<unsigned>(AArch64PhysicalReg::X8),
        static_cast<unsigned>(AArch64PhysicalReg::X16),
        static_cast<unsigned>(AArch64PhysicalReg::X17),
    };

inline constexpr std::array<unsigned, 8> kAArch64FixedBorrowableFloatPhysicalRegs = {
    static_cast<unsigned>(AArch64PhysicalReg::V0),
    static_cast<unsigned>(AArch64PhysicalReg::V1),
    static_cast<unsigned>(AArch64PhysicalReg::V2),
    static_cast<unsigned>(AArch64PhysicalReg::V3),
    static_cast<unsigned>(AArch64PhysicalReg::V4),
    static_cast<unsigned>(AArch64PhysicalReg::V5),
    static_cast<unsigned>(AArch64PhysicalReg::V6),
    static_cast<unsigned>(AArch64PhysicalReg::V7),
};

inline constexpr std::array<unsigned, 18> kAArch64DefaultCallerClobberedGeneralRegs = {
    static_cast<unsigned>(AArch64PhysicalReg::X0),
    static_cast<unsigned>(AArch64PhysicalReg::X1),
    static_cast<unsigned>(AArch64PhysicalReg::X2),
    static_cast<unsigned>(AArch64PhysicalReg::X3),
    static_cast<unsigned>(AArch64PhysicalReg::X4),
    static_cast<unsigned>(AArch64PhysicalReg::X5),
    static_cast<unsigned>(AArch64PhysicalReg::X6),
    static_cast<unsigned>(AArch64PhysicalReg::X7),
    static_cast<unsigned>(AArch64PhysicalReg::X8),
    static_cast<unsigned>(AArch64PhysicalReg::X9),
    static_cast<unsigned>(AArch64PhysicalReg::X10),
    static_cast<unsigned>(AArch64PhysicalReg::X11),
    static_cast<unsigned>(AArch64PhysicalReg::X12),
    static_cast<unsigned>(AArch64PhysicalReg::X13),
    static_cast<unsigned>(AArch64PhysicalReg::X14),
    static_cast<unsigned>(AArch64PhysicalReg::X15),
    static_cast<unsigned>(AArch64PhysicalReg::X16),
    static_cast<unsigned>(AArch64PhysicalReg::X17),
};

inline constexpr std::array<unsigned, 24> kAArch64DefaultCallerClobberedFloatRegs = {
    static_cast<unsigned>(AArch64PhysicalReg::V0),
    static_cast<unsigned>(AArch64PhysicalReg::V1),
    static_cast<unsigned>(AArch64PhysicalReg::V2),
    static_cast<unsigned>(AArch64PhysicalReg::V3),
    static_cast<unsigned>(AArch64PhysicalReg::V4),
    static_cast<unsigned>(AArch64PhysicalReg::V5),
    static_cast<unsigned>(AArch64PhysicalReg::V6),
    static_cast<unsigned>(AArch64PhysicalReg::V7),
    static_cast<unsigned>(AArch64PhysicalReg::V16),
    static_cast<unsigned>(AArch64PhysicalReg::V17),
    static_cast<unsigned>(AArch64PhysicalReg::V18),
    static_cast<unsigned>(AArch64PhysicalReg::V19),
    static_cast<unsigned>(AArch64PhysicalReg::V20),
    static_cast<unsigned>(AArch64PhysicalReg::V21),
    static_cast<unsigned>(AArch64PhysicalReg::V22),
    static_cast<unsigned>(AArch64PhysicalReg::V23),
    static_cast<unsigned>(AArch64PhysicalReg::V24),
    static_cast<unsigned>(AArch64PhysicalReg::V25),
    static_cast<unsigned>(AArch64PhysicalReg::V26),
    static_cast<unsigned>(AArch64PhysicalReg::V27),
    static_cast<unsigned>(AArch64PhysicalReg::V28),
    static_cast<unsigned>(AArch64PhysicalReg::V29),
    static_cast<unsigned>(AArch64PhysicalReg::V30),
    static_cast<unsigned>(AArch64PhysicalReg::V31),
};

inline bool is_aarch64_callee_saved_allocatable_general_physical_reg(unsigned reg) {
    return std::find(kAArch64CalleeSavedAllocatableGeneralPhysicalRegs.begin(),
                     kAArch64CalleeSavedAllocatableGeneralPhysicalRegs.end(),
                     reg) != kAArch64CalleeSavedAllocatableGeneralPhysicalRegs.end();
}

inline bool is_aarch64_callee_saved_allocatable_float_physical_reg(unsigned reg) {
    return std::find(kAArch64CalleeSavedAllocatableFloatPhysicalRegs.begin(),
                     kAArch64CalleeSavedAllocatableFloatPhysicalRegs.end(),
                     reg) != kAArch64CalleeSavedAllocatableFloatPhysicalRegs.end();
}

inline AArch64CallClobberMask make_default_aarch64_call_clobber_mask() {
    std::set<unsigned> regs(kAArch64DefaultCallerClobberedGeneralRegs.begin(),
                            kAArch64DefaultCallerClobberedGeneralRegs.end());
    regs.insert(kAArch64DefaultCallerClobberedFloatRegs.begin(),
                kAArch64DefaultCallerClobberedFloatRegs.end());
    return AArch64CallClobberMask(std::move(regs));
}

} // namespace sysycc
