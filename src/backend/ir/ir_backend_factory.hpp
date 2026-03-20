#pragma once

#include <memory>

#include "backend/ir/ir_kind.hpp"

namespace sysycc {

class IRBackend;

std::unique_ptr<IRBackend> create_ir_backend(IrKind kind);

} // namespace sysycc
