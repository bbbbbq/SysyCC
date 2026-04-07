#pragma once

#include <stdint.h>
#include <memory>
#include <optional>
#include <cstddef>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend/ir/analysis/core_ir_analysis_kind.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

class CoreIrFunction;

struct CoreIrPassMetadata {
    bool reads_core_ir = false;
    bool writes_core_ir = false;
    bool produces_core_ir = false;
    bool verify_after_success = false;

    static CoreIrPassMetadata core_ir_build() noexcept {
        return CoreIrPassMetadata{false, false, true, true};
    }

    static CoreIrPassMetadata core_ir_transform() noexcept {
        return CoreIrPassMetadata{true, true, false, true};
    }

    static CoreIrPassMetadata core_ir_reader() noexcept {
        return CoreIrPassMetadata{true, false, false, false};
    }
};

class CoreIrPreservedAnalyses {
  private:
    bool preserve_all_ = false;
    std::unordered_set<CoreIrAnalysisKind> preserved_;

  public:
    static CoreIrPreservedAnalyses preserve_all() {
        CoreIrPreservedAnalyses analyses;
        analyses.preserve_all_ = true;
        return analyses;
    }

    static CoreIrPreservedAnalyses preserve_none() {
        return CoreIrPreservedAnalyses{};
    }

    void preserve(CoreIrAnalysisKind kind) { preserved_.insert(kind); }

    void preserve_cfg_family() {
        preserve(CoreIrAnalysisKind::Cfg);
        preserve(CoreIrAnalysisKind::DominatorTree);
        preserve(CoreIrAnalysisKind::DominanceFrontier);
        preserve(CoreIrAnalysisKind::LoopInfo);
    }

    void preserve_memory_family() {
        preserve(CoreIrAnalysisKind::AliasAnalysis);
        preserve(CoreIrAnalysisKind::MemorySSA);
        preserve(CoreIrAnalysisKind::FunctionEffectSummary);
    }

    void preserve_loop_family() { preserve(CoreIrAnalysisKind::LoopInfo); }

    bool preserves(CoreIrAnalysisKind kind) const noexcept {
        return preserve_all_ || preserved_.find(kind) != preserved_.end();
    }

    bool get_preserve_all() const noexcept { return preserve_all_; }

    bool preserves_none() const noexcept {
        return !preserve_all_ && preserved_.empty();
    }
};

struct CoreIrPassEffects {
    bool module_changed = false;
    std::unordered_set<CoreIrFunction *> changed_functions;
    std::unordered_set<CoreIrFunction *> cfg_changed_functions;
    CoreIrPreservedAnalyses preserved_analyses =
        CoreIrPreservedAnalyses::preserve_none();

    bool has_changes() const noexcept {
        return module_changed || !changed_functions.empty() ||
               !cfg_changed_functions.empty();
    }
};

enum class PassKind : uint8_t {
    Preprocess,
    Lex,
    Parse,
    Ast,
    Semantic,
    BuildCoreIr,
    CoreIrFunctionAttrs,
    CoreIrIpsccp,
    CoreIrArgumentPromotion,
    CoreIrInliner,
    CoreIrGlobalDce,
    CoreIrCanonicalize,
    CoreIrSimplifyCfg,
    CoreIrLoopSimplify,
    CoreIrLoopRotate,
    CoreIrLcssa,
    CoreIrIndVarSimplify,
    CoreIrSimpleLoopUnswitch,
    CoreIrLoopIdiom,
    CoreIrSroa,
    CoreIrLoopMemoryPromotion,
    CoreIrLoopUnroll,
    CoreIrStackSlotForward,
    CoreIrCopyPropagation,
    CoreIrLocalCse,
    CoreIrDeadStoreElimination,
    CoreIrMem2Reg,
    CoreIrConstFold,
    CoreIrSccp,
    CoreIrInstCombine,
    CoreIrLicm,
    CoreIrGvn,
    CoreIrDce,
    LowerIr,
    CodeGen,
};

struct PassResult {
    bool ok = true;
    std::string message;
    std::optional<CoreIrPassEffects> core_ir_effects;

    static PassResult Success() { return {true, "", std::nullopt}; }

    static PassResult Success(CoreIrPassEffects effects) {
        return {true, "", std::move(effects)};
    }

    static PassResult Failure(std::string msg) {
        return {false, std::move(msg), std::nullopt};
    }
};

// Defines the common interface that every compiler pass must implement.
class Pass {
  public:
    virtual ~Pass() = default;
    virtual PassKind Kind() const = 0;
    virtual const char *Name() const = 0;
    virtual CoreIrPassMetadata Metadata() const noexcept { return {}; }
    virtual PassResult Run(CompilerContext &context) = 0;
};

// Owns pass objects and runs them in pipeline order.
class PassManager {
  private:
    struct FixedPointPassGroup {
        std::vector<std::unique_ptr<Pass>> passes;
        std::size_t max_iterations = 4;
        bool module_scope = false;
    };

    struct PipelineEntry {
        std::unique_ptr<Pass> pass;
        std::optional<FixedPointPassGroup> fixed_point_group;
    };

    std::vector<PipelineEntry> entries_;

  public:
    void AddPass(std::unique_ptr<Pass> pass);
    void AddCoreIrFixedPointGroup(std::vector<std::unique_ptr<Pass>> passes,
                                  std::size_t max_iterations = 4);
    void AddCoreIrModuleFixedPointGroup(std::vector<std::unique_ptr<Pass>> passes,
                                        std::size_t max_iterations = 4);
    PassManager() = default;
    Pass *get_pass_by_kind(PassKind kind) const;
    PassResult Run(CompilerContext &context);
};

} // namespace sysycc
