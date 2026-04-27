#pragma once

#include <filesystem>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "backend/asm_gen/backend_options.hpp"
#include "common/diagnostic/warning_policy.hpp"

namespace sysycc {

namespace detail {

inline void append_existing_system_include_directory(
    std::vector<std::string> &system_include_directories,
    const std::filesystem::path &include_path) {
    std::error_code error;
    if (!std::filesystem::is_directory(include_path, error)) {
        return;
    }

    const std::string include_path_string = include_path.string();
    for (const std::string &existing_path : system_include_directories) {
        if (existing_path == include_path_string) {
            return;
        }
    }
    system_include_directories.push_back(include_path_string);
}

inline void append_clang_resource_include_directories(
    std::vector<std::string> &system_include_directories,
    const std::filesystem::path &root_path) {
    const std::filesystem::path clang_include_root(
        root_path / "Library/Developer/CommandLineTools/usr/lib/clang");
    std::error_code error;
    for (const auto &entry :
         std::filesystem::directory_iterator(clang_include_root, error)) {
        if (!entry.is_directory()) {
            continue;
        }

        append_existing_system_include_directory(system_include_directories,
                                                 entry.path() / "include");
    }
}

inline void append_linux_gcc_include_directories(
    std::vector<std::string> &system_include_directories,
    const std::filesystem::path &root_path) {
    const std::filesystem::path gcc_root(root_path / "usr/lib/gcc");
    std::error_code root_error;
    for (const auto &triple_entry :
         std::filesystem::directory_iterator(gcc_root, root_error)) {
        if (!triple_entry.is_directory()) {
            continue;
        }
        const std::string triple_name = triple_entry.path().filename().string();
        if (triple_name.find("-linux-gnu") == std::string::npos) {
            continue;
        }
        std::error_code version_error;
        for (const auto &version_entry :
             std::filesystem::directory_iterator(triple_entry.path(),
                                                 version_error)) {
            if (!version_entry.is_directory()) {
                continue;
            }
            append_existing_system_include_directory(system_include_directories,
                                                     version_entry.path() /
                                                         "include");
        }
    }
}

inline void append_linux_clang_include_directories(
    std::vector<std::string> &system_include_directories,
    const std::filesystem::path &root_path) {
    const std::filesystem::path direct_clang_root(root_path / "usr/lib/clang");
    std::error_code direct_error;
    for (const auto &version_entry :
         std::filesystem::directory_iterator(direct_clang_root, direct_error)) {
        if (!version_entry.is_directory()) {
            continue;
        }
        append_existing_system_include_directory(system_include_directories,
                                                 version_entry.path() /
                                                     "include");
    }

    const std::filesystem::path usr_lib(root_path / "usr/lib");
    std::error_code usr_lib_error;
    for (const auto &llvm_entry :
         std::filesystem::directory_iterator(usr_lib, usr_lib_error)) {
        if (!llvm_entry.is_directory()) {
            continue;
        }
        const std::string llvm_dir_name = llvm_entry.path().filename().string();
        if (llvm_dir_name.rfind("llvm-", 0) != 0) {
            continue;
        }
        const std::filesystem::path version_root(llvm_entry.path() /
                                                 "lib/clang");
        std::error_code version_error;
        for (const auto &version_entry :
             std::filesystem::directory_iterator(version_root, version_error)) {
            if (!version_entry.is_directory()) {
                continue;
            }
            append_existing_system_include_directory(system_include_directories,
                                                     version_entry.path() /
                                                         "include");
        }
    }
}

inline void append_linux_multiarch_include_directories(
    std::vector<std::string> &system_include_directories,
    const std::filesystem::path &root_path) {
    const std::filesystem::path usr_include(root_path / "usr/include");
    const std::vector<std::string> common_triples = {
        "aarch64-linux-gnu",   "x86_64-linux-gnu", "arm-linux-gnueabihf",
        "arm-linux-gnueabi",   "i386-linux-gnu",   "riscv64-linux-gnu",
        "powerpc64le-linux-gnu"};
    for (const std::string &triple : common_triples) {
        append_existing_system_include_directory(system_include_directories,
                                                 usr_include / triple);
    }

    std::error_code error;
    for (const auto &entry :
         std::filesystem::directory_iterator(usr_include, error)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string directory_name = entry.path().filename().string();
        if (directory_name.find("-linux-gnu") == std::string::npos) {
            continue;
        }
        append_existing_system_include_directory(system_include_directories,
                                                 entry.path());
    }
}

inline void append_linux_system_include_directories(
    std::vector<std::string> &system_include_directories,
    const std::filesystem::path &root_path) {
    append_existing_system_include_directory(system_include_directories,
                                             root_path / "usr/local/include");
    const std::size_t before_gcc_include_count =
        system_include_directories.size();
    append_linux_gcc_include_directories(system_include_directories, root_path);
    if (system_include_directories.size() == before_gcc_include_count) {
        append_linux_clang_include_directories(system_include_directories,
                                               root_path);
    }
    append_linux_multiarch_include_directories(system_include_directories,
                                               root_path);
    append_existing_system_include_directory(system_include_directories,
                                             root_path / "usr/include");
}

inline std::vector<std::string> get_default_system_include_directories() {
    std::vector<std::string> system_include_directories;

#if defined(__APPLE__)
    append_clang_resource_include_directories(system_include_directories, "/");
    append_existing_system_include_directory(
        system_include_directories,
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include");
    append_existing_system_include_directory(
        system_include_directories,
        "/Library/Developer/CommandLineTools/usr/include");
#else
    append_linux_system_include_directories(system_include_directories, "/");
#endif

    return system_include_directories;
}

} // namespace detail

enum class StopAfterStage : uint8_t {
    None,
    Preprocess,
    Lex,
    Parse,
    Ast,
    Semantic,
    CoreIr,
    IR,
    Asm,
};

enum class DriverAction : uint8_t {
    InternalPipeline,
    FullCompile,
    CompileOnly,
    PreprocessOnly,
    SyntaxOnly,
    EmitAssembly,
    EmitLlvmIr,
};

enum class LanguageMode : uint8_t {
    Sysy,
    C99,
    C11,
    C17,
    C2x,
    Gnu99,
    Gnu11,
    Gnu17,
    Gnu2x,
};

enum class OptimizationLevel : uint8_t {
    O0,
    O1,
};

enum class DepfileMode : uint8_t {
    None,
    MD,
    MMD,
};

enum class CommandLineMacroActionKind : uint8_t {
    Define,
    Undefine,
};

class DepfileTargetOption {
  private:
    std::string value_;
    bool quote_for_make_ = false;

