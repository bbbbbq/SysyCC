#include "pass.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

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
        return pass_kind == PassKind::CodeGen &&
               (context.get_asm_result() != nullptr ||
                context.get_object_result() != nullptr);
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
    return text == "1" || text == "true" || text == "TRUE" || text == "yes" ||
           text == "YES" || text == "on" || text == "ON";
}

bool trace_passes_enabled() { return env_flag_enabled("SYSYCC_TRACE_PASSES"); }

std::string pass_report_dir() {
    const char *value = std::getenv("SYSYCC_PASS_REPORT_DIR");
    return value == nullptr ? std::string{} : std::string(value);
}

bool pass_report_enabled() {
    return trace_passes_enabled() || !pass_report_dir().empty();
}

bool force_core_ir_verification_enabled() {
    return env_flag_enabled("SYSYCC_VERIFY_CORE_IR");
}

struct CoreIrStats {
    std::size_t functions = 0;
    std::size_t blocks = 0;
    std::size_t instructions = 0;
};

CoreIrStats get_core_ir_stats(const CompilerContext &context) {
    const CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    const CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return {};
    }

    CoreIrStats stats;
    for (const auto &function : module->get_functions()) {
        if (function == nullptr) {
            continue;
        }
        ++stats.functions;
        stats.blocks += function->get_basic_blocks().size();
        for (const auto &block : function->get_basic_blocks()) {
            if (block != nullptr) {
                stats.instructions += block->get_instructions().size();
            }
        }
    }
    return stats;
}

std::size_t count_core_ir_blocks(const CompilerContext &context) {
    return get_core_ir_stats(context).blocks;
}

long long diff_count(std::size_t after, std::size_t before) {
    return static_cast<long long>(after) - static_cast<long long>(before);
}

std::string format_delta(long long delta) {
    std::ostringstream oss;
    if (delta >= 0) {
        oss << '+';
    }
    oss << delta;
    return oss.str();
}

std::string sanitize_report_filename(std::string name) {
    if (name.empty()) {
        return "tu";
    }
    for (char &ch : name) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (!std::isalnum(value) && ch != '.' && ch != '_' && ch != '-') {
            ch = '_';
        }
    }
    return name;
}

std::string pass_report_file_name(const CompilerContext &context) {
    const std::filesystem::path input_path(context.get_input_file());
    std::string base = input_path.filename().string();
    if (base.empty()) {
        base = "tu";
    }
    const std::size_t hash = std::hash<std::string>{}(context.get_input_file());
    std::ostringstream oss;
    oss << sanitize_report_filename(base) << '-' << std::hex << hash << ".md";
    return oss.str();
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
              << " stopped=" << (stopped ? 1 : 0) << " blocks=" << block_count
              << " elapsed_ms=" << elapsed_ms << '\n';
}

class PassProfileReport {
  private:
    struct PassEvent {
        std::size_t sequence = 0;
        std::string pass_name;
        std::optional<std::size_t> fixed_point_group;
        std::optional<std::size_t> fixed_point_iteration;
        CoreIrStats before;
        CoreIrStats after;
        std::chrono::steady_clock::duration elapsed{};
        bool changed = false;
        bool stopped = false;
    };

    struct FixedPointGroupEvent {
        std::size_t index = 0;
        std::string scope;
        std::size_t pass_count = 0;
        std::size_t max_iterations = 0;
        std::size_t iterations_run = 0;
        bool converged = false;
        std::vector<bool> iteration_changed;
    };

    struct PassAggregate {
        std::string pass_name;
        std::chrono::steady_clock::duration elapsed{};
        std::size_t runs = 0;
        std::size_t changed_runs = 0;
        long long block_delta = 0;
        long long instruction_delta = 0;
    };

    bool enabled_ = false;
    std::string input_file_;
    std::chrono::steady_clock::time_point start_time_;
    std::vector<PassEvent> pass_events_;
    std::vector<FixedPointGroupEvent> fixed_point_groups_;

    static double to_ms(std::chrono::steady_clock::duration duration) {
        return std::chrono::duration<double, std::milli>(duration).count();
    }

    std::vector<PassAggregate> aggregate_passes() const {
        std::vector<PassAggregate> aggregates;
        std::unordered_map<std::string, std::size_t> index_by_name;
        for (const PassEvent &event : pass_events_) {
            auto found = index_by_name.find(event.pass_name);
            if (found == index_by_name.end()) {
                index_by_name.emplace(event.pass_name, aggregates.size());
                aggregates.push_back(PassAggregate{event.pass_name});
                found = index_by_name.find(event.pass_name);
            }
            PassAggregate &aggregate = aggregates[found->second];
            aggregate.elapsed += event.elapsed;
            ++aggregate.runs;
            if (event.changed) {
                ++aggregate.changed_runs;
            }
            aggregate.block_delta +=
                diff_count(event.after.blocks, event.before.blocks);
            aggregate.instruction_delta +=
                diff_count(event.after.instructions, event.before.instructions);
        }
        std::sort(aggregates.begin(), aggregates.end(),
                  [](const PassAggregate &lhs, const PassAggregate &rhs) {
                      if (lhs.elapsed != rhs.elapsed) {
                          return lhs.elapsed > rhs.elapsed;
                      }
                      return lhs.pass_name < rhs.pass_name;
                  });
        return aggregates;
    }

