#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class CoreIrBinaryInst;
class DiagnosticEngine;

bool emit_non_float128_binary(AArch64MachineBlock &machine_block,
                              const CoreIrBinaryInst &binary,
                              const AArch64VirtualReg &lhs_reg,
                              const AArch64VirtualReg &rhs_reg,
                              const AArch64VirtualReg &dst_reg,
                              AArch64MachineFunction &function,
                              DiagnosticEngine &diagnostic_engine);

} // namespace sysycc
