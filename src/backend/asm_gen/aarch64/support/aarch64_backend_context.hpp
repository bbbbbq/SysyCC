#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/backend_options.hpp"

namespace sysycc {

class CoreIrModule;
class DiagnosticEngine;

struct AArch64CodegenContext {
    const CoreIrModule *module = nullptr;
    const BackendOptions *backend_options = nullptr;
    DiagnosticEngine *diagnostic_engine = nullptr;
    AArch64MachineModule machine_module;
};

} // namespace sysycc
