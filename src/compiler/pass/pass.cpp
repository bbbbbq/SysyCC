#include "pass.hpp"

#include <stdexcept>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"

namespace sysycc {

namespace {

std::string first_error_message(const DiagnosticEngine &diagnostic_engine) {
    for (const Diagnostic &diagnostic : diagnostic_engine.get_diagnostics()) {
        if (diagnostic.get_level() == DiagnosticLevel::Error) {
            return diagnostic.get_message();
        }
    }
    return "compilation failed";
}

bool should_stop_after_pass(const CompilerContext &context, PassKind pass_kind) {
    switch (context.get_stop_after_stage()) {
    case StopAfterStage::None:
        return false;
    case StopAfterStage::Preprocess:
        return pass_kind == PassKind::Preprocess;
    case StopAfterStage::Lex:
        return pass_kind == PassKind::Lex;
    case StopAfterStage::Parse:
        return pass_kind == PassKind::Parse;
    case StopAfterStage::Ast:
        return pass_kind == PassKind::Ast;
    case StopAfterStage::Semantic:
        return pass_kind == PassKind::Semantic;
    case StopAfterStage::CoreIr:
        return pass_kind == PassKind::CoreIrDce;
    case StopAfterStage::IR:
        return pass_kind == PassKind::LowerIr;
    case StopAfterStage::Asm:
        return pass_kind == PassKind::CodeGen;
    }

    return false;
}

std::unordered_set<CoreIrFunction *>
collect_changed_functions(const CoreIrPassEffects &effects) {
    std::unordered_set<CoreIrFunction *> changed_functions =
        effects.changed_functions;
    changed_functions.insert(effects.cfg_changed_functions.begin(),
                             effects.cfg_changed_functions.end());
    return changed_functions;
}

void invalidate_non_preserved_analyses(CompilerContext &context,
                                       const CoreIrPassEffects &effects) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrAnalysisManager *analysis_manager =
        build_result == nullptr ? nullptr : build_result->get_analysis_manager();
    if (analysis_manager == nullptr || !effects.has_changes()) {
        return;
    }

    constexpr CoreIrAnalysisKind k_all_analysis_kinds[] = {
        CoreIrAnalysisKind::Cfg,
        CoreIrAnalysisKind::DominatorTree,
        CoreIrAnalysisKind::DominanceFrontier,
        CoreIrAnalysisKind::PromotableStackSlot,
        CoreIrAnalysisKind::LoopInfo,
        CoreIrAnalysisKind::AliasAnalysis,
        CoreIrAnalysisKind::MemorySSA,
        CoreIrAnalysisKind::FunctionEffectSummary,
    };

    for (CoreIrFunction *function : collect_changed_functions(effects)) {
        if (function == nullptr) {
            continue;
        }
        const bool cfg_changed =
            effects.cfg_changed_functions.find(function) !=
            effects.cfg_changed_functions.end();
        for (CoreIrAnalysisKind kind : k_all_analysis_kinds) {
            const bool cfg_or_loop_family =
                kind == CoreIrAnalysisKind::Cfg ||
                kind == CoreIrAnalysisKind::DominatorTree ||
                kind == CoreIrAnalysisKind::DominanceFrontier ||
                kind == CoreIrAnalysisKind::LoopInfo;
            if (cfg_changed && cfg_or_loop_family) {
                analysis_manager->invalidate(*function, kind);
                continue;
            }
            if (!effects.preserved_analyses.preserves(kind)) {
                analysis_manager->invalidate(*function, kind);
            }
        }
    }
}

bool verify_core_ir_after_pass(CompilerContext &context, const Pass &pass,
                               const PassResult &result) {
    const CoreIrPassMetadata metadata = pass.Metadata();
    if (!metadata.verify_after_success) {
        return true;
    }

    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            std::string(pass.Name()) +
                " requested Core IR verification but no Core IR is available");
        return false;
    }

    CoreIrVerifier verifier;
    if (metadata.produces_core_ir) {
        return emit_core_ir_verify_result(context, verifier.verify_module(*module),
                                          pass.Name());
    }

    if (result.core_ir_effects.has_value() &&
        result.core_ir_effects->has_changes()) {
        for (CoreIrFunction *function :
             collect_changed_functions(*result.core_ir_effects)) {
            if (function == nullptr) {
                continue;
            }
            if (!emit_core_ir_verify_result(
                    context, verifier.verify_function(*function), pass.Name())) {
                return false;
            }
        }
        return true;
    }

    return emit_core_ir_verify_result(context, verifier.verify_module(*module),
                                      pass.Name());
}

} // namespace

void PassManager::AddPass(std::unique_ptr<Pass> pass) {
    if (pass == nullptr) {
        return;
    }

    if (get_pass_by_kind(pass->Kind()) != nullptr) {
        throw std::runtime_error(std::string("duplicate pass kind: ") +
                                 pass->Name());
    }

    passes_.push_back(std::move(pass));
}

Pass *PassManager::get_pass_by_kind(PassKind kind) const {
    for (const std::unique_ptr<Pass> &pass : passes_) {
        if (pass != nullptr && pass->Kind() == kind) {
            return pass.get();
        }
    }

    return nullptr;
}

PassResult PassManager::Run(CompilerContext &context) {
    for (const std::unique_ptr<Pass> &pass : passes_) {
        if (pass == nullptr) {
            return PassResult::Failure("encountered null pass");
        }

        PassResult result = pass->Run(context);
        if (!result.ok) {
            return result;
        }
        if (context.get_diagnostic_engine().has_error()) {
            return PassResult::Failure(
                first_error_message(context.get_diagnostic_engine()));
        }
        if (pass->Metadata().writes_core_ir && !result.core_ir_effects.has_value()) {
            return PassResult::Failure(std::string(pass->Name()) +
                                       " wrote Core IR but did not report CoreIrPassEffects");
        }
        if (result.core_ir_effects.has_value()) {
            invalidate_non_preserved_analyses(context, *result.core_ir_effects);
        }
        if (!verify_core_ir_after_pass(context, *pass, result)) {
            return PassResult::Failure(std::string(pass->Name()) +
                                       " failed Core IR verification");
        }
        if (should_stop_after_pass(context, pass->Kind())) {
            return PassResult::Success();
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
