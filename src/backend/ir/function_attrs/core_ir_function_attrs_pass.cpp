#include "backend/ir/function_attrs/core_ir_function_attrs_pass.hpp"

#include <string>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/function_attrs_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

void derive_memory_flags(const CoreIrFunctionAttrsSummary &summary, bool &readnone,
                         bool &readonly, bool &writeonly) {
    readnone = summary.memory_behavior == CoreIrMemoryBehavior::None;
    readonly = summary.memory_behavior == CoreIrMemoryBehavior::Read;
    writeonly = summary.memory_behavior == CoreIrMemoryBehavior::Write;
}

} // namespace

PassKind CoreIrFunctionAttrsPass::Kind() const {
    return PassKind::CoreIrFunctionAttrs;
}

const char *CoreIrFunctionAttrsPass::Name() const {
    return "CoreIrFunctionAttrsPass";
}

CoreIrPassMetadata CoreIrFunctionAttrsPass::Metadata() const noexcept {
    return CoreIrPassMetadata::core_ir_transform();
}

PassResult CoreIrFunctionAttrsPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir function attrs dependencies");
    }

    const CoreIrFunctionAttrsAnalysisResult &analysis =
        analysis_manager->get_or_compute<CoreIrFunctionAttrsAnalysis>(*module);

    CoreIrPassEffects effects;
    for (const auto &function_ptr : module->get_functions()) {
        CoreIrFunction *function = function_ptr.get();
        if (function == nullptr) {
            continue;
        }
        const CoreIrFunctionAttrsSummary *summary = analysis.get_summary(function);
        if (summary == nullptr) {
            continue;
        }

        bool readnone = false;
        bool readonly = false;
        bool writeonly = false;
        derive_memory_flags(*summary, readnone, readonly, writeonly);

        bool changed = false;
        if (function->get_is_readnone() != readnone) {
            function->set_is_readnone(readnone);
            changed = true;
        }
        if (function->get_is_readonly() != readonly) {
            function->set_is_readonly(readonly);
            changed = true;
        }
        if (function->get_is_writeonly() != writeonly) {
            function->set_is_writeonly(writeonly);
            changed = true;
        }
        if (function->get_is_norecurse() != summary->is_norecurse) {
            function->set_is_norecurse(summary->is_norecurse);
            changed = true;
        }
        if (function->get_parameter_nocapture() != summary->parameter_nocapture) {
            function->set_parameter_nocapture(summary->parameter_nocapture);
            changed = true;
        }
        effects.module_changed = effects.module_changed || changed;
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
