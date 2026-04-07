#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace sysycc {

enum class AArch64PhysicalReg : unsigned {
    X0 = 0,
    X1 = 1,
    X2 = 2,
    X3 = 3,
    X4 = 4,
    X5 = 5,
    X6 = 6,
    X7 = 7,
    X8 = 8,
    X9 = 9,
    X10 = 10,
    X11 = 11,
    X12 = 12,
    X13 = 13,
    X14 = 14,
    X15 = 15,
    X16 = 16,
    X17 = 17,
    X18 = 18,
    X19 = 19,
    X20 = 20,
    X21 = 21,
    X22 = 22,
    X23 = 23,
    X24 = 24,
    X25 = 25,
    X26 = 26,
    X27 = 27,
    X28 = 28,
    X29 = 29,
    X30 = 30,
    V0 = 100,
    V1 = 101,
    V2 = 102,
    V3 = 103,
    V4 = 104,
    V5 = 105,
    V6 = 106,
    V7 = 107,
    V8 = 108,
    V9 = 109,
    V10 = 110,
    V11 = 111,
    V12 = 112,
    V13 = 113,
    V14 = 114,
    V15 = 115,
    V16 = 116,
    V17 = 117,
    V18 = 118,
    V19 = 119,
    V20 = 120,
    V21 = 121,
    V22 = 122,
    V23 = 123,
    V24 = 124,
    V25 = 125,
    V26 = 126,
    V27 = 127,
    V28 = 128,
    V29 = 129,
    V30 = 130,
    V31 = 131,
};

enum class AArch64RegBank : unsigned char {
    General,
    FloatingPoint,
};

enum class AArch64VirtualRegKind : unsigned char {
    General32,
    General64,
    Float16,
    Float32,
    Float64,
    Float128,
};

enum class AArch64RegClass {
    CallerSavedGeneral,
    CalleeSavedGeneral,
    SpillScratchGeneral,
    CallerSavedFloat,
    CalleeSavedFloat,
};

class AArch64CallClobberMask {
  private:
    std::set<unsigned> regs_;

  public:
    AArch64CallClobberMask() = default;
    explicit AArch64CallClobberMask(std::set<unsigned> regs)
        : regs_(std::move(regs)) {}

    const std::set<unsigned> &get_regs() const noexcept { return regs_; }
    bool clobbers(unsigned reg) const noexcept {
        return regs_.find(reg) != regs_.end();
    }
};

struct AArch64InstructionFlags {
    bool is_call = false;
};

class AArch64VirtualReg {
  private:
    std::size_t id_ = 0;
    AArch64VirtualRegKind kind_ = AArch64VirtualRegKind::General32;

  public:
    AArch64VirtualReg() = default;
    AArch64VirtualReg(std::size_t id, AArch64VirtualRegKind kind)
        : id_(id), kind_(kind) {}

    std::size_t get_id() const noexcept { return id_; }
    AArch64VirtualRegKind get_kind() const noexcept { return kind_; }
    AArch64RegBank get_bank() const noexcept {
        switch (kind_) {
        case AArch64VirtualRegKind::General32:
        case AArch64VirtualRegKind::General64:
            return AArch64RegBank::General;
        case AArch64VirtualRegKind::Float16:
        case AArch64VirtualRegKind::Float32:
        case AArch64VirtualRegKind::Float64:
        case AArch64VirtualRegKind::Float128:
            return AArch64RegBank::FloatingPoint;
        }
        return AArch64RegBank::General;
    }
    bool is_general() const noexcept { return get_bank() == AArch64RegBank::General; }
    bool is_floating_point() const noexcept {
        return get_bank() == AArch64RegBank::FloatingPoint;
    }
    bool get_use_64bit() const noexcept {
        return kind_ == AArch64VirtualRegKind::General64 ||
               kind_ == AArch64VirtualRegKind::Float64;
    }
    bool is_valid() const noexcept { return id_ != 0; }
};

} // namespace sysycc
