#include "pass.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
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

bool should_stop_after_pass(const CompilerContext &context,
                            PassKind pass_kind) {
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
        return context.get_optimization_level() == OptimizationLevel::O1
                   ? pass_kind == PassKind::CoreIrDce
                   : pass_kind == PassKind::CoreIrMem2Reg;
    case StopAfterStage::IR:
        return pass_kind == PassKind::LowerIr;
    case StopAfterStage::Asm:
        return pass_kind == PassKind::CodeGen;
    }

    return false;
}

bool should_run_fixed_point_group(const CompilerContext &context) {
    return context.get_optimization_level() == OptimizationLevel::O1;
}

bool env_flag_enabled(const char *name) {
    const char *value = std::getenv(name);
    if (value == nullptr) {
        return false;
    }
    const std::string text(value);
    return text == "1" || text == "true" || text == "TRUE" ||
           text == "yes" || text == "YES" || text == "on" ||
           text == "ON";
}

bool trace_passes_enabled() { return env_flag_enabled("SYSYCC_TRACE_PASSES"); }

std::size_t count_core_ir_blocks(const CompilerContext &context) {
    const CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    const CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return 0;
    }

    std::size_t count = 0;
    for (const auto &function : module->get_functions()) {
        if (function != nullptr) {
            count += function->get_basic_blocks().size();
        }
    }
    return count;
}

void trace_pass_event(std::string_view event, const Pass &pass,
                      std::size_t block_count) {
    std::cerr << "[sysycc-pass] " << event << ' ' << pass.Name()
              << " blocks=" << block_count << '\n';
}

void trace_pass_leave(const Pass &pass, bool changed, bool stopped,
                      std::size_t block_count,
                      std::chrono::steady_clock::duration elapsed) {
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    std::cerr << "[sysycc-pass] leave " << pass.Name()
              << " changed=" << (changed ? 1 : 0)
              << " stopped=" << (stopped ? 1 : 0)
              << " blocks=" << block_count << " elapsed_ms=" << elapsed_ms
              << '\n';
}

bool contains_float_leaf_type(const CoreIrType *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->get_kind()) {
    case CoreIrTypeKind::Float:
        return true;
    case CoreIrTypeKind::Pointer:
        return contains_float_leaf_type(
            static_cast<const CoreIrPointerType *>(type)->get_pointee_type());
    case CoreIrTypeKind::Array:
        return contains_float_leaf_type(
            static_cast<const CoreIrArrayType *>(type)->get_element_type());
    case CoreIrTypeKind::Struct: {
        const auto &elements =
            static_cast<const CoreIrStructType *>(type)->get_element_types();
        for (const CoreIrType *element_type : elements) {
            if (contains_float_leaf_type(element_type)) {
                return true;
            }
        }
        return false;
    }
    case CoreIrTypeKind::Void:
    case CoreIrTypeKind::Integer:
    case CoreIrTypeKind::Vector:
    case CoreIrTypeKind::Function:
        return false;
    }
    return false;
}

bool function_has_float_aggregate_pointer_parameter(const CoreIrFunction &function) {
    for (const auto &parameter : function.get_parameters()) {
        if (parameter == nullptr) {
            continue;
        }
        const auto *pointer_type =
            dynamic_cast<const CoreIrPointerType *>(parameter->get_type());
        if (pointer_type == nullptr) {
            continue;
        }
        const CoreIrType *pointee_type = pointer_type->get_pointee_type();
        if (pointee_type == nullptr) {
            continue;
        }
        if ((pointee_type->get_kind() == CoreIrTypeKind::Array ||
             pointee_type->get_kind() == CoreIrTypeKind::Struct) &&
            contains_float_leaf_type(pointee_type)) {
            return true;
        }
    }
    return false;
}

bool should_bypass_post_mem2reg_llvm_lane(const CompilerContext &context) {
    const CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    const CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return false;
    }
    for (const auto &function : module->get_functions()) {
        if (function != nullptr &&
            function_has_float_aggregate_pointer_parameter(*function)) {
            return true;
        }
    }
    return false;
}

