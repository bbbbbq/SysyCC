#pragma once

#include <memory>

namespace sysycc {

class CoreIrTargetBackend;
enum class IrKind : unsigned char;

std::unique_ptr<CoreIrTargetBackend> create_core_ir_target_backend(IrKind kind);

} // namespace sysycc
