#include "backend/asm_gen/aarch64/support/aarch64_backend_context.hpp"

#include "backend/ir/shared/core/ir_module.hpp"

namespace sysycc {

AArch64CoreIrCodegenInputAdapter::AArch64CoreIrCodegenInputAdapter(
    const CoreIrModule &module) noexcept
    : module_(&module) {}

const CoreIrModule &AArch64CoreIrCodegenInputAdapter::core_ir_module() const {
    return *module_;
}

} // namespace sysycc