    std::string render() const {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "# SysyCC Pass Trace Report\n\n";
        oss << "- Input: `" << (input_file_.empty() ? "<unknown>" : input_file_)
            << "`\n";
        oss << "- Pipeline wall time: "
            << to_ms(std::chrono::steady_clock::now() - start_time_) << " ms\n";
        oss << "- Pass invocations: " << pass_events_.size() << "\n\n";

        const std::vector<PassAggregate> aggregates = aggregate_passes();
        oss << "## Top 10 Slow Passes\n\n";
        oss << "| Rank | Pass | Total ms | Runs | Changed runs | Blocks delta "
               "| "
               "Instructions delta |\n";
        oss << "| ---: | --- | ---: | ---: | ---: | ---: | ---: |\n";
        const std::size_t limit = std::min<std::size_t>(10, aggregates.size());
        for (std::size_t index = 0; index < limit; ++index) {
            const PassAggregate &aggregate = aggregates[index];
            oss << "| " << (index + 1) << " | `" << aggregate.pass_name
                << "` | " << to_ms(aggregate.elapsed) << " | " << aggregate.runs
                << " | " << aggregate.changed_runs << " | "
                << format_delta(aggregate.block_delta) << " | "
                << format_delta(aggregate.instruction_delta) << " |\n";
        }
        oss << '\n';

        oss << "## Fixed-Point Groups\n\n";
        oss << "| Group | Scope | Passes | Iterations | Max | Converged | "
               "Changed "
               "iterations |\n";
        oss << "| ---: | --- | ---: | ---: | ---: | --- | --- |\n";
        if (fixed_point_groups_.empty()) {
            oss << "| - | - | - | - | - | - | - |\n";
        } else {
            for (const FixedPointGroupEvent &group : fixed_point_groups_) {
                std::ostringstream changed_iterations;
                for (std::size_t index = 0;
                     index < group.iteration_changed.size(); ++index) {
                    if (index != 0) {
                        changed_iterations << ',';
                    }
                    changed_iterations
                        << (group.iteration_changed[index] ? '1' : '0');
                }
                oss << "| " << group.index << " | " << group.scope << " | "
                    << group.pass_count << " | " << group.iterations_run
                    << " | " << group.max_iterations << " | "
                    << (group.converged ? "yes" : "no") << " | "
                    << (changed_iterations.str().empty()
                            ? "-"
                            : changed_iterations.str())
                    << " |\n";
            }
        }
        oss << '\n';

        oss << "## Pass Timeline\n\n";
        oss << "| # | Pass | Fixed point | ms | Changed | Stopped | Blocks | "
               "Instructions |\n";
        oss << "| ---: | --- | --- | ---: | ---: | ---: | --- | --- |\n";
        for (const PassEvent &event : pass_events_) {
            std::string fixed_point = "-";
            if (event.fixed_point_group.has_value() &&
                event.fixed_point_iteration.has_value()) {
                std::ostringstream fixed_point_oss;
                fixed_point_oss << "group " << *event.fixed_point_group
                                << " iter " << *event.fixed_point_iteration;
                fixed_point = fixed_point_oss.str();
            }
            const long long block_delta =
                diff_count(event.after.blocks, event.before.blocks);
            const long long instruction_delta =
                diff_count(event.after.instructions, event.before.instructions);
            oss << "| " << event.sequence << " | `" << event.pass_name << "` | "
                << fixed_point << " | " << to_ms(event.elapsed) << " | "
                << (event.changed ? 1 : 0) << " | " << (event.stopped ? 1 : 0)
                << " | " << event.before.blocks << " -> " << event.after.blocks
                << " (" << format_delta(block_delta) << ") | "
                << event.before.instructions << " -> "
                << event.after.instructions << " ("
                << format_delta(instruction_delta) << ") |\n";
        }
        return oss.str();
    }

  public:
    explicit PassProfileReport(const CompilerContext &context)
        : enabled_(pass_report_enabled()),
          input_file_(context.get_input_file()),
          start_time_(std::chrono::steady_clock::now()) {}

    bool enabled() const noexcept { return enabled_; }

