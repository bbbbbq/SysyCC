#pragma once

#include "backend/asm_gen/riscv64/api/riscv64_codegen_api.hpp"

namespace sysycc {

Riscv64AsmCompileResult
compile_llvm_ir_file_to_riscv64_asm(const Riscv64CodegenFileRequest &request);

Riscv64ObjectCompileResult compile_llvm_ir_file_to_riscv64_object(
    const Riscv64CodegenFileRequest &request);

Riscv64AsmCompileResult
compile_llvm_bitcode_file_to_riscv64_asm(
    const Riscv64CodegenFileRequest &request);

Riscv64ObjectCompileResult compile_llvm_bitcode_file_to_riscv64_object(
    const Riscv64CodegenFileRequest &request);

} // namespace sysycc
