#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/backend_options.hpp"

namespace sysycc {

class CoreIrModule;
class DiagnosticEngine;

class AArch64CodegenInput {
  public:
    virtual ~AArch64CodegenInput() = default;
    virtual const CoreIrModule &core_ir_module() const = 0;
};

class AArch64CoreIrCodegenInputAdapter final : public AArch64CodegenInput {
  private:
    const CoreIrModule *module_ = nullptr;

  public:
    explicit AArch64CoreIrCodegenInputAdapter(const CoreIrModule &module) noexcept;

    const CoreIrModule &core_ir_module() const override;
};

struct AArch64CodegenContext {
    const AArch64CodegenInput *input = nullptr;
    const BackendOptions *backend_options = nullptr;
    DiagnosticEngine *diagnostic_engine = nullptr;
    AArch64AsmModule asm_module;
    AArch64MachineModule machine_module;
    AArch64ObjectModule object_module;
};

} // namespace sysycc
