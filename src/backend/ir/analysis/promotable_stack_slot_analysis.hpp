#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrFunction;
class CoreIrInstruction;
class CoreIrLoadInst;
class CoreIrStackSlot;
class CoreIrStoreInst;
class CoreIrType;

enum class CoreIrPromotionUnitKind : unsigned char {
    WholeSlot,
    AccessPath,
};

enum class CoreIrPromotionFailureReason : unsigned char {
    AddressEscaped,
    DynamicIndex,
    NonLoadStoreUser,
    OverlappingAccessPath,
    UnsupportedLeafType,
    TypeMismatch,
    UndefinedOnSomePath,
};

struct CoreIrPromotionUnit {
    CoreIrPromotionUnitKind kind = CoreIrPromotionUnitKind::WholeSlot;
    CoreIrStackSlot *stack_slot = nullptr;
    std::vector<std::uint64_t> access_path;
    const CoreIrType *value_type = nullptr;
};

struct CoreIrPromotionUnitInfo {
    CoreIrPromotionUnit unit;
    std::vector<CoreIrLoadInst *> loads;
    std::vector<CoreIrStoreInst *> stores;
    std::unordered_set<CoreIrBasicBlock *> def_blocks;
};

struct CoreIrRejectedStackSlot {
    CoreIrStackSlot *stack_slot = nullptr;
    CoreIrPromotionFailureReason reason = CoreIrPromotionFailureReason::AddressEscaped;
};

class CoreIrPromotableStackSlotAnalysisResult {
  private:
    const CoreIrFunction *function_ = nullptr;
    std::vector<CoreIrPromotionUnitInfo> unit_infos_;
    std::unordered_map<const CoreIrInstruction *, std::size_t> instruction_to_unit_;
    std::vector<CoreIrRejectedStackSlot> rejected_slots_;

  public:
    CoreIrPromotableStackSlotAnalysisResult() = default;
    CoreIrPromotableStackSlotAnalysisResult(
        const CoreIrFunction *function, std::vector<CoreIrPromotionUnitInfo> unit_infos,
        std::unordered_map<const CoreIrInstruction *, std::size_t> instruction_to_unit,
        std::vector<CoreIrRejectedStackSlot> rejected_slots) noexcept;

    const CoreIrFunction *get_function() const noexcept { return function_; }

    const std::vector<CoreIrPromotionUnitInfo> &get_unit_infos() const noexcept {
        return unit_infos_;
    }

    const CoreIrPromotionUnitInfo *
    find_unit_for_instruction(const CoreIrInstruction *instruction) const noexcept;

    const std::vector<CoreIrRejectedStackSlot> &get_rejected_slots() const noexcept {
        return rejected_slots_;
    }
};

class CoreIrPromotableStackSlotAnalysis {
  public:
    using ResultType = CoreIrPromotableStackSlotAnalysisResult;

    CoreIrPromotableStackSlotAnalysisResult Run(const CoreIrFunction &function) const;
};

} // namespace sysycc