void maybe_dump_core_ir_before_stop(CompilerContext &context) {
    if (context.get_stop_after_stage() != StopAfterStage::CoreIr ||
        !context.get_dump_core_ir()) {
        return;
    }

    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return;
    }

    const std::filesystem::path output_dir("build/intermediate_results");
    std::filesystem::create_directories(output_dir);
    const std::filesystem::path input_path(context.get_input_file());
    const std::filesystem::path output_file =
        output_dir / (input_path.stem().string() + ".core-ir.txt");
    std::ofstream ofs(output_file);
    if (!ofs.is_open()) {
        return;
    }

    CoreIrRawPrinter printer;
    ofs << printer.print_module(*module);
    context.set_core_ir_dump_file_path(output_file.string());
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
        build_result == nullptr ? nullptr
                                : build_result->get_analysis_manager();
    if (analysis_manager == nullptr || !effects.has_changes()) {
        return;
    }
    if (effects.module_changed) {
        analysis_manager->invalidate_all();
        return;
    }

    constexpr CoreIrAnalysisKind k_module_analysis_kinds[] = {
        CoreIrAnalysisKind::CallGraph,
        CoreIrAnalysisKind::FunctionAttrs,
        CoreIrAnalysisKind::EscapeAnalysis,
        CoreIrAnalysisKind::BlockFrequencyLite,
        CoreIrAnalysisKind::TargetTransformInfoLite,
    };
    for (CoreIrAnalysisKind kind : k_module_analysis_kinds) {
        if (!effects.preserved_analyses.preserves(kind)) {
            analysis_manager->invalidate(kind);
        }
    }

    constexpr CoreIrAnalysisKind k_all_analysis_kinds[] = {
        CoreIrAnalysisKind::Cfg,
        CoreIrAnalysisKind::DominatorTree,
        CoreIrAnalysisKind::DominanceFrontier,
        CoreIrAnalysisKind::PromotableStackSlot,
        CoreIrAnalysisKind::LoopInfo,
        CoreIrAnalysisKind::InductionVar,
        CoreIrAnalysisKind::ScalarEvolutionLite,
        CoreIrAnalysisKind::AliasAnalysis,
        CoreIrAnalysisKind::MemorySSA,
        CoreIrAnalysisKind::FunctionEffectSummary,
    };

    for (CoreIrFunction *function : collect_changed_functions(effects)) {
        if (function == nullptr) {
            continue;
        }
        const bool cfg_changed = effects.cfg_changed_functions.find(function) !=
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
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            std::string(pass.Name()) +
                " requested Core IR verification but no Core IR is available");
        return false;
    }

    CoreIrVerifier verifier;
    if (metadata.produces_core_ir) {
        return emit_core_ir_verify_result(
            context, verifier.verify_module(*module), pass.Name());
    }

    if (result.core_ir_effects.has_value() &&
        result.core_ir_effects->has_changes()) {
        if (result.core_ir_effects->module_changed ||
            collect_changed_functions(*result.core_ir_effects).empty()) {
            return emit_core_ir_verify_result(
                context, verifier.verify_module(*module), pass.Name());
        }
        for (CoreIrFunction *function :
             collect_changed_functions(*result.core_ir_effects)) {
            if (function == nullptr) {
                continue;
            }
            if (!emit_core_ir_verify_result(context,
                                            verifier.verify_function(*function),
                                            pass.Name())) {
                return false;
            }
        }
        return true;
    }

    return emit_core_ir_verify_result(context, verifier.verify_module(*module),
                                      pass.Name());
}

struct PassExecutionResult {
    PassResult result = PassResult::Success();
    bool changed = false;
    bool stopped = false;
};

