#pragma once

#include <optional>
#include <string>

#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_model.hpp"

namespace sysycc {

std::optional<AArch64LlvmImportConstant> parse_llvm_import_constant_text(
    const AArch64LlvmImportType &type, const std::string &text);

} // namespace sysycc
