#include "compiler.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "backend/asm_gen/aarch64/aarch64_asm_gen_pass.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "backend/asm_gen/object_result.hpp"
#include "backend/asm_gen/riscv64/riscv64_asm_gen_pass.hpp"
#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"
#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/gvn/core_ir_gvn_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/licm/core_ir_licm_pass.hpp"
#include "backend/ir/local_cse/core_ir_local_cse_pass.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/pipeline/core_ir_pass_pipeline.hpp"
#include "backend/ir/sccp/core_ir_sccp_pass.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"
#include "backend/ir/lower/lower_ir_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "compiler/pass/pass.hpp"
#include "frontend/ast/ast_pass.hpp"
#include "frontend/dialects/core/dialect.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/preprocess/preprocess.hpp"
#include "frontend/semantic/semantic_pass.hpp"

extern char **environ;

namespace sysycc {

namespace {

bool is_native_backend(BackendKind backend_kind) {
    return backend_kind == BackendKind::AArch64Native ||
           backend_kind == BackendKind::Riscv64Native;
}

const char *backend_kind_name(BackendKind backend_kind) {
    switch (backend_kind) {
    case BackendKind::LlvmIr:
        return "llvm-ir";
    case BackendKind::AArch64Native:
        return "aarch64-native";
    case BackendKind::Riscv64Native:
        return "riscv64-native";
    }
    return "unknown";
}

const char *expected_target_triple(BackendKind backend_kind) {
    switch (backend_kind) {
    case BackendKind::AArch64Native:
        return "aarch64-unknown-linux-gnu";
    case BackendKind::Riscv64Native:
        return "riscv64-unknown-linux-gnu";
    case BackendKind::LlvmIr:
        return "";
    }
    return "";
}

std::string default_output_file_for_action(const CompilerOption &option) {
    const std::filesystem::path input_path(option.get_input_file());
    switch (option.get_driver_action()) {
    case DriverAction::FullCompile:
        return "a.out";
    case DriverAction::EmitLlvmIr:
        return input_path.stem().string() + ".ll";
    case DriverAction::EmitAssembly:
        return input_path.stem().string() + ".s";
    case DriverAction::CompileOnly:
        return input_path.stem().string() + ".o";
    default:
        return option.get_output_file();
    }
}

std::string effective_primary_output_file(const CompilerOption &option) {
    return option.get_output_file().empty()
               ? default_output_file_for_action(option)
               : option.get_output_file();
}

std::string effective_depfile_output_file(const CompilerOption &option) {
    if (!option.get_depfile_output_file().empty()) {
        return option.get_depfile_output_file();
    }

    std::filesystem::path depfile_path(effective_primary_output_file(option));
    depfile_path.replace_extension(".d");
    return depfile_path.string();
}

std::string dependency_scanner_language_flag(const CompilerOption &option) {
    switch (option.get_language_mode()) {
    case LanguageMode::C99:
        return "-std=c99";
    case LanguageMode::C11:
        return "-std=c11";
    case LanguageMode::C17:
        return "-std=c17";
    case LanguageMode::C2x:
        return "-std=c2x";
    case LanguageMode::Gnu99:
        return "-std=gnu99";
    case LanguageMode::Gnu11:
        return "-std=gnu11";
    case LanguageMode::Gnu17:
        return "-std=gnu17";
    case LanguageMode::Gnu2x:
        return "-std=gnu2x";
    case LanguageMode::Sysy:
        return "";
    }
    return "";
}

void append_command_line_macro_argument(
    std::vector<std::string> &arguments,
    const CommandLineMacroOption &macro_option) {
    if (macro_option.get_action_kind() == CommandLineMacroActionKind::Undefine) {
        arguments.push_back("-U" + macro_option.get_name());
        return;
    }

    std::string define_argument = "-D" + macro_option.get_name();
    if (macro_option.has_replacement()) {
        define_argument += "=" + macro_option.get_replacement();
    }
    arguments.push_back(std::move(define_argument));
}

void append_depfile_target_arguments(std::vector<std::string> &arguments,
                                     const CompilerOption &option,
                                     const std::string &primary_output_file) {
    if (option.get_depfile_targets().empty()) {
        arguments.push_back("-MQ");
        arguments.push_back(primary_output_file);
        return;
    }

    for (const DepfileTargetOption &target : option.get_depfile_targets()) {
        arguments.push_back(target.get_quote_for_make() ? "-MQ" : "-MT");
        arguments.push_back(target.get_value());
    }
}

std::vector<std::string> build_dependency_scanner_arguments(
    const CompilerOption &option, const std::string &depfile_output_file,
    const std::string &primary_output_file) {
    std::vector<std::string> arguments;
    arguments.push_back("-x");
    arguments.push_back("c");
    if (const std::string language_flag =
            dependency_scanner_language_flag(option);
        !language_flag.empty()) {
        arguments.push_back(language_flag);
    }
    arguments.push_back(option.get_depfile_mode() == DepfileMode::MD ? "-MD"
                                                                     : "-MMD");
    if (option.get_depfile_add_phony_targets()) {
        arguments.push_back("-MP");
    }
    arguments.push_back("-MF");
    arguments.push_back(depfile_output_file);
    append_depfile_target_arguments(arguments, option, primary_output_file);
    if (option.get_no_stdinc()) {
        arguments.push_back("-nostdinc");
    }
    if (!option.get_sysroot().empty()) {
        arguments.push_back("--sysroot=" + option.get_sysroot());
    }
    if (!option.get_isysroot().empty()) {
        arguments.push_back("-isysroot");
        arguments.push_back(option.get_isysroot());
    }
    for (const std::string &include_directory :
         option.get_include_directories()) {
        arguments.push_back("-I");
        arguments.push_back(include_directory);
    }
    for (const std::string &quote_include_directory :
         option.get_quote_include_directories()) {
        arguments.push_back("-iquote");
        arguments.push_back(quote_include_directory);
    }
    for (const std::string &system_include_directory :
         option.get_system_include_directories()) {
        arguments.push_back("-isystem");
        arguments.push_back(system_include_directory);
    }
    for (const std::string &after_system_include_directory :
         option.get_after_system_include_directories()) {
        arguments.push_back("-idirafter");
        arguments.push_back(after_system_include_directory);
    }
    for (const CommandLineMacroOption &macro_option :
         option.get_command_line_macro_options()) {
        append_command_line_macro_argument(arguments, macro_option);
    }
    for (const std::string &forced_include_file :
         option.get_forced_include_files()) {
        arguments.push_back("-include");
        arguments.push_back(forced_include_file);
    }
    if (option.get_link_with_pthread()) {
        arguments.push_back("-pthread");
    }
    arguments.push_back("-E");
    arguments.push_back(option.get_input_file());
    arguments.push_back("-o");
    arguments.push_back("/dev/null");
    return arguments;
}

struct SubprocessResult {
    bool launched = false;
    int exit_code = 0;
    std::string error_message;
};

SubprocessResult run_subprocess(const std::string &program,
                                const std::vector<std::string> &arguments) {
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char *>(program.c_str()));
    for (const std::string &argument : arguments) {
        argv.push_back(const_cast<char *>(argument.c_str()));
    }
    argv.push_back(nullptr);

