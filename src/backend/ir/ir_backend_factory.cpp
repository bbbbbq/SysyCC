#include "backend/ir/ir_backend_factory.hpp"

#include <memory>

#include "backend/ir/ir_backend.hpp"
#include "backend/ir/llvm/llvm_ir_backend.hpp"

namespace sysycc {

std::unique_ptr<IRBackend> create_ir_backend(IrKind kind) {
    switch (kind) {
    case IrKind::LLVM:
        return std::make_unique<LlvmIrBackend>();
    case IrKind::None:
        break;
    }
    return nullptr;
}

} // namespace sysycc
