#pragma once

#include <stdint.h>

namespace sysycc {

enum class IrKind : uint8_t {
    None,
    LLVM,
};

} // namespace sysycc
