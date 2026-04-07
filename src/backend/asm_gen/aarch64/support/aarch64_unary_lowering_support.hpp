#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrUnaryInst;
class DiagnosticEngine;

bool emit_non_float128_unary(AArch64MachineBlock &machine_block,
                             const CoreIrUnaryInst &unary,
                             const AArch64VirtualReg &operand_reg,
                             const AArch64VirtualReg &dst_reg,
                             AArch64MachineFunction &function,
                             DiagnosticEngine &diagnostic_engine);

} // namespace sysycc