    void record_pass(const Pass &pass,
                     std::optional<std::size_t> fixed_point_group,
                     std::optional<std::size_t> fixed_point_iteration,
                     CoreIrStats before, CoreIrStats after,
                     std::chrono::steady_clock::duration elapsed, bool changed,
                     bool stopped) {
        if (!enabled_) {
            return;
        }
        PassEvent event;
        event.sequence = pass_events_.size() + 1;
        event.pass_name = pass.Name();
        event.fixed_point_group = fixed_point_group;
        event.fixed_point_iteration = fixed_point_iteration;
        event.before = before;
        event.after = after;
        event.elapsed = elapsed;
        event.changed = changed;
        event.stopped = stopped;
        pass_events_.push_back(std::move(event));
    }

    void begin_fixed_point_group(std::size_t index, bool module_scope,
                                 std::size_t pass_count,
                                 std::size_t max_iterations) {
        if (!enabled_) {
            return;
        }
        FixedPointGroupEvent group;
        group.index = index;
        group.scope = module_scope ? "module" : "function";
        group.pass_count = pass_count;
        group.max_iterations = max_iterations;
        fixed_point_groups_.push_back(std::move(group));
    }

    void record_fixed_point_iteration(std::size_t index, bool changed) {
        if (!enabled_) {
            return;
        }
        for (FixedPointGroupEvent &group : fixed_point_groups_) {
            if (group.index == index) {
                group.iteration_changed.push_back(changed);
                group.iterations_run = group.iteration_changed.size();
                return;
            }
        }
    }

    void finish_fixed_point_group(std::size_t index, std::size_t iterations_run,
                                  bool converged) {
        if (!enabled_) {
            return;
        }
        for (FixedPointGroupEvent &group : fixed_point_groups_) {
            if (group.index == index) {
                group.iterations_run = iterations_run;
                group.converged = converged;
                return;
            }
        }
    }

