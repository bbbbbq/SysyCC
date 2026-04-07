#pragma once

#include <stdint.h>

namespace sysycc {

enum class CoreIrAnalysisKind : uint8_t {
    CallGraph,
    FunctionAttrs,
    Cfg,
    DominatorTree,
    DominanceFrontier,
    PromotableStackSlot,
    LoopInfo,
    InductionVar,
    ScalarEvolutionLite,
    AliasAnalysis,
    MemorySSA,
    FunctionEffectSummary,
    EscapeAnalysis,
    BlockFrequencyLite,
    TargetTransformInfoLite,
};

} // namespace sysycc
