#pragma once

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_abi_lowering_pass.hpp"

namespace sysycc {

class CoreIrCallInst;
class CoreIrFunction;
class CoreIrInstruction;
class CoreIrParameter;
class CoreIrStackSlot;
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
    virtual AArch64FunctionAbiInfo classify_call(const CoreIrCallInst &call) const = 0;
};

bool is_supported_native_value_type(const CoreIrType *type);
bool instruction_has_canonical_vreg(const CoreIrInstruction &instruction);
std::size_t allocate_aggregate_value_slot(std::size_t &current_offset,
                                          const CoreIrType *type);
bool validate_function_lowering_readiness(const CoreIrFunction &function,
                                          AArch64FunctionPlanningContext &context);
void seed_incoming_stack_argument_offsets(
    const CoreIrFunction &function, const AArch64FunctionAbiInfo &abi_info,
    std::unordered_map<const CoreIrParameter *, std::size_t>
        &incoming_stack_argument_offsets);
void layout_stack_slots(AArch64MachineFunction &machine_function,
                        const CoreIrFunction &function,
                        std::size_t &current_offset);
bool seed_function_value_locations(
    const CoreIrFunction &function, AArch64MachineFunction &machine_function,
    std::unordered_map<const CoreIrValue *, AArch64ValueLocation> &value_locations,
    std::unordered_map<const CoreIrValue *, std::size_t> &aggregate_value_offsets,
    std::size_t &current_offset, AArch64FunctionPlanningContext &context);
void seed_call_argument_copy_slots(
    const CoreIrFunction &function,
    std::unordered_map<const CoreIrCallInst *, std::vector<std::size_t>>
        &indirect_call_argument_copy_offsets,
    std::size_t &current_offset, AArch64FunctionPlanningContext &context);
void seed_promoted_stack_slots(
    const CoreIrFunction &function,
    const std::unordered_map<const CoreIrValue *, AArch64ValueLocation> &value_locations,
    std::unordered_set<const CoreIrStackSlot *> &promoted_stack_slots);

} // namespace sysycc