    void emit(const CompilerContext &context) const {
        if (!enabled_ || pass_events_.empty()) {
            return;
        }

        const std::string report = render();
        if (trace_passes_enabled()) {
            std::cerr << report;
            if (report.empty() || report.back() != '\n') {
                std::cerr << '\n';
            }
        }

        const std::string output_dir = pass_report_dir();
        if (output_dir.empty()) {
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(output_dir, ec);
        if (ec) {
            std::cerr << "[sysycc-pass] failed to create pass report dir `"
                      << output_dir << "`: " << ec.message() << '\n';
            return;
        }
        const std::filesystem::path output_path =
            std::filesystem::path(output_dir) / pass_report_file_name(context);
        std::ofstream ofs(output_path);
        if (!ofs.is_open()) {
            std::cerr << "[sysycc-pass] failed to write pass report `"
                      << output_path.string() << "`\n";
            return;
        }
        ofs << report;
    }
};

bool contains_float_leaf_type(
    const CoreIrType *type, std::unordered_set<const CoreIrType *> &visiting) {
    if (type == nullptr) {
        return false;
    }
    if (!visiting.insert(type).second) {
        return false;
    }
    switch (type->get_kind()) {
    case CoreIrTypeKind::Float:
        return true;
    case CoreIrTypeKind::Pointer:
        return contains_float_leaf_type(
            static_cast<const CoreIrPointerType *>(type)->get_pointee_type(),
            visiting);
    case CoreIrTypeKind::Array:
        return contains_float_leaf_type(
            static_cast<const CoreIrArrayType *>(type)->get_element_type(),
            visiting);
    case CoreIrTypeKind::Struct: {
        const auto &elements =
            static_cast<const CoreIrStructType *>(type)->get_element_types();
        for (const CoreIrType *element_type : elements) {
            if (contains_float_leaf_type(element_type, visiting)) {
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

bool contains_float_leaf_type(const CoreIrType *type) {
    std::unordered_set<const CoreIrType *> visiting;
    return contains_float_leaf_type(type, visiting);
}

bool function_has_float_aggregate_pointer_parameter(
    const CoreIrFunction &function) {
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
    constexpr std::size_t kDefaultCoreIrVerifyBlockLimit = 500;
    if (!force_core_ir_verification_enabled() &&
        count_core_ir_blocks(context) > kDefaultCoreIrVerifyBlockLimit) {
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
        !result.core_ir_effects->has_changes()) {
        return true;
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

PassExecutionResult
run_one_pass(CompilerContext &context, Pass &pass,
             PassProfileReport *profile_report,
             std::optional<std::size_t> fixed_point_group = std::nullopt,
             std::optional<std::size_t> fixed_point_iteration = std::nullopt) {
    PassExecutionResult execution_result;

    const bool trace = trace_passes_enabled();
    const CoreIrStats before_stats = get_core_ir_stats(context);
    const auto start_time = std::chrono::steady_clock::now();
    if (trace) {
        trace_pass_event("enter", pass, before_stats.blocks);
    }

    auto finish = [&](bool changed, bool stopped) {
        const auto elapsed = std::chrono::steady_clock::now() - start_time;
        const CoreIrStats after_stats = get_core_ir_stats(context);
        if (profile_report != nullptr) {
            profile_report->record_pass(pass, fixed_point_group,
                                        fixed_point_iteration, before_stats,
                                        after_stats, elapsed, changed, stopped);
        }
        if (trace) {
            trace_pass_leave(pass, changed, stopped, after_stats.blocks,
                             elapsed);
        }
    };

    PassResult result = pass.Run(context);
    if (!result.ok) {
        execution_result.result = std::move(result);
        finish(false, false);
        return execution_result;
    }
    if (context.get_diagnostic_engine().has_error()) {
        execution_result.result = PassResult::Failure(
            first_error_message(context.get_diagnostic_engine()));
        finish(false, false);
        return execution_result;
    }
    if (pass.Metadata().writes_core_ir && !result.core_ir_effects.has_value()) {
        execution_result.result = PassResult::Failure(
            std::string(pass.Name()) +
            " wrote Core IR but did not report CoreIrPassEffects");
        finish(false, false);
        return execution_result;
    }
    if (result.core_ir_effects.has_value()) {
        execution_result.changed = result.core_ir_effects->has_changes();
        invalidate_non_preserved_analyses(context, *result.core_ir_effects);
    }
    if (!verify_core_ir_after_pass(context, pass, result)) {
        execution_result.result = PassResult::Failure(
            std::string(pass.Name()) + " failed Core IR verification");
        finish(execution_result.changed, false);
        return execution_result;
    }
    if (should_stop_after_pass(context, pass.Kind())) {
        maybe_dump_core_ir_before_stop(context);
        execution_result.stopped = true;
    }
    finish(execution_result.changed, execution_result.stopped);
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
    PassProfileReport profile_report(context);
    auto finish = [&](PassResult result) {
        profile_report.emit(context);
        return result;
    };

    bool bypass_post_mem2reg_llvm_lane = false;
    std::size_t fixed_point_group_index = 0;
    for (const PipelineEntry &entry : entries_) {
        if (entry.pass != nullptr) {
            if (bypass_post_mem2reg_llvm_lane &&
                entry.pass->Kind() != PassKind::LowerIr &&
                entry.pass->Kind() != PassKind::CodeGen) {
                continue;
            }
            PassExecutionResult execution_result =
                run_one_pass(context, *entry.pass, &profile_report);
            if (!execution_result.result.ok) {
                return finish(std::move(execution_result.result));
            }
            if (execution_result.stopped) {
                return finish(PassResult::Success());
            }
            if (entry.pass->Kind() == PassKind::CoreIrMem2Reg &&
                should_bypass_post_mem2reg_llvm_lane(context)) {
                bypass_post_mem2reg_llvm_lane = true;
            }
            continue;
        }

        if (!entry.fixed_point_group.has_value()) {
            return finish(
                PassResult::Failure("encountered empty pipeline entry"));
        }

        if (bypass_post_mem2reg_llvm_lane) {
            continue;
        }
        if (!should_run_fixed_point_group(context)) {
            continue;
        }

        const FixedPointPassGroup &group = *entry.fixed_point_group;
        ++fixed_point_group_index;
        profile_report.begin_fixed_point_group(
            fixed_point_group_index, group.module_scope, group.passes.size(),
            group.max_iterations);
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
                    return finish(PassResult::Failure(
                        "encountered null pass in fixed-point group"));
                }

                PassExecutionResult execution_result =
                    run_one_pass(context, *pass, &profile_report,
                                 fixed_point_group_index, iteration + 1);
                if (!execution_result.result.ok) {
                    return finish(std::move(execution_result.result));
                }
                if (execution_result.stopped) {
                    return finish(PassResult::Success());
                }
                iteration_changed =
                    execution_result.changed || iteration_changed;
            }
            profile_report.record_fixed_point_iteration(fixed_point_group_index,
                                                        iteration_changed);
            if (trace) {
                std::cerr << "[sysycc-pass] fixed-point iteration "
                          << (iteration + 1) << '/' << group.max_iterations
                          << " changed=" << (iteration_changed ? 1 : 0) << '\n';
            }
            if (!iteration_changed) {
                converged = true;
                break;
            }
        }
        profile_report.finish_fixed_point_group(fixed_point_group_index,
                                                iterations_run, converged);
        if (trace) {
            std::cerr << "[sysycc-pass] fixed-point end iterations="
                      << iterations_run << " converged=" << (converged ? 1 : 0)
                      << '\n';
        }
    }

    return finish(PassResult::Success());
}

} // namespace sysycc