  public:
    DepfileTargetOption() = default;

    DepfileTargetOption(std::string value, bool quote_for_make)
        : value_(std::move(value)), quote_for_make_(quote_for_make) {}

    const std::string &get_value() const noexcept { return value_; }

    bool get_quote_for_make() const noexcept { return quote_for_make_; }
};

class CommandLineMacroOption {
  private:
    CommandLineMacroActionKind action_kind_ = CommandLineMacroActionKind::Define;
    std::string name_;
    std::string replacement_;
    bool has_replacement_ = false;

  public:
    CommandLineMacroOption() = default;

    CommandLineMacroOption(CommandLineMacroActionKind action_kind,
                           std::string name, std::string replacement = {},
                           bool has_replacement = false)
        : action_kind_(action_kind), name_(std::move(name)),
          replacement_(std::move(replacement)),
          has_replacement_(has_replacement) {}

    CommandLineMacroActionKind get_action_kind() const noexcept {
        return action_kind_;
    }

    const std::string &get_name() const noexcept { return name_; }

    const std::string &get_replacement() const noexcept { return replacement_; }

    bool has_replacement() const noexcept { return has_replacement_; }
};

// Stores the configuration for one compiler invocation.
class CompilerOption {
  private:
    std::string input_file_;
    std::vector<std::string> source_input_files_;
    std::vector<std::string> linker_input_files_;
    bool link_only_ = false;
    std::string output_file_;
    DepfileMode depfile_mode_ = DepfileMode::None;
    std::string depfile_output_file_;
    std::vector<DepfileTargetOption> depfile_targets_;
    bool depfile_add_phony_targets_ = false;
    std::vector<std::string> include_directories_;
    std::vector<std::string> quote_include_directories_;
    std::vector<std::string> system_include_directories_ =
        detail::get_default_system_include_directories();
    std::vector<std::string> after_system_include_directories_;
    std::vector<CommandLineMacroOption> command_line_macro_options_;
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
    bool emit_object_ = false;
    StopAfterStage stop_after_stage_ = StopAfterStage::None;
    DriverAction driver_action_ = DriverAction::InternalPipeline;
    LanguageMode language_mode_ = LanguageMode::Sysy;
    OptimizationLevel optimization_level_ = OptimizationLevel::O0;
    bool enable_gnu_dialect_ = true;
    bool enable_clang_dialect_ = true;
    bool enable_builtin_type_extension_pack_ = true;
    bool verbose_ = false;
    WarningPolicy warning_policy_;
    BackendOptions backend_options_;

  public:
    CompilerOption() = default;

