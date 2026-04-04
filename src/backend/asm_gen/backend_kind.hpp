#pragma once

#include <stdint.h>

namespace sysycc {

enum class BackendKind : uint8_t {
    LlvmIr,
    AArch64Native,
};

} // namespace sysycc
