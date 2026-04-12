#pragma once

#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"

namespace sysycc {

struct AArch64BitcodeTextModule {
    bool ok = false;
    std::string textual_llvm_ir;
    std::vector<AArch64CodegenDiagnostic> diagnostics;
};

AArch64BitcodeTextModule load_llvm_bitcode_as_text(const std::string &file_path);

struct AArch64BitcodeWriteResult {
    bool ok = false;
    std::vector<AArch64CodegenDiagnostic> diagnostics;
};

AArch64BitcodeWriteResult write_llvm_ir_text_to_bitcode_file(
    const std::string &source_name, const std::string &text,
    const std::string &file_path);

} // namespace sysycc