    explicit CompilerOption(std::string input_file)
        : input_file_(std::move(input_file)) {}

    CompilerOption(std::string input_file, std::string output_file)
        : input_file_(std::move(input_file)),
          output_file_(std::move(output_file)) {}

    const std::string &get_input_file() const noexcept { return input_file_; }

    void set_input_file(std::string input_file) {
        input_file_ = std::move(input_file);
    }

    const std::vector<std::string> &get_source_input_files() const noexcept {
        return source_input_files_;
    }

    void set_source_input_files(std::vector<std::string> source_input_files) {
        source_input_files_ = std::move(source_input_files);
    }

    const std::vector<std::string> &get_linker_input_files() const noexcept {
        return linker_input_files_;
    }

    void set_linker_input_files(std::vector<std::string> linker_input_files) {
        linker_input_files_ = std::move(linker_input_files);
    }

    bool get_link_only() const noexcept { return link_only_; }

    void set_link_only(bool link_only) noexcept { link_only_ = link_only; }

    const std::string &get_output_file() const noexcept { return output_file_; }

    void set_output_file(std::string output_file) {
        output_file_ = std::move(output_file);
    }

    bool get_generate_depfile() const noexcept {
        return depfile_mode_ != DepfileMode::None;
    }

    void set_generate_depfile(bool generate_depfile) noexcept {
        depfile_mode_ = generate_depfile ? DepfileMode::MMD : DepfileMode::None;
    }

    DepfileMode get_depfile_mode() const noexcept { return depfile_mode_; }

    void set_depfile_mode(DepfileMode depfile_mode) noexcept {
        depfile_mode_ = depfile_mode;
    }

    const std::string &get_depfile_output_file() const noexcept {
        return depfile_output_file_;
    }

    void set_depfile_output_file(std::string depfile_output_file) {
        depfile_output_file_ = std::move(depfile_output_file);
    }

    const std::vector<DepfileTargetOption> &get_depfile_targets() const noexcept {
        return depfile_targets_;
    }

    void set_depfile_targets(std::vector<DepfileTargetOption> depfile_targets) {
        depfile_targets_ = std::move(depfile_targets);
    }

    bool get_depfile_add_phony_targets() const noexcept {
        return depfile_add_phony_targets_;
    }

    void set_depfile_add_phony_targets(bool depfile_add_phony_targets) noexcept {
        depfile_add_phony_targets_ = depfile_add_phony_targets;
    }

    const std::vector<std::string> &get_include_directories() const noexcept {
        return include_directories_;
    }

    void set_include_directories(std::vector<std::string> include_directories) {
        include_directories_ = std::move(include_directories);
    }

    void add_include_directory(std::string include_directory) {
        include_directories_.push_back(std::move(include_directory));
    }

    const std::vector<std::string> &
    get_quote_include_directories() const noexcept {
        return quote_include_directories_;
    }

    void set_quote_include_directories(
        std::vector<std::string> quote_include_directories) {
        quote_include_directories_ = std::move(quote_include_directories);
    }

    const std::vector<std::string> &
    get_system_include_directories() const noexcept {
        return system_include_directories_;
    }

    void set_system_include_directories(
        std::vector<std::string> system_include_directories) {
        system_include_directories_ = std::move(system_include_directories);
    }

    const std::vector<std::string> &
    get_after_system_include_directories() const noexcept {
        return after_system_include_directories_;
    }

    void set_after_system_include_directories(
        std::vector<std::string> after_system_include_directories) {
        after_system_include_directories_ =
            std::move(after_system_include_directories);
    }

    const std::vector<CommandLineMacroOption> &
    get_command_line_macro_options() const noexcept {
        return command_line_macro_options_;
    }

    void set_command_line_macro_options(
        std::vector<CommandLineMacroOption> command_line_macro_options) {
        command_line_macro_options_ = std::move(command_line_macro_options);
    }

    const std::vector<std::string> &get_forced_include_files() const noexcept {
        return forced_include_files_;
    }

    void set_forced_include_files(std::vector<std::string> forced_include_files) {
        forced_include_files_ = std::move(forced_include_files);
    }

    const std::string &get_sysroot() const noexcept { return sysroot_; }

    void set_sysroot(std::string sysroot) { sysroot_ = std::move(sysroot); }

    const std::string &get_isysroot() const noexcept { return isysroot_; }

    void set_isysroot(std::string isysroot) {
        isysroot_ = std::move(isysroot);
    }

    const std::vector<std::string> &get_linker_search_directories() const noexcept {
        return linker_search_directories_;
    }