    pid_t child_pid = 0;
    const int spawn_result =
        posix_spawnp(&child_pid, program.c_str(), nullptr, nullptr, argv.data(),
                     environ);
    if (spawn_result != 0) {
        return {false, spawn_result, std::strerror(spawn_result)};
    }

    int wait_status = 0;
    if (waitpid(child_pid, &wait_status, 0) < 0) {
        return {true, errno, std::strerror(errno)};
    }
    if (WIFEXITED(wait_status)) {
        return {true, WEXITSTATUS(wait_status), ""};
    }
    if (WIFSIGNALED(wait_status)) {
        return {true, 128 + WTERMSIG(wait_status),
                "terminated by signal " + std::to_string(WTERMSIG(wait_status))};
    }
    return {true, 1, "terminated unexpectedly"};
}

std::vector<std::string> host_c_driver_candidates() {
    std::vector<std::string> candidates;
    if (const char *env_program = std::getenv("SYSYCC_HOST_CC");
        env_program != nullptr && env_program[0] != '\0') {
        candidates.emplace_back(env_program);
    }
    candidates.emplace_back("clang");
    candidates.emplace_back("cc");
    return candidates;
}

struct TemporaryFile {
    std::filesystem::path path;

    TemporaryFile() = default;
    TemporaryFile(const TemporaryFile &) = delete;
    TemporaryFile &operator=(const TemporaryFile &) = delete;

    TemporaryFile(TemporaryFile &&other) noexcept : path(std::move(other.path)) {
        other.path.clear();
    }

    TemporaryFile &operator=(TemporaryFile &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        cleanup();
        path = std::move(other.path);
        other.path.clear();
        return *this;
    }

    ~TemporaryFile() { cleanup(); }

    void cleanup() noexcept {
        if (path.empty()) {
            return;
        }
        std::error_code remove_error;
        std::filesystem::remove(path, remove_error);
        path.clear();
    }
};

bool write_text_file(const std::filesystem::path &path, const std::string &text,
                     std::string &error_message) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        error_message = "failed to open file '" + path.string() + "'";
        return false;
    }
    ofs << text;
    if (!ofs.good()) {
        error_message = "failed to write file '" + path.string() + "'";
        return false;
    }
    return true;
}

bool create_temporary_llvm_ir_file(const std::string &llvm_ir_text,
                                   TemporaryFile &temporary_file,
                                   std::string &error_message) {
    std::error_code temp_dir_error;
    const std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path(temp_dir_error);
    if (temp_dir_error) {
        error_message = "failed to query temporary directory: " +
                        temp_dir_error.message();
        return false;
    }

    std::string file_template =
        (temp_dir / "sysycc-full-link-XXXXXX").string();
    std::vector<char> template_buffer(file_template.begin(), file_template.end());
    template_buffer.push_back('\0');
    const int fd = mkstemp(template_buffer.data());
    if (fd < 0) {
        error_message =
            "failed to create temporary link file: " + std::string(std::strerror(errno));
        return false;
    }
    close(fd);

    temporary_file.path = template_buffer.data();
    return write_text_file(temporary_file.path, llvm_ir_text, error_message);
}