PassExecutionResult run_one_pass(CompilerContext &context, Pass &pass) {
    PassExecutionResult execution_result;

    const bool trace = trace_passes_enabled();
    const auto start_time = std::chrono::steady_clock::now();
    if (trace) {
        trace_pass_event("enter", pass, count_core_ir_blocks(context));
    }

    PassResult result = pass.Run(context);
    if (!result.ok) {
        execution_result.result = std::move(result);
        if (trace) {
            trace_pass_leave(pass, false, false, count_core_ir_blocks(context),
                             std::chrono::steady_clock::now() - start_time);
        }
        return execution_result;
    }
    if (context.get_diagnostic_engine().has_error()) {
        execution_result.result = PassResult::Failure(
            first_error_message(context.get_diagnostic_engine()));
        if (trace) {
            trace_pass_leave(pass, false, false, count_core_ir_blocks(context),
                             std::chrono::steady_clock::now() - start_time);
        }
        return execution_result;
    }
    if (pass.Metadata().writes_core_ir && !result.core_ir_effects.has_value()) {
        execution_result.result = PassResult::Failure(
            std::string(pass.Name()) +
            " wrote Core IR but did not report CoreIrPassEffects");
        if (trace) {
            trace_pass_leave(pass, false, false, count_core_ir_blocks(context),
                             std::chrono::steady_clock::now() - start_time);
        }
        return execution_result;
    }
    if (result.core_ir_effects.has_value()) {
        execution_result.changed = result.core_ir_effects->has_changes();
        invalidate_non_preserved_analyses(context, *result.core_ir_effects);
    }
    if (!verify_core_ir_after_pass(context, pass, result)) {
        execution_result.result = PassResult::Failure(
            std::string(pass.Name()) + " failed Core IR verification");
        if (trace) {
            trace_pass_leave(pass, execution_result.changed, false,
                             count_core_ir_blocks(context),
                             std::chrono::steady_clock::now() - start_time);
        }
        return execution_result;
    }
    if (should_stop_after_pass(context, pass.Kind())) {
        maybe_dump_core_ir_before_stop(context);
        execution_result.stopped = true;
    }
    if (trace) {
        trace_pass_leave(pass, execution_result.changed, execution_result.stopped,
                         count_core_ir_blocks(context),
                         std::chrono::steady_clock::now() - start_time);
    }
    execution_result.result = PassResult::Success();
    return execution_result;
}

} // namespace

void PassManager::AddPass(std::unique_ptr<Pass> pass) {
    if (pass == nullptr) {
        return;
    }

    PipelineEntry entry;
    entry.pass = std::move(pass);
    entries_.push_back(std::move(entry));
}

void PassManager::AddCoreIrFixedPointGroup(
    std::vector<std::unique_ptr<Pass>> passes, std::size_t max_iterations) {
    FixedPointPassGroup group;
    for (std::unique_ptr<Pass> &pass : passes) {
        if (pass != nullptr) {
            group.passes.push_back(std::move(pass));
        }
    }
    if (group.passes.empty()) {
        return;
    }
    if (max_iterations == 0) {
        max_iterations = 1;
    }
    group.max_iterations = max_iterations;

    PipelineEntry entry;
    entry.fixed_point_group = std::move(group);
    entries_.push_back(std::move(entry));
}

void PassManager::AddCoreIrModuleFixedPointGroup(
    std::vector<std::unique_ptr<Pass>> passes, std::size_t max_iterations) {
    FixedPointPassGroup group;
    for (std::unique_ptr<Pass> &pass : passes) {
        if (pass != nullptr) {
            group.passes.push_back(std::move(pass));
        }
    }
    if (group.passes.empty()) {
        return;
    }
    if (max_iterations == 0) {
        max_iterations = 1;
    }
    group.max_iterations = max_iterations;
    group.module_scope = true;

    PipelineEntry entry;
    entry.fixed_point_group = std::move(group);
    entries_.push_back(std::move(entry));
}