    void set_linker_search_directories(
        std::vector<std::string> linker_search_directories) {
        linker_search_directories_ = std::move(linker_search_directories);
    }

    const std::vector<std::string> &get_linker_libraries() const noexcept {
        return linker_libraries_;
    }

    void set_linker_libraries(std::vector<std::string> linker_libraries) {
        linker_libraries_ = std::move(linker_libraries);
    }

    const std::vector<std::string> &
    get_linker_passthrough_arguments() const noexcept {
        return linker_passthrough_arguments_;
    }

    void set_linker_passthrough_arguments(
        std::vector<std::string> linker_passthrough_arguments) {
        linker_passthrough_arguments_ = std::move(linker_passthrough_arguments);
    }

    bool get_link_with_pthread() const noexcept { return link_with_pthread_; }

    void set_link_with_pthread(bool link_with_pthread) noexcept {
        link_with_pthread_ = link_with_pthread;
    }

    bool get_no_stdinc() const noexcept { return no_stdinc_; }

    void set_no_stdinc(bool no_stdinc) noexcept { no_stdinc_ = no_stdinc; }

    bool dump_tokens() const noexcept { return dump_tokens_; }

    void set_dump_tokens(bool dump_tokens) noexcept {
        dump_tokens_ = dump_tokens;
    }

    bool dump_ast() const noexcept { return dump_ast_; }

    void set_dump_ast(bool dump_ast) noexcept { dump_ast_ = dump_ast; }

    bool dump_parse() const noexcept { return dump_parse_; }

    void set_dump_parse(bool dump_parse) noexcept { dump_parse_ = dump_parse; }

    bool dump_ir() const noexcept { return dump_ir_; }

    void set_dump_ir(bool dump_ir) noexcept { dump_ir_ = dump_ir; }

    bool dump_core_ir() const noexcept { return dump_core_ir_; }

    void set_dump_core_ir(bool dump_core_ir) noexcept {
        dump_core_ir_ = dump_core_ir;
    }

    bool emit_asm() const noexcept { return emit_asm_; }

    void set_emit_asm(bool emit_asm) noexcept { emit_asm_ = emit_asm; }

    bool emit_object() const noexcept { return emit_object_; }

    void set_emit_object(bool emit_object) noexcept { emit_object_ = emit_object; }

    StopAfterStage get_stop_after_stage() const noexcept {
        return stop_after_stage_;
    }

    void set_stop_after_stage(StopAfterStage stop_after_stage) noexcept {
        stop_after_stage_ = stop_after_stage;
    }

    DriverAction get_driver_action() const noexcept { return driver_action_; }

    void set_driver_action(DriverAction driver_action) noexcept {
        driver_action_ = driver_action;
    }

    LanguageMode get_language_mode() const noexcept { return language_mode_; }

    void set_language_mode(LanguageMode language_mode) noexcept {
        language_mode_ = language_mode;
    }

    OptimizationLevel get_optimization_level() const noexcept {
        return optimization_level_;
    }

    void set_optimization_level(OptimizationLevel optimization_level) noexcept {
        optimization_level_ = optimization_level;
    }

    bool get_enable_gnu_dialect() const noexcept {
        return enable_gnu_dialect_;
    }

    void set_enable_gnu_dialect(bool enable_gnu_dialect) noexcept {
        enable_gnu_dialect_ = enable_gnu_dialect;
    }

    bool get_enable_clang_dialect() const noexcept {
        return enable_clang_dialect_;
    }

    void set_enable_clang_dialect(bool enable_clang_dialect) noexcept {
        enable_clang_dialect_ = enable_clang_dialect;
    }

    bool get_enable_builtin_type_extension_pack() const noexcept {
        return enable_builtin_type_extension_pack_;
    }

    void set_enable_builtin_type_extension_pack(
        bool enable_builtin_type_extension_pack) noexcept {
        enable_builtin_type_extension_pack_ =
            enable_builtin_type_extension_pack;
    }

    bool get_verbose() const noexcept { return verbose_; }

    void set_verbose(bool verbose) noexcept { verbose_ = verbose; }

    const WarningPolicy &get_warning_policy() const noexcept {
        return warning_policy_;
    }

    WarningPolicy &get_warning_policy() noexcept { return warning_policy_; }

    void set_warning_policy(WarningPolicy warning_policy) {
        warning_policy_ = std::move(warning_policy);
    }

    const BackendOptions &get_backend_options() const noexcept {
        return backend_options_;
    }

    BackendOptions &get_backend_options() noexcept { return backend_options_; }

    void set_backend_options(BackendOptions backend_options) {
        backend_options_ = std::move(backend_options);
    }
};

} // namespace sysycc