bool create_temporary_output_file(const std::string &prefix,
                                  TemporaryFile &temporary_file,
                                  std::string &error_message) {
    std::error_code temp_dir_error;
    const std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path(temp_dir_error);
    if (temp_dir_error) {
        error_message = "failed to query temporary directory: " +
                        temp_dir_error.message();
        return false;
    }

    std::string file_template = (temp_dir / (prefix + "-XXXXXX")).string();
    std::vector<char> template_buffer(file_template.begin(), file_template.end());
    template_buffer.push_back('\0');
    const int fd = mkstemp(template_buffer.data());
    if (fd < 0) {
        error_message =
            "failed to create temporary output file: " +
            std::string(std::strerror(errno));
        return false;
    }
    close(fd);
    temporary_file.path = template_buffer.data();
    return true;
}

std::vector<std::string> build_full_compile_link_arguments(
    const CompilerOption &option,
    const std::vector<std::string> &llvm_ir_files,
    const std::vector<std::string> &object_files,
    const std::string &output_file) {
    std::vector<std::string> arguments;
    if (option.get_backend_options().get_debug_info()) {
        arguments.push_back("-g");
    }
    if (!option.get_sysroot().empty()) {
        arguments.push_back("--sysroot=" + option.get_sysroot());
    }
    if (!option.get_isysroot().empty()) {
        arguments.push_back("-isysroot");
        arguments.push_back(option.get_isysroot());
    }
    for (const std::string &llvm_ir_file : llvm_ir_files) {
        arguments.push_back("-Wno-override-module");
        arguments.push_back("-x");
        arguments.push_back("ir");
        arguments.push_back(llvm_ir_file);
    }
    if (!llvm_ir_files.empty() &&
        (!object_files.empty() || !option.get_linker_input_files().empty() ||
         !option.get_linker_libraries().empty())) {
        arguments.push_back("-x");
        arguments.push_back("none");
    }
    for (const std::string &object_file : object_files) {
        arguments.push_back(object_file);
    }
    for (const std::string &link_input_file : option.get_linker_input_files()) {
        arguments.push_back(link_input_file);
    }
    for (const std::string &search_directory :
         option.get_linker_search_directories()) {
        arguments.push_back("-L");
        arguments.push_back(search_directory);
    }
    for (const std::string &library_name : option.get_linker_libraries()) {
        arguments.push_back("-l" + library_name);
    }
    for (const std::string &passthrough_argument :
         option.get_linker_passthrough_arguments()) {
        arguments.push_back(passthrough_argument);
    }
    if (option.get_link_with_pthread()) {
        arguments.push_back("-pthread");
    }
    arguments.push_back("-o");
    arguments.push_back(output_file);
    return arguments;
}

std::vector<std::string> effective_source_input_files(
    const CompilerOption &option) {
    if (!option.get_source_input_files().empty()) {
        return option.get_source_input_files();
    }
    if (!option.get_link_only() && !option.get_input_file().empty()) {
        return {option.get_input_file()};
    }
    return {};
}

bool ensure_output_parent_directory(const std::string &output_file,
                                    std::string &error_message) {
    const std::filesystem::path output_path(output_file);
    if (!output_path.has_parent_path()) {
        return true;
    }
    std::error_code create_error;
    std::filesystem::create_directories(output_path.parent_path(),
                                        create_error);
    if (create_error) {
        error_message = "failed to create output directory '" +
                        output_path.parent_path().string() +
                        "': " + create_error.message();
        return false;
    }
    return true;
}

PassResult run_host_c_driver_arguments(
    CompilerContext &context, const std::vector<std::string> &arguments,
    const std::string &purpose) {
    std::string last_failure_message;
    for (const std::string &program : host_c_driver_candidates()) {
        const SubprocessResult result = run_subprocess(program, arguments);
        if (!result.launched) {
            last_failure_message =
                "failed to launch host C driver '" + program +
                "' for " + purpose + ": " + result.error_message;
            continue;
        }
        if (result.exit_code == 0) {
            return PassResult::Success();
        }

        last_failure_message =
            "host C driver '" + program + "' failed for " + purpose +
            " with exit code " + std::to_string(result.exit_code);
        if (!result.error_message.empty()) {
            last_failure_message += ": " + result.error_message;
        }
        break;
    }
    if (last_failure_message.empty()) {
        last_failure_message =
            "failed to locate a host C compiler for " + purpose;
    }
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              last_failure_message);
    return PassResult::Failure(last_failure_message);
}

PassResult compile_llvm_ir_to_host_object(const CompilerOption &option,
                                          CompilerContext &context) {
    const IRResult *ir_result = context.get_ir_result();
    if (ir_result == nullptr) {
        const std::string message =
            "missing LLVM IR output for host object emission";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                  message);
        return PassResult::Failure(message);
    }

    const std::string output_file = effective_primary_output_file(option);
    std::string output_directory_error;
    if (!ensure_output_parent_directory(output_file, output_directory_error)) {
        context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                  output_directory_error);
        return PassResult::Failure(output_directory_error);
    }

    TemporaryFile temporary_llvm_ir_file;
    std::string temp_file_error;
    if (!create_temporary_llvm_ir_file(ir_result->get_text(),
                                       temporary_llvm_ir_file,
                                       temp_file_error)) {
        context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                  temp_file_error);
        return PassResult::Failure(temp_file_error);
    }

    std::vector<std::string> arguments;
    if (option.get_backend_options().get_debug_info()) {
        arguments.push_back("-g");
    }
    if (option.get_backend_options().get_position_independent()) {
        arguments.push_back("-fPIC");
    }
    if (!option.get_sysroot().empty()) {
        arguments.push_back("--sysroot=" + option.get_sysroot());
    }
    if (!option.get_isysroot().empty()) {
        arguments.push_back("-isysroot");
        arguments.push_back(option.get_isysroot());
    }
    arguments.push_back("-Wno-override-module");
    arguments.push_back("-x");
    arguments.push_back("ir");
    arguments.push_back(temporary_llvm_ir_file.path.string());
    arguments.push_back("-c");
    arguments.push_back("-o");
    arguments.push_back(output_file);

    PassResult result =
        run_host_c_driver_arguments(context, arguments, "LLVM IR object emission");
    if (!result.ok) {
        return result;
    }
    context.set_object_result(
        std::make_unique<ObjectResult>(ObjectTargetKind::None,
                                       std::vector<std::uint8_t>{}));
    return PassResult::Success();
}

