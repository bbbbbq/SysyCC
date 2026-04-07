#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

class CoreIrValue;
class CoreIrConstantFloat;
class DiagnosticEngine;

class AArch64ScalarLoweringContext {
  public:
    virtual ~AArch64ScalarLoweringContext() = default;

    virtual bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                                      const CoreIrValue *value,
                                      AArch64VirtualReg &out) = 0;
    virtual bool require_canonical_vreg(const CoreIrValue *value,
                                        AArch64VirtualReg &out) const = 0;
    virtual bool emit_float128_binary_helper(AArch64MachineBlock &machine_block,
                                             CoreIrBinaryOpcode opcode,
                                             const AArch64VirtualReg &lhs_reg,
                                             const AArch64VirtualReg &rhs_reg,
                                             const AArch64VirtualReg &dst_reg) = 0;
    virtual bool emit_float128_compare_helper(
        AArch64MachineBlock &machine_block, CoreIrComparePredicate predicate,
        const AArch64VirtualReg &lhs_reg, const AArch64VirtualReg &rhs_reg,
        const AArch64VirtualReg &dst_reg,
        AArch64MachineFunction &function) = 0;
    virtual bool emit_float128_cast_helper(AArch64MachineBlock &machine_block,
                                           const CoreIrCastInst &cast,
                                           const AArch64VirtualReg &operand_reg,
                                           const AArch64VirtualReg &dst_reg,
                                           AArch64MachineFunction &function) = 0;
    virtual bool materialize_float_constant(AArch64MachineBlock &machine_block,
                                            const CoreIrConstantFloat &constant,
                                            const AArch64VirtualReg &target_reg,
                                            AArch64MachineFunction &function) = 0;
    virtual DiagnosticEngine &diagnostic_engine() const = 0;
};

} // namespace sysycc
