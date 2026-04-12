#pragma once

#include <string>

#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"

namespace sysycc {

AArch64AsmCompileResult
compile_restricted_llvm_file_to_asm(const AArch64CodegenFileRequest &request);

AArch64ObjectCompileResult
compile_restricted_llvm_file_to_object(const AArch64CodegenFileRequest &request);

AArch64AsmCompileResult compile_restricted_llvm_text_to_asm(
    const AArch64CodegenFileRequest &request, const std::string &source_name,
    const std::string &text);

AArch64ObjectCompileResult compile_restricted_llvm_text_to_object(
    const AArch64CodegenFileRequest &request, const std::string &source_name,
    const std::string &text);

} // namespace sysycc
