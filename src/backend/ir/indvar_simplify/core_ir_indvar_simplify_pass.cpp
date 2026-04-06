#include "backend/ir/indvar_simplify/core_ir_indvar_simplify_pass.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/induction_var_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
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

CoreIrComparePredicate negate_compare_predicate(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return CoreIrComparePredicate::NotEqual;
    case CoreIrComparePredicate::NotEqual:
        return CoreIrComparePredicate::Equal;
    case CoreIrComparePredicate::SignedLess:
        return CoreIrComparePredicate::SignedGreaterEqual;
    case CoreIrComparePredicate::SignedLessEqual:
        return CoreIrComparePredicate::SignedGreater;
    case CoreIrComparePredicate::SignedGreater:
        return CoreIrComparePredicate::SignedLessEqual;
    case CoreIrComparePredicate::SignedGreaterEqual:
        return CoreIrComparePredicate::SignedLess;
    case CoreIrComparePredicate::UnsignedLess:
        return CoreIrComparePredicate::UnsignedGreaterEqual;
    case CoreIrComparePredicate::UnsignedLessEqual:
        return CoreIrComparePredicate::UnsignedGreater;
    case CoreIrComparePredicate::UnsignedGreater:
        return CoreIrComparePredicate::UnsignedLessEqual;
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return CoreIrComparePredicate::UnsignedLess;
    }
    return predicate;
}

bool canonicalize_loop_compare(const CoreIrCanonicalInductionVarInfo &iv) {
    if (iv.exit_compare == nullptr || iv.phi == nullptr || iv.exit_bound == nullptr) {
        return false;
    }

    bool changed = false;
    if (iv.exit_compare->get_lhs() != iv.phi) {
        iv.exit_compare->set_operand(0, iv.phi);
        iv.exit_compare->set_operand(1, iv.exit_bound);
        iv.exit_compare->set_predicate(iv.normalized_predicate);
        changed = true;
    } else if (iv.exit_compare->get_rhs() != iv.exit_bound ||
               iv.exit_compare->get_predicate() != iv.normalized_predicate) {
        iv.exit_compare->set_operand(1, iv.exit_bound);
        iv.exit_compare->set_predicate(iv.normalized_predicate);
        changed = true;
    }

    if (iv.exit_branch == nullptr) {
        return changed;
    }

    CoreIrComparePredicate expected_branch_predicate = iv.normalized_predicate;
    if (!iv.inside_successor_is_true) {
        expected_branch_predicate = negate_compare_predicate(expected_branch_predicate);
    }
    if (iv.exit_compare->get_predicate() != expected_branch_predicate) {
        iv.exit_compare->set_predicate(expected_branch_predicate);
        changed = true;
    }
    return changed;
}

} // namespace

PassKind CoreIrIndVarSimplifyPass::Kind() const {
    return PassKind::CoreIrIndVarSimplify;
}

const char *CoreIrIndVarSimplifyPass::Name() const {
    return "CoreIrIndVarSimplifyPass";
}

PassResult CoreIrIndVarSimplifyPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir indvar simplify dependencies");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
        const CoreIrInductionVarAnalysisResult &induction_vars =
            analysis_manager->get_or_compute<CoreIrInductionVarAnalysis>(*function);

        bool function_changed = false;
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr == nullptr) {
                continue;
            }
            const CoreIrCanonicalInductionVarInfo *iv =
                induction_vars.get_canonical_induction_var(*loop_ptr);
            if (iv == nullptr) {
                continue;
            }
            function_changed = canonicalize_loop_compare(*iv) || function_changed;
        }

        if (function_changed) {
            effects.changed_functions.insert(function.get());
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }

    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    effects.preserved_analyses.preserve_cfg_family();
    effects.preserved_analyses.preserve(CoreIrAnalysisKind::PromotableStackSlot);
    effects.preserved_analyses.preserve_memory_family();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