std::string default_compile_only_output_file_for_source(
    const std::string &source_file) {
    const std::filesystem::path input_path(source_file);
    return input_path.stem().string() + ".o";
}

void append_child_diagnostics(const Compiler &child_compiler,
                              CompilerContext &parent_context) {
    const DiagnosticEngine &child_diagnostic_engine =
        child_compiler.get_context().get_diagnostic_engine();
    for (const Diagnostic &diagnostic :
         child_diagnostic_engine.get_diagnostics()) {
        parent_context.get_diagnostic_engine().add_diagnostic(diagnostic);
    }
}

bool compile_source_to_temporary_llvm_ir(const CompilerOption &option,
                                         const std::string &source_file,
                                         TemporaryFile &temporary_file,
                                         CompilerContext &parent_context,
                                         std::string &error_message) {
    CompilerOption source_option = option;
    source_option.set_input_file(source_file);
    source_option.set_source_input_files({source_file});
    source_option.set_linker_input_files({});
    source_option.set_link_only(false);
    source_option.set_output_file("");
    source_option.set_depfile_mode(DepfileMode::None);
    source_option.set_depfile_output_file("");
    source_option.set_depfile_targets({});
    source_option.set_depfile_add_phony_targets(false);
    source_option.set_driver_action(DriverAction::EmitLlvmIr);
    source_option.set_emit_asm(false);
    source_option.set_emit_object(false);
    source_option.set_stop_after_stage(StopAfterStage::IR);

    BackendOptions backend_options = source_option.get_backend_options();
    backend_options.set_backend_kind(BackendKind::LlvmIr);
    backend_options.set_target_triple("");
    backend_options.set_output_file("");
    source_option.set_backend_options(std::move(backend_options));

    Compiler source_compiler(std::move(source_option));
    PassResult compile_result = source_compiler.Run();
    append_child_diagnostics(source_compiler, parent_context);
    const DiagnosticEngine &source_diagnostic_engine =
        source_compiler.get_context().get_diagnostic_engine();

    if (!compile_result.ok) {
        error_message = "failed to compile source input '" + source_file +
                        "': " + compile_result.message;
        if (source_diagnostic_engine.get_diagnostics().empty()) {
            parent_context.get_diagnostic_engine().add_error(
                DiagnosticStage::Compiler, error_message);
        }
        return false;
    }

    const IRResult *ir_result = source_compiler.get_context().get_ir_result();
    if (ir_result == nullptr) {
        error_message = "missing LLVM IR output for source input '" +
                        source_file + "'";
        parent_context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler, error_message);
        return false;
    }

    if (!create_temporary_llvm_ir_file(ir_result->get_text(), temporary_file,
                                       error_message)) {
        parent_context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler, error_message);
        return false;
    }
    return true;
}

CompilerOption make_single_source_llvm_ir_option(const CompilerOption &option,
                                                 const std::string &source_file) {
    CompilerOption source_option = option;
    source_option.set_input_file(source_file);
    source_option.set_source_input_files({source_file});
    source_option.set_linker_input_files({});
    source_option.set_link_only(false);
    source_option.set_output_file("");
    source_option.set_depfile_mode(DepfileMode::None);
    source_option.set_depfile_output_file("");
    source_option.set_depfile_targets({});
    source_option.set_depfile_add_phony_targets(false);
    source_option.set_driver_action(DriverAction::EmitLlvmIr);
    source_option.set_emit_asm(false);
    source_option.set_emit_object(false);
    source_option.set_stop_after_stage(StopAfterStage::IR);

    BackendOptions backend_options = source_option.get_backend_options();
    backend_options.set_backend_kind(BackendKind::LlvmIr);
    backend_options.set_target_triple("");
    backend_options.set_output_file("");
    source_option.set_backend_options(std::move(backend_options));
    return source_option;
}

