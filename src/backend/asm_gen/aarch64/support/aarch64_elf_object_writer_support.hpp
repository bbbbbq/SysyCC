#pragma once

#include <filesystem>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

class DiagnosticEngine;

struct AArch64DataOnlyObjectWriterOptions {
    bool force_defined_symbols_global = false;
};

bool write_aarch64_data_only_object(
    const AArch64ObjectModule &object_module,
    const std::filesystem::path &object_file,
    const AArch64DataOnlyObjectWriterOptions &options,
    DiagnosticEngine &diagnostic_engine);

} // namespace sysycc
