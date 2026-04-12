#pragma once

#include <string>

#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_model.hpp"

namespace sysycc {

AArch64LlvmImportModule
parse_restricted_llvm_ir_file(const std::string &file_path);

AArch64LlvmImportModule parse_restricted_llvm_ir_text(
    const std::string &source_name, const std::string &text);

} // namespace sysycc
