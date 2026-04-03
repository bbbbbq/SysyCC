#include "backend/ir/lower/lowering/core_ir_target_backend_factory.hpp"

#include <memory>

#include "backend/ir/shared/ir_kind.hpp"
#include "backend/ir/lower/lowering/aarch64/core_ir_aarch64_target_backend.hpp"
#include "backend/ir/lower/lowering/core_ir_target_backend.hpp"
#include "backend/ir/lower/lowering/llvm/core_ir_llvm_target_backend.hpp"

namespace sysycc {

std::unique_ptr<CoreIrTargetBackend> create_core_ir_target_backend(IrKind kind) {
    switch (kind) {
    case IrKind::LLVM:
        return std::make_unique<CoreIrLlvmTargetBackend>();
    case IrKind::AArch64:
        return std::make_unique<CoreIrAArch64TargetBackend>();
    case IrKind::None:
        return nullptr;
    }
    return nullptr;
}

} // namespace sysycc
