#pragma once
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "backend/asm_gen/backend_kind.hpp"
#include "compiler/complier_option.hpp"

namespace ClI {
// Parses command line arguments and translates them into compiler options.
class Cli {
  private:
    std::string program_name_ = "compiler";
    std::string input_file_;
    std::string output_file_;
    std::vector<std::string> include_directories_;
    std::vector<std::string> system_include_directories_;
    std::vector<sysycc::CommandLineMacroOption> command_line_macro_options_;
    std::vector<std::string> forced_include_files_;
    bool no_stdinc_ = false;
    bool dump_tokens_ = false;
    bool dump_parse_ = false;
    bool dump_ast_ = false;
    bool dump_ir_ = false;
    bool dump_core_ir_ = false;
    bool emit_asm_ = false;
    sysycc::StopAfterStage stop_after_stage_ = sysycc::StopAfterStage::None;
    bool explicit_stop_after_ = false;
    bool internal_pipeline_requested_ = false;
    sysycc::DriverAction driver_action_ = sysycc::DriverAction::InternalPipeline;
    sysycc::LanguageMode language_mode_ = sysycc::LanguageMode::Sysy;
    sysycc::OptimizationLevel optimization_level_ =
        sysycc::OptimizationLevel::O0;
    bool explicit_optimization_level_ = false;
    bool enable_gnu_dialect_ = true;
    bool enable_clang_dialect_ = true;
    bool enable_builtin_type_extension_pack_ = true;
    bool verbose_ = false;
    sysycc::WarningPolicy warning_policy_;
    sysycc::BackendKind backend_kind_ = sysycc::BackendKind::LlvmIr;
    bool explicit_backend_ = false;
    std::string target_triple_;
    bool request_preprocess_only_ = false;
    bool request_syntax_only_ = false;
    bool request_emit_assembly_ = false;
    bool request_emit_llvm_ = false;
    bool request_compile_only_ = false;
    bool request_position_independent_ = false;
    bool request_debug_info_ = false;
    bool is_help_ = false;
    bool is_version_ = false;
    bool has_error_ = false;
    std::string version_ = "0.1.0";

    void emit_error(const std::string &message) {
        has_error_ = true;
        std::cerr << program_name_ << ": error: " << message << '\n';
    }

    bool finalize_driver_mode();

  public:
    void Run(int argc, char *argv[]);
    bool get_is_help() const noexcept { return is_help_; }
    bool get_is_version() const noexcept { return is_version_; }
    bool get_has_error() const noexcept { return has_error_; }
    bool has_input_file() const noexcept { return !input_file_.empty(); }
    const std::string &get_program_name() const noexcept { return program_name_; }
    const std::string &get_version() const noexcept { return version_; }

    void PrintHelp() const {
        std::cout << "Usage: " << program_name_ << " [options] <input_file>\n"
                  << "Options:\n"
                  << "  -E                 Preprocess only; write to stdout or -o\n"
                  << "  -fsyntax-only      Run through semantic analysis only\n"
                  << "  -S                 Emit assembly output\n"
                  << "  -emit-llvm         Emit LLVM IR (requires -S)\n"
                  << "  -O0                Disable optional Core IR optimization passes\n"
                  << "  -O1                Enable current Core IR optimization passes\n"
                  << "  -c                 Emit object output\n"
                  << "  -fPIC              Emit position-independent AArch64 code\n"
                  << "  -g                 Emit basic debug information on native AArch64 outputs\n"
                  << "  -I<dir>            Add include search directory\n"
                  << "  -I <dir>           Add include search directory\n"
                  << "  -isystem <dir>     Add system include search directory\n"
                  << "  -D<name>[=value]   Define a preprocessor macro\n"
                  << "  -D <name>[=value]  Define a preprocessor macro\n"
                  << "  -U<name>           Undefine a preprocessor macro\n"
                  << "  -U <name>          Undefine a preprocessor macro\n"
                  << "  -include <file>    Force include a file before the main input\n"
                  << "  -nostdinc          Disable default system include directories\n"
                  << "  -o <output_file>   Specify output file\n"
                  << "  -std=<mode>        Select language mode (c99, gnu99, sysy)\n"
                  << "  -fgnu-extensions   Enable GNU dialect extensions\n"
                  << "  -fno-gnu-extensions Disable GNU dialect extensions\n"
                  << "  -fclang-extensions Enable Clang dialect extensions\n"
                  << "  -fno-clang-extensions Disable Clang dialect extensions\n"
                  << "  -fbuiltin-types    Enable builtin type extensions\n"
                  << "  -fno-builtin-types Disable builtin type extensions\n"
                  << "  -Wall              Enable stable frontend warnings\n"
                  << "  -Wextra            Enable additional conservative warnings\n"
                  << "  -Werror            Promote warnings to errors\n"
                  << "  -Wno-error         Keep warnings as warnings\n"
                  << "  -W<name>           Enable a named warning\n"
                  << "  -Wno-<name>        Disable a named warning\n"
                  << "  -Werror=<name>     Promote a named warning to an error\n"
                  << "  -Wno-error=<name>  Keep a named warning as a warning\n"
                  << "  -h, --help         Show this help message and exit\n"
                  << "  --version          Show version information and exit\n"
                  << "  -v                 Show verbose driver configuration while compiling\n";
    }
    void PrintVersion() {
        std::cout << program_name_ << " version " << version_ << '\n';
    }

    void set_compiler_option(sysycc::ComplierOption &option) const {
        option.set_input_file(input_file_);
        option.set_output_file(output_file_);
        option.set_include_directories(include_directories_);
        std::vector<std::string> merged_system_include_directories;
        if (!no_stdinc_) {
            merged_system_include_directories =
                option.get_system_include_directories();
        }
        merged_system_include_directories.insert(
            merged_system_include_directories.begin(),
            system_include_directories_.begin(),
            system_include_directories_.end());
        option.set_system_include_directories(
            std::move(merged_system_include_directories));
        option.set_command_line_macro_options(command_line_macro_options_);
        option.set_forced_include_files(forced_include_files_);
        option.set_no_stdinc(no_stdinc_);
        option.set_dump_tokens(dump_tokens_);
        option.set_dump_parse(dump_parse_);
        option.set_dump_ast(dump_ast_);
        option.set_dump_ir(dump_ir_);
        option.set_dump_core_ir(dump_core_ir_);
        option.set_driver_action(driver_action_);
        option.set_emit_asm(emit_asm_);
        option.set_emit_object(driver_action_ == sysycc::DriverAction::CompileOnly);
        option.set_stop_after_stage(stop_after_stage_);
        option.set_language_mode(language_mode_);
        option.set_optimization_level(optimization_level_);
        option.set_enable_gnu_dialect(enable_gnu_dialect_);
        option.set_enable_clang_dialect(enable_clang_dialect_);
        option.set_enable_builtin_type_extension_pack(
            enable_builtin_type_extension_pack_);
        option.set_verbose(verbose_);
        option.set_warning_policy(warning_policy_);
        sysycc::BackendOptions backend_options;
        backend_options.set_backend_kind(backend_kind_);
        backend_options.set_target_triple(target_triple_);
        if (driver_action_ == sysycc::DriverAction::EmitAssembly ||
            driver_action_ == sysycc::DriverAction::CompileOnly) {
            backend_options.set_output_file(output_file_);
        }
        backend_options.set_position_independent(request_position_independent_);
        backend_options.set_debug_info(request_debug_info_);
        option.set_backend_options(std::move(backend_options));
    }
};
} // namespace ClI
