#pragma once

#include <cstddef>
#include <unordered_map>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrFunction;
class CoreIrInstruction;
class CoreIrType;
class CoreIrValue;

enum class AArch64ValueLocationKind : unsigned char {
    VirtualReg,
    MemoryAddress,
    ConstantInt,
    ConstantNull,
    SymbolAddress,
    StackSlotAddress,
    None,
};

struct AArch64ValueLocation {
    AArch64ValueLocationKind kind = AArch64ValueLocationKind::None;
    AArch64VirtualReg virtual_reg;
};

class AArch64FunctionPlanningContext {
  public:
    virtual ~AArch64FunctionPlanningContext() = default;

    virtual void report_error(const std::string &message) = 0;
    virtual AArch64VirtualReg create_virtual_reg(AArch64MachineFunction &function,
                                                 const CoreIrType *type) = 0;
    virtual AArch64VirtualReg
    create_pointer_virtual_reg(AArch64MachineFunction &function) = 0;
};

bool is_supported_native_value_type(const CoreIrType *type);
bool instruction_has_canonical_vreg(const CoreIrInstruction &instruction);
std::size_t allocate_aggregate_value_slot(std::size_t &current_offset,
                                          const CoreIrType *type);
bool seed_function_value_locations(
    const CoreIrFunction &function, AArch64MachineFunction &machine_function,
    std::unordered_map<const CoreIrValue *, AArch64ValueLocation> &value_locations,
    std::unordered_map<const CoreIrValue *, std::size_t> &aggregate_value_offsets,
    std::size_t &current_offset, AArch64FunctionPlanningContext &context);

} // namespace sysycc
