#pragma once

#include <stdint.h>

namespace sysycc {

enum class CoreIrAnalysisKind : uint8_t {
    Cfg,
    DominatorTree,
};

} // namespace sysycc