bool compile_source_to_temporary_llvm_ir_isolated(
    const CompilerOption &option, const std::string &source_file,
    TemporaryFile &temporary_file, CompilerContext &parent_context,
    std::string &error_message) {
    if (!create_temporary_output_file("sysycc-full-link-ir", temporary_file,
                                      error_message)) {
        parent_context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                         error_message);
        return false;
    }

    const pid_t child_pid = fork();
    if (child_pid < 0) {
        error_message =
            "failed to fork isolated source compiler: " +
            std::string(std::strerror(errno));
        parent_context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                         error_message);
        return false;
    }

    if (child_pid == 0) {
        Compiler source_compiler(
            make_single_source_llvm_ir_option(option, source_file));
        const PassResult compile_result = source_compiler.Run();
        if (!compile_result.ok) {
            std::exit(2);
        }
        const IRResult *ir_result =
            source_compiler.get_context().get_ir_result();
        if (ir_result == nullptr) {
            std::exit(3);
        }
        std::string child_error;
        if (!write_text_file(temporary_file.path, ir_result->get_text(),
                             child_error)) {
            std::exit(4);
        }
        std::exit(0);
    }

    int wait_status = 0;
    if (waitpid(child_pid, &wait_status, 0) < 0) {
        error_message = "failed to wait for isolated source compiler: " +
                        std::string(std::strerror(errno));
        parent_context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                         error_message);
        return false;
    }
    if (WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0) {
        return true;
    }
    if (WIFSIGNALED(wait_status)) {
        error_message = "isolated source compiler for '" + source_file +
                        "' terminated by signal " +
                        std::to_string(WTERMSIG(wait_status));
    } else {
        const int exit_code =
            WIFEXITED(wait_status) ? WEXITSTATUS(wait_status) : 1;
        error_message = "isolated source compiler for '" + source_file +
                        "' failed with exit code " +
                        std::to_string(exit_code);
    }
    parent_context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                     error_message);
    return false;
}

bool compile_temporary_llvm_ir_to_temporary_object(
    const CompilerOption &option, const std::filesystem::path &llvm_ir_file,
    TemporaryFile &temporary_object_file, CompilerContext &context,
    std::string &error_message) {
    if (!create_temporary_output_file("sysycc-full-link-obj",
                                      temporary_object_file, error_message)) {
        context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                  error_message);
        return false;
    }

    std::vector<std::string> arguments;
    if (option.get_backend_options().get_debug_info()) {
        arguments.push_back("-g");
    }
    if (option.get_backend_options().get_position_independent()) {
        arguments.push_back("-fPIC");
    }
    if (!option.get_sysroot().empty()) {
        arguments.push_back("--sysroot=" + option.get_sysroot());
    }
    if (!option.get_isysroot().empty()) {
        arguments.push_back("-isysroot");
        arguments.push_back(option.get_isysroot());
    }
    arguments.push_back("-Wno-override-module");
    arguments.push_back("-x");
    arguments.push_back("ir");
    arguments.push_back(llvm_ir_file.string());
    arguments.push_back("-c");
    arguments.push_back("-o");
    arguments.push_back(temporary_object_file.path.string());

    PassResult result =
        run_host_c_driver_arguments(context, arguments, "LLVM IR object emission");
    if (!result.ok) {
        error_message = result.message;
        return false;
    }
    return true;
}

PassResult compile_multiple_sources_to_objects(const CompilerOption &option,
                                               CompilerContext &context) {
    const std::vector<std::string> source_input_files =
        effective_source_input_files(option);
    for (const std::string &source_file : source_input_files) {
        CompilerOption source_option = option;
        source_option.set_input_file(source_file);
        source_option.set_source_input_files({source_file});
        source_option.set_linker_input_files({});
        source_option.set_link_only(false);
        source_option.set_output_file(
            default_compile_only_output_file_for_source(source_file));
        source_option.set_depfile_output_file("");
        source_option.set_depfile_targets({});

        BackendOptions backend_options = source_option.get_backend_options();
        backend_options.set_output_file(source_option.get_output_file());
        source_option.set_backend_options(std::move(backend_options));

        Compiler source_compiler(std::move(source_option));
        const PassResult result = source_compiler.Run();
        append_child_diagnostics(source_compiler, context);
        if (!result.ok) {
            const std::string message =
                "failed to compile source input '" + source_file +
                "': " + result.message;
            if (source_compiler.get_context()
                    .get_diagnostic_engine()
                    .get_diagnostics()
                    .empty()) {
                context.get_diagnostic_engine().add_error(
                    DiagnosticStage::Compiler, message);
            }
            return PassResult::Failure(message);
        }
    }

    return PassResult::Success();
}