Pass *PassManager::get_pass_by_kind(PassKind kind) const {
    for (const PipelineEntry &entry : entries_) {
        if (entry.pass != nullptr && entry.pass->Kind() == kind) {
            return entry.pass.get();
        }
        if (!entry.fixed_point_group.has_value()) {
            continue;
        }
        for (const std::unique_ptr<Pass> &pass :
             entry.fixed_point_group->passes) {
            if (pass != nullptr && pass->Kind() == kind) {
                return pass.get();
            }
        }
    }

    return nullptr;
}

std::vector<PassKind> PassManager::get_pipeline_kinds() const {
    std::vector<PassKind> kinds;
    for (const PipelineEntry &entry : entries_) {
        if (entry.pass != nullptr) {
            kinds.push_back(entry.pass->Kind());
            continue;
        }
        if (!entry.fixed_point_group.has_value()) {
            continue;
        }
        for (const std::unique_ptr<Pass> &pass :
             entry.fixed_point_group->passes) {
            if (pass != nullptr) {
                kinds.push_back(pass->Kind());
            }
        }
    }
    return kinds;
}

PassResult PassManager::Run(CompilerContext &context) {
    bool bypass_post_mem2reg_llvm_lane = false;
    for (const PipelineEntry &entry : entries_) {
        if (entry.pass != nullptr) {
            if (bypass_post_mem2reg_llvm_lane &&
                entry.pass->Kind() != PassKind::LowerIr &&
                entry.pass->Kind() != PassKind::CodeGen) {
                continue;
            }
            PassExecutionResult execution_result =
                run_one_pass(context, *entry.pass);
            if (!execution_result.result.ok) {
                return execution_result.result;
            }
            if (execution_result.stopped) {
                return PassResult::Success();
            }
            if (entry.pass->Kind() == PassKind::CoreIrMem2Reg &&
                should_bypass_post_mem2reg_llvm_lane(context)) {
                bypass_post_mem2reg_llvm_lane = true;
            }
            continue;
        }

        if (!entry.fixed_point_group.has_value()) {
            return PassResult::Failure("encountered empty pipeline entry");
        }

        if (bypass_post_mem2reg_llvm_lane) {
            continue;
        }
        if (!should_run_fixed_point_group(context)) {
            continue;
        }

        const FixedPointPassGroup &group = *entry.fixed_point_group;
        const bool trace = trace_passes_enabled();
        if (trace) {
            std::cerr << "[sysycc-pass] fixed-point begin scope="
                      << (group.module_scope ? "module" : "function")
                      << " passes=" << group.passes.size()
                      << " max_iterations=" << group.max_iterations << '\n';
        }
        std::size_t iterations_run = 0;
        bool converged = false;
        for (std::size_t iteration = 0; iteration < group.max_iterations;
             ++iteration) {
            iterations_run = iteration + 1;
            bool iteration_changed = false;
            if (trace) {
                std::cerr << "[sysycc-pass] fixed-point iteration "
                          << (iteration + 1) << '/' << group.max_iterations
                          << " begin\n";
            }
            for (const std::unique_ptr<Pass> &pass : group.passes) {
                if (pass == nullptr) {
                    return PassResult::Failure(
                        "encountered null pass in fixed-point group");
                }

                PassExecutionResult execution_result =
                    run_one_pass(context, *pass);
                if (!execution_result.result.ok) {
                    return execution_result.result;
                }
                if (execution_result.stopped) {
                    return PassResult::Success();
                }
                iteration_changed =
                    execution_result.changed || iteration_changed;
            }
            if (trace) {
                std::cerr << "[sysycc-pass] fixed-point iteration "
                          << (iteration + 1) << '/' << group.max_iterations
                          << " changed=" << (iteration_changed ? 1 : 0)
                          << '\n';
            }
            if (!iteration_changed) {
                converged = true;
                break;
            }
        }
        if (trace) {
            std::cerr << "[sysycc-pass] fixed-point end iterations="
                      << iterations_run
                      << " converged=" << (converged ? 1 : 0) << '\n';
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
