#pragma once

#include <optional>
#include <string>

#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_model.hpp"

namespace sysycc {

std::optional<AArch64LlvmImportType>
parse_llvm_import_type_text(const std::string &text);

} // namespace sysycc