PassResult maybe_link_full_compile(const CompilerOption &option,
                                   CompilerContext &context) {
    if (option.get_driver_action() != DriverAction::FullCompile) {
        return PassResult::Success();
    }

    const std::string output_file = effective_primary_output_file(option);
    if (output_file.empty()) {
        const std::string message =
            "failed to determine executable output path for full compile";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                  message);
        return PassResult::Failure(message);
    }

    const std::filesystem::path output_path(output_file);
    if (output_path.has_parent_path()) {
        std::error_code create_error;
        std::filesystem::create_directories(output_path.parent_path(),
                                            create_error);
        if (create_error) {
            const std::string message =
                "failed to create output directory '" +
                output_path.parent_path().string() +
                "': " + create_error.message();
            context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                      message);
            return PassResult::Failure(message);
        }
    }

    std::vector<TemporaryFile> temporary_llvm_ir_files;
    std::vector<TemporaryFile> temporary_object_files;
    std::vector<std::string> llvm_ir_file_paths;
    std::vector<std::string> object_file_paths;
    if (!option.get_link_only()) {
        const std::vector<std::string> source_input_files =
            effective_source_input_files(option);
        if (source_input_files.size() > 1) {
            temporary_llvm_ir_files.reserve(source_input_files.size());
            temporary_object_files.reserve(source_input_files.size());
            object_file_paths.reserve(source_input_files.size());
            for (const std::string &source_file : source_input_files) {
                TemporaryFile temporary_llvm_ir_file;
                std::string temp_file_error;
                if (!compile_source_to_temporary_llvm_ir_isolated(
                        option, source_file, temporary_llvm_ir_file, context,
                        temp_file_error)) {
                    return PassResult::Failure(temp_file_error);
                }
                TemporaryFile temporary_object_file;
                if (!compile_temporary_llvm_ir_to_temporary_object(
                        option, temporary_llvm_ir_file.path,
                        temporary_object_file, context, temp_file_error)) {
                    return PassResult::Failure(temp_file_error);
                }
                object_file_paths.push_back(temporary_object_file.path.string());
                temporary_llvm_ir_files.push_back(
                    std::move(temporary_llvm_ir_file));
                temporary_object_files.push_back(
                    std::move(temporary_object_file));
            }
        } else {
            const IRResult *ir_result = context.get_ir_result();
            if (ir_result == nullptr) {
                const std::string message =
                    "missing LLVM IR output for full compile";
                context.get_diagnostic_engine().add_error(
                    DiagnosticStage::Compiler, message);
                return PassResult::Failure(message);
            }

            TemporaryFile temporary_llvm_ir_file;
            std::string temp_file_error;
            if (!create_temporary_llvm_ir_file(ir_result->get_text(),
                                               temporary_llvm_ir_file,
                                               temp_file_error)) {
                context.get_diagnostic_engine().add_error(
                    DiagnosticStage::Compiler, temp_file_error);
                return PassResult::Failure(temp_file_error);
            }
            llvm_ir_file_paths.push_back(temporary_llvm_ir_file.path.string());
            temporary_llvm_ir_files.push_back(std::move(temporary_llvm_ir_file));
        }
    }

    const std::vector<std::string> arguments =
        build_full_compile_link_arguments(option, llvm_ir_file_paths,
                                          object_file_paths,
                                          output_file);
    std::string last_failure_message;
    for (const std::string &program : host_c_driver_candidates()) {
        const SubprocessResult result = run_subprocess(program, arguments);
        if (!result.launched) {
            last_failure_message =
                "failed to launch host linker driver '" + program +
                "': " + result.error_message;
            continue;
        }
        if (result.exit_code == 0) {
            return PassResult::Success();
        }

        last_failure_message =
            "host linker driver '" + program +
            "' failed with exit code " + std::to_string(result.exit_code);
        if (!result.error_message.empty()) {
            last_failure_message += ": " + result.error_message;
        }
        break;
    }

    if (last_failure_message.empty()) {
        last_failure_message =
            "failed to locate a host C compiler for full compile linking";
    }
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              last_failure_message);
    return PassResult::Failure(last_failure_message);
}

PassResult maybe_generate_depfile(const CompilerOption &option,
                                  CompilerContext &context) {
    if (!option.get_generate_depfile()) {
        return PassResult::Success();
    }

    const std::string primary_output_file =
        effective_primary_output_file(option);
    if (primary_output_file.empty()) {
        const std::string message =
            "failed to determine the primary output path for depfile generation";
        context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                  message);
        return PassResult::Failure(message);
    }

    const std::string depfile_output_file =
        effective_depfile_output_file(option);
    std::error_code filesystem_error;
    const std::filesystem::path depfile_path(depfile_output_file);
    if (depfile_path.has_parent_path()) {
        std::filesystem::create_directories(depfile_path.parent_path(),
                                            filesystem_error);
        if (filesystem_error) {
            const std::string message =
                "failed to create depfile directory '" +
                depfile_path.parent_path().string() +
                "': " + filesystem_error.message();
            context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                      message);
            return PassResult::Failure(message);
        }
    }

    const std::vector<std::string> arguments =
        build_dependency_scanner_arguments(option, depfile_output_file,
                                           primary_output_file);

    std::string last_failure_message;
    for (const std::string &program : host_c_driver_candidates()) {
        const SubprocessResult result = run_subprocess(program, arguments);
        if (!result.launched) {
            last_failure_message =
                "failed to launch dependency scanner '" + program +
                "': " + result.error_message;
            continue;
        }
        if (result.exit_code == 0) {
            return PassResult::Success();
        }

        last_failure_message =
            "dependency scanner '" + program +
            "' failed with exit code " + std::to_string(result.exit_code);
        if (!result.error_message.empty()) {
            last_failure_message += ": " + result.error_message;
        }
        break;
    }

    if (last_failure_message.empty()) {
        last_failure_message =
            "failed to locate a host C compiler for depfile generation";
    }
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              last_failure_message);
    return PassResult::Failure(last_failure_message);
}

} // namespace

Compiler::Compiler()
    : context_(std::make_unique<CompilerContext>()),
      pass_manager_(std::make_unique<PassManager>()) {}

Compiler::Compiler(CompilerOption option)
    : option_(std::move(option)),
      context_(std::make_unique<CompilerContext>()),
      pass_manager_(std::make_unique<PassManager>()) {
    sync_context_from_option();
}

Compiler::~Compiler() = default;

