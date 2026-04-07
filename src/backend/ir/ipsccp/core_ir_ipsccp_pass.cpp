#include "backend/ir/ipsccp/core_ir_ipsccp_pass.hpp"

#include <string>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::erase_instruction;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

bool try_fold_direct_call(CoreIrCallInst &call, CoreIrFunction &caller,
                          const CoreIrFunctionAttrsAnalysisResult &attrs) {
    const CoreIrFunctionAttrsSummary *summary =
        attrs.get_summary(caller.get_parent() == nullptr
                              ? nullptr
                              : caller.get_parent()->find_function(call.get_callee_name()));
    if (summary == nullptr) {
        return false;
    }

    CoreIrValue *replacement = nullptr;
    if (summary->constant_return != nullptr) {
        replacement = const_cast<CoreIrConstant *>(summary->constant_return);
    } else if (summary->returned_parameter_index.has_value() &&
               *summary->returned_parameter_index < call.get_argument_count()) {
        CoreIrValue *argument = call.get_argument(*summary->returned_parameter_index);
        if (dynamic_cast<CoreIrConstant *>(argument) != nullptr) {
            replacement = argument;
        }
    }

    if (replacement == nullptr) {
        return false;
    }

    call.replace_all_uses_with(replacement);
    if (call.get_uses().empty() &&
        summary->memory_behavior == CoreIrMemoryBehavior::None) {
        erase_instruction(*call.get_parent(), &call);
    }
    return true;
}

} // namespace

PassKind CoreIrIpsccpPass::Kind() const { return PassKind::CoreIrIpsccp; }

const char *CoreIrIpsccpPass::Name() const { return "CoreIrIpsccpPass"; }

CoreIrPassMetadata CoreIrIpsccpPass::Metadata() const noexcept {
    return CoreIrPassMetadata::core_ir_transform();
}

PassResult CoreIrIpsccpPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir analysis manager");
    }

    const CoreIrFunctionAttrsAnalysisResult &attrs =
        analysis_manager->get_or_compute<CoreIrFunctionAttrsAnalysis>(*module);

    CoreIrPassEffects effects;
    for (const auto &function_ptr : module->get_functions()) {
        CoreIrFunction *function = function_ptr.get();
        if (function == nullptr) {
            continue;
        }
        bool function_changed = false;
        for (const auto &block_ptr : function->get_basic_blocks()) {
            if (block_ptr == nullptr) {
                continue;
            }
            auto &instructions = block_ptr->get_instructions();
            for (std::size_t index = 0; index < instructions.size();) {
                auto *call = dynamic_cast<CoreIrCallInst *>(instructions[index].get());
                if (call == nullptr || !call->get_is_direct_call()) {
                    ++index;
                    continue;
                }
                if (!try_fold_direct_call(*call, *function, attrs)) {
                    ++index;
                    continue;
                }
                function_changed = true;
                if (index < instructions.size() && instructions[index].get() != call) {
                    continue;
                }
                ++index;
            }
        }
        if (function_changed) {
            effects.changed_functions.insert(function);
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    effects.preserved_analyses.preserve_cfg_family();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
