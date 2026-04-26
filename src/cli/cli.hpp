#pragma once
#include <cstddef>
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
    std::vector<std::string> positional_inputs_;
    std::string input_file_;
    std::vector<std::string> source_input_files_;
    std::vector<std::string> linker_input_files_;
    bool link_only_ = false;
    std::string output_file_;
    sysycc::DepfileMode depfile_mode_ = sysycc::DepfileMode::None;
    std::string depfile_output_file_;
    std::vector<sysycc::DepfileTargetOption> depfile_targets_;
    bool depfile_add_phony_targets_ = false;
    std::vector<std::string> include_directories_;
    std::vector<std::string> quote_include_directories_;
    std::vector<std::string> system_include_directories_;
    std::vector<std::string> after_system_include_directories_;
    std::vector<sysycc::CommandLineMacroOption> command_line_macro_options_;
    std::vector<std::string> forced_include_files_;
    std::string sysroot_;
    std::string isysroot_;
    std::vector<std::string> linker_search_directories_;
    std::vector<std::string> linker_libraries_;
    std::vector<std::string> linker_passthrough_arguments_;
    bool link_with_pthread_ = false;
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
    bool force_c_input_language_ = false;
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
    bool finalize_inputs();

  public:
    void Run(int argc, char *argv[]);
    bool get_is_help() const noexcept { return is_help_; }
    bool get_is_version() const noexcept { return is_version_; }
    bool get_has_error() const noexcept { return has_error_; }
    bool has_input_file() const noexcept {
        return !input_file_.empty() || !linker_input_files_.empty() ||
               !positional_inputs_.empty();
    }
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
                  << "  -x c               Force C input language for following inputs\n"
                  << "  -pipe              Accepted for GCC-like build compatibility; ignored\n"
                  << "  -fPIC              Emit position-independent native code\n"
                  << "  -g                 Forward debug-info requests to native backends\n"
                  << "  -MD                Write a depfile including system headers\n"
                  << "  -MMD               Write a depfile excluding system headers\n"
                  << "  -MF <file>         Write the depfile to <file> (requires -MD/-MMD)\n"
                  << "  -MT <target>       Override depfile target name\n"
                  << "  -MQ <target>       Override depfile target name with Make quoting\n"
                  << "  -MP                Add phony header targets to the depfile (requires -MD/-MMD)\n"
                  << "  -I<dir>            Add include search directory\n"
                  << "  -I <dir>           Add include search directory\n"
                  << "  -iquote <dir>      Add quoted-include-only search directory\n"
                  << "  -isystem <dir>     Add system include search directory\n"
                  << "  -idirafter <dir>   Add late system include search directory\n"
                  << "  --sysroot <dir>    Use a sysroot for headers and host linking\n"
                  << "  -isysroot <dir>    Use a header SDK root for system includes\n"
                  << "  -L<dir>            Add a linker search directory for full-compile linking\n"
                  << "  -l<name>           Link a library during full-compile linking\n"
                  << "  -Wl,<arg>          Pass an argument through to the external linker driver\n"
                  << "  -pthread           Preserve pthread linkage intent for full-compile linking\n"
                  << "  -ffunction-sections Accepted for GCC-like build compatibility; ignored\n"
                  << "  -fdata-sections    Accepted for GCC-like build compatibility; ignored\n"
                  << "  -fno-common        Accepted for GCC-like build compatibility; ignored\n"
                  << "  -fvisibility=hidden Accepted for GCC-like build compatibility; ignored\n"
                  << "  -Winvalid-pch      Accepted for GCC-like build compatibility; ignored\n"
                  << "  -D<name>[=value]   Define a preprocessor macro\n"
                  << "  -D <name>[=value]  Define a preprocessor macro\n"
                  << "  -U<name>           Undefine a preprocessor macro\n"
                  << "  -U <name>          Undefine a preprocessor macro\n"
                  << "  -include <file>    Force include a file before the main input\n"
                  << "  -nostdinc          Disable default system include directories\n"
                  << "  -o <output_file>   Specify output file\n"
                  << "  -std=<mode>        Select language mode (c99, c11, c17, c18, c2x/c23, gnu99, gnu11, gnu17, gnu18, gnu2x/gnu23, sysy)\n"
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
        option.set_source_input_files(source_input_files_);
        option.set_linker_input_files(linker_input_files_);
        option.set_link_only(link_only_);
        option.set_output_file(output_file_);
        option.set_depfile_mode(depfile_mode_);
        option.set_depfile_output_file(depfile_output_file_);
        option.set_depfile_targets(depfile_targets_);
        option.set_depfile_add_phony_targets(depfile_add_phony_targets_);
        option.set_include_directories(include_directories_);
        option.set_quote_include_directories(quote_include_directories_);
        std::vector<std::string> merged_system_include_directories;
        if (!no_stdinc_) {
            merged_system_include_directories =
                option.get_system_include_directories();
        }
        merged_system_include_directories.insert(
            merged_system_include_directories.begin(),
            system_include_directories_.begin(),
            system_include_directories_.end());
        const auto append_sysroot_include_directories =
            [](std::vector<std::string> &directories,
               const std::string &root) {
                if (root.empty()) {
                    return;
                }
                const std::filesystem::path root_path(root);
                directories.push_back((root_path / "usr/local/include").string());
                directories.push_back((root_path / "usr/include").string());
            };
        std::vector<std::string> sysroot_include_directories;
        if (!no_stdinc_) {
            append_sysroot_include_directories(sysroot_include_directories,
                                               sysroot_);
            append_sysroot_include_directories(sysroot_include_directories,
                                               isysroot_);
        }
        merged_system_include_directories.insert(
            merged_system_include_directories.begin() +
                static_cast<std::ptrdiff_t>(system_include_directories_.size()),
            sysroot_include_directories.begin(),
            sysroot_include_directories.end());
        option.set_system_include_directories(
            std::move(merged_system_include_directories));
        option.set_after_system_include_directories(
            after_system_include_directories_);
        option.set_command_line_macro_options(command_line_macro_options_);
        option.set_forced_include_files(forced_include_files_);
        option.set_sysroot(sysroot_);
        option.set_isysroot(isysroot_);
        option.set_linker_search_directories(linker_search_directories_);
        option.set_linker_libraries(linker_libraries_);
        option.set_linker_passthrough_arguments(linker_passthrough_arguments_);
        option.set_link_with_pthread(link_with_pthread_);
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
