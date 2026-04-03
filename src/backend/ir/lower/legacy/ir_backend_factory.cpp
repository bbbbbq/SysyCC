#include "backend/ir/lower/legacy/ir_backend_factory.hpp"

#include <memory>

#include "backend/ir/lower/legacy/ir_backend.hpp"
#include "backend/ir/lower/legacy/llvm/llvm_ir_backend.hpp"

namespace sysycc {

std::unique_ptr<IRBackend> create_ir_backend(IrKind kind) {
    switch (kind) {
    case IrKind::LLVM:
        return std::make_unique<LlvmIrBackend>();
    case IrKind::AArch64:
    case IrKind::None:
        break;
    }
    return nullptr;
}

} // namespace sysycc