Compiler::Compiler(Compiler &&) noexcept = default;

Compiler &Compiler::operator=(Compiler &&) noexcept = default;

void Compiler::sync_context_from_option() {
    context_->set_input_file(option_.get_input_file());
    context_->set_include_directories(option_.get_include_directories());
    context_->set_quote_include_directories(option_.get_quote_include_directories());
    std::vector<std::string> system_include_directories =
        option_.get_system_include_directories();
    system_include_directories.insert(
        system_include_directories.end(),
        option_.get_after_system_include_directories().begin(),
        option_.get_after_system_include_directories().end());
    context_->set_system_include_directories(std::move(system_include_directories));
    context_->set_command_line_macro_options(
        option_.get_command_line_macro_options());
    context_->set_forced_include_files(option_.get_forced_include_files());
    context_->set_no_stdinc(option_.get_no_stdinc());
    context_->set_dump_tokens(option_.dump_tokens());
    context_->set_dump_parse(option_.dump_parse());
    context_->set_dump_ast(option_.dump_ast());
    context_->set_dump_ir(option_.dump_ir());
    context_->set_dump_core_ir(option_.dump_core_ir());
    context_->set_emit_asm(option_.emit_asm());
    context_->set_emit_object(option_.emit_object());
    context_->set_stop_after_stage(option_.get_stop_after_stage());
    context_->set_optimization_level(option_.get_optimization_level());
    context_->set_backend_options(option_.get_backend_options());
    context_->get_diagnostic_engine().set_warning_policy(
        option_.get_warning_policy());
    context_->configure_dialects(option_.get_enable_gnu_dialect(),
                                option_.get_enable_clang_dialect(),
                                option_.get_enable_builtin_type_extension_pack());
}

void Compiler::InitializePasses() {
    if (pipeline_initialized_) {
        return;
    }

    const BackendKind backend_kind =
        context_->get_backend_options().get_backend_kind();
    const StopAfterStage stop_after_stage = context_->get_stop_after_stage();

    pass_manager_->AddPass(std::make_unique<PreprocessPass>());
    pass_manager_->AddPass(std::make_unique<LexerPass>());
    pass_manager_->AddPass(std::make_unique<ParserPass>());
    pass_manager_->AddPass(std::make_unique<AstPass>());
    pass_manager_->AddPass(std::make_unique<SemanticPass>());
    append_default_core_ir_pipeline(*pass_manager_, backend_kind);
    if (!(is_native_backend(backend_kind) &&
          stop_after_stage == StopAfterStage::CoreIr)) {
        pass_manager_->AddPass(std::make_unique<AArch64AsmGenPass>());
        auto riscv64_pass = std::make_unique<Riscv64AsmGenPass>();
        pass_manager_->AddPass(std::move(riscv64_pass));
    }
    pipeline_initialized_ = true;
}

void Compiler::set_option(CompilerOption option) {
    option_ = std::move(option);
    sync_context_from_option();
}

const CompilerOption &Compiler::get_option() const noexcept { return option_; }

CompilerContext &Compiler::get_context() noexcept { return *context_; }

const CompilerContext &Compiler::get_context() const noexcept {
    return *context_;
}

void Compiler::register_dialect(std::unique_ptr<FrontendDialect> dialect) {
    if (dialect == nullptr) {
        return;
    }
    extra_dialects_.push_back(std::move(dialect));
}

void Compiler::AddPass(std::unique_ptr<Pass> pass) {
    pass_manager_->AddPass(std::move(pass));
}

PassResult Compiler::validate_dialect_configuration() {
    const auto &registration_errors =
        context_->get_dialect_manager().get_registration_errors();
    if (registration_errors.empty()) {
        return PassResult::Success();
    }

    const std::string summary =
        "invalid dialect configuration: registration conflicts detected";
    context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                               summary);
    for (const std::string &registration_error : registration_errors) {
        context_->get_diagnostic_engine().add_note(DiagnosticStage::Compiler,
                                                  registration_error);
    }
    return PassResult::Failure(summary);
}

