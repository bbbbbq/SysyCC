#pragma once

#include <memory>
#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_model.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_module.hpp"

namespace sysycc {

struct AArch64CoreIrImportedModule {
    std::unique_ptr<CoreIrContext> context;
    CoreIrModule *module = nullptr;
    std::string source_target_triple;
    std::vector<std::string> module_asm_lines;
    std::vector<AArch64CodegenDiagnostic> diagnostics;
};

AArch64CoreIrImportedModule lower_llvm_import_model_to_core_ir(
    const AArch64LlvmImportModule &module);

} // namespace sysycc
