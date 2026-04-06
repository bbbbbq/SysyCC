#pragma once

#include <stdint.h>

namespace sysycc {

enum class CoreIrAnalysisKind : uint8_t {
    Cfg,
    DominatorTree,
    DominanceFrontier,
    PromotableStackSlot,
    LoopInfo,
    AliasAnalysis,
    MemorySSA,
    FunctionEffectSummary,
};

} // namespace sysycc