PassResult Compiler::validate_backend_configuration() {
    const BackendOptions &backend_options = context_->get_backend_options();
    const BackendKind backend_kind = backend_options.get_backend_kind();
    const std::string &target_triple = backend_options.get_target_triple();

    if (is_native_backend(backend_kind)) {
        if (!context_->get_emit_asm()) {
            if (!context_->get_emit_object()) {
                const std::string message =
                    std::string("--backend=") + backend_kind_name(backend_kind) +
                    " currently requires -S or -c";
                context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                           message);
                return PassResult::Failure(message);
            }
        }
        if (context_->get_emit_asm() && context_->get_emit_object()) {
            const std::string message =
                std::string("--backend=") + backend_kind_name(backend_kind) +
                " cannot emit asm and object at the same time";
            context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        if (!target_triple.empty() &&
            target_triple != expected_target_triple(backend_kind)) {
            const std::string message =
                "unsupported " + std::string(backend_kind_name(backend_kind)) +
                " target triple: " + target_triple;
            context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        if (context_->get_stop_after_stage() == StopAfterStage::IR) {
            const std::string message =
                std::string("--stop-after=ir is incompatible with --backend=") +
                backend_kind_name(backend_kind);
            context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        if (context_->get_dump_ir()) {
            const std::string message =
                std::string("--dump-ir is incompatible with --backend=") +
                backend_kind_name(backend_kind);
            context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        return PassResult::Success();
    }

    if (context_->get_emit_object() &&
        option_.get_driver_action() != DriverAction::CompileOnly) {
        const std::string message =
            "object emission with --backend=llvm-ir is only supported through -c";
        context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }

    if (context_->get_emit_asm()) {
        const std::string message =
            "-S currently requires --backend=aarch64-native or --backend=riscv64-native";
        context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }
    if (!target_triple.empty()) {
        const std::string message =
            "--target is only supported with --backend=aarch64-native or --backend=riscv64-native";
        context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }
    if (context_->get_stop_after_stage() == StopAfterStage::Asm) {
        const std::string message =
            "--stop-after=asm requires --backend=aarch64-native or --backend=riscv64-native together with -S";
        context_->get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }

    return PassResult::Success();
}

PassResult Compiler::validate_driver_configuration() {
    const std::vector<std::string> source_input_files =
        effective_source_input_files(option_);
    switch (option_.get_driver_action()) {
    case DriverAction::InternalPipeline:
        return PassResult::Success();
    case DriverAction::FullCompile:
        if (source_input_files.size() > 1 && option_.get_generate_depfile()) {
            const std::string message =
                "dependency generation with multiple source inputs is not supported yet; compile sources separately";
            context_->get_diagnostic_engine().add_error(
                DiagnosticStage::Compiler, message);
            return PassResult::Failure(message);
        }
        return PassResult::Success();
    case DriverAction::CompileOnly:
        if (source_input_files.size() > 1) {
            if (!option_.get_output_file().empty()) {
                const std::string message =
                    "cannot specify '-o' with '-c' and multiple source inputs";
                context_->get_diagnostic_engine().add_error(
                    DiagnosticStage::Compiler, message);
                return PassResult::Failure(message);
            }
            if (!option_.get_depfile_output_file().empty()) {
                const std::string message =
                    "'-MF' with '-c' and multiple source inputs is not supported yet; compile sources separately";
                context_->get_diagnostic_engine().add_error(
                    DiagnosticStage::Compiler, message);
                return PassResult::Failure(message);
            }
            if (!option_.get_depfile_targets().empty()) {
                const std::string message =
                    "'-MT' and '-MQ' with '-c' and multiple source inputs are not supported yet; compile sources separately";
                context_->get_diagnostic_engine().add_error(
                    DiagnosticStage::Compiler, message);
                return PassResult::Failure(message);
            }
        }
        return PassResult::Success();
    case DriverAction::PreprocessOnly:
    case DriverAction::SyntaxOnly:
    case DriverAction::EmitAssembly:
    case DriverAction::EmitLlvmIr:
        return PassResult::Success();
    }

    return PassResult::Success();
}

PassResult Compiler::Run() {
    context_->clear_diagnostic_engine();
    context_->clear_core_ir_build_result();
    context_->clear_ir_result();
    context_->clear_asm_result();
    context_->clear_object_result();
    context_->set_core_ir_dump_file_path("");
    context_->set_ir_dump_file_path("");
    context_->set_llvm_ir_text_artifact_file_path("");
    context_->set_llvm_ir_bitcode_artifact_file_path("");
    context_->set_asm_dump_file_path("");
    context_->set_object_dump_file_path("");
    sync_context_from_option();
    for (auto &dialect : extra_dialects_) {
        context_->get_dialect_manager().register_dialect(std::move(dialect));
    }
    extra_dialects_.clear();
    PassResult driver_validation_result = validate_driver_configuration();
    if (!driver_validation_result.ok) {
        return driver_validation_result;
    }
    PassResult dialect_validation_result = validate_dialect_configuration();
    if (!dialect_validation_result.ok) {
        return dialect_validation_result;
    }
    PassResult backend_validation_result = validate_backend_configuration();
    if (!backend_validation_result.ok) {
        return backend_validation_result;
    }
    if (option_.get_link_only()) {
        return maybe_link_full_compile(option_, *context_);
    }
    if (option_.get_driver_action() == DriverAction::CompileOnly &&
        effective_source_input_files(option_).size() > 1) {
        return compile_multiple_sources_to_objects(option_, *context_);
    }
    if (option_.get_driver_action() == DriverAction::FullCompile &&
        effective_source_input_files(option_).size() > 1) {
        return maybe_link_full_compile(option_, *context_);
    }
    InitializePasses();
    PassResult pipeline_result = pass_manager_->Run(*context_);
    if (!pipeline_result.ok) {
        return pipeline_result;
    }
    if (option_.get_driver_action() == DriverAction::CompileOnly &&
        option_.get_backend_options().get_backend_kind() == BackendKind::LlvmIr) {
        PassResult object_result =
            compile_llvm_ir_to_host_object(option_, *context_);
        if (!object_result.ok) {
            return object_result;
        }
        PassResult depfile_result = maybe_generate_depfile(option_, *context_);
        if (!depfile_result.ok) {
            return depfile_result;
        }
        return object_result;
    }
    PassResult full_compile_link_result =
        maybe_link_full_compile(option_, *context_);
    if (!full_compile_link_result.ok) {
        return full_compile_link_result;
    }
    return maybe_generate_depfile(option_, *context_);
}

} // namespace sysycc
