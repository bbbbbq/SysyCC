#include "cli.hpp"

#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>

#include "common/diagnostic/warning_options.hpp"

namespace ClI {
namespace {

bool parse_stop_after_stage(const std::string &stage_name,
                            sysycc::StopAfterStage &stage) {
    if (stage_name == "preprocess") {
        stage = sysycc::StopAfterStage::Preprocess;
        return true;
    }
    if (stage_name == "lex") {
        stage = sysycc::StopAfterStage::Lex;
        return true;
    }
    if (stage_name == "parse") {
        stage = sysycc::StopAfterStage::Parse;
        return true;
    }
    if (stage_name == "ast") {
        stage = sysycc::StopAfterStage::Ast;
        return true;
    }
    if (stage_name == "semantic") {
        stage = sysycc::StopAfterStage::Semantic;
        return true;
    }
    if (stage_name == "core-ir") {
        stage = sysycc::StopAfterStage::CoreIr;
        return true;
    }
    if (stage_name == "llvm-ir") {
        stage = sysycc::StopAfterStage::IR;
        return true;
    }
    if (stage_name == "ir") {
        stage = sysycc::StopAfterStage::IR;
        return true;
    }
    if (stage_name == "asm") {
        stage = sysycc::StopAfterStage::Asm;
        return true;
    }
    return false;
}

bool parse_backend_kind(const std::string &backend_name,
                        sysycc::BackendKind &backend_kind) {
    if (backend_name == "llvm-ir") {
        backend_kind = sysycc::BackendKind::LlvmIr;
        return true;
    }
    if (backend_name == "aarch64-native") {
        backend_kind = sysycc::BackendKind::AArch64Native;
        return true;
    }
    if (backend_name == "riscv64-native") {
        backend_kind = sysycc::BackendKind::Riscv64Native;
        return true;
    }
    return false;
}

const char *default_target_triple_for_backend(sysycc::BackendKind backend_kind) {
    switch (backend_kind) {
    case sysycc::BackendKind::AArch64Native:
        return "aarch64-unknown-linux-gnu";
    case sysycc::BackendKind::Riscv64Native:
        return "riscv64-unknown-linux-gnu";
    case sysycc::BackendKind::LlvmIr:
        return "";
    }
    return "";
}

std::optional<sysycc::BackendKind>
infer_backend_kind_from_target_triple(const std::string &target_triple) {
    if (target_triple.find("aarch64") != std::string::npos) {
        return sysycc::BackendKind::AArch64Native;
    }
    if (target_triple.find("riscv64") != std::string::npos) {
        return sysycc::BackendKind::Riscv64Native;
    }
    return std::nullopt;
}

const char *backend_kind_to_string(sysycc::BackendKind backend_kind) {
    switch (backend_kind) {
    case sysycc::BackendKind::LlvmIr:
        return "llvm-ir";
    case sysycc::BackendKind::AArch64Native:
        return "aarch64-native";
    case sysycc::BackendKind::Riscv64Native:
        return "riscv64-native";
    }
    return "unknown";
}

bool parse_language_mode(const std::string &mode_name,
                         sysycc::LanguageMode &language_mode) {
    if (mode_name == "c99") {
        language_mode = sysycc::LanguageMode::C99;
        return true;
    }
    if (mode_name == "iso9899:1999") {
        language_mode = sysycc::LanguageMode::C99;
        return true;
    }
    if (mode_name == "c11") {
        language_mode = sysycc::LanguageMode::C11;
        return true;
    }
    if (mode_name == "iso9899:2011") {
        language_mode = sysycc::LanguageMode::C11;
        return true;
    }
    if (mode_name == "c17" || mode_name == "c18") {
        language_mode = sysycc::LanguageMode::C17;
        return true;
    }
    if (mode_name == "iso9899:2017" || mode_name == "iso9899:2018") {
        language_mode = sysycc::LanguageMode::C17;
        return true;
    }
    if (mode_name == "c2x" || mode_name == "c23") {
        language_mode = sysycc::LanguageMode::C2x;
        return true;
    }
    if (mode_name == "gnu99") {
        language_mode = sysycc::LanguageMode::Gnu99;
        return true;
    }
    if (mode_name == "gnu11") {
        language_mode = sysycc::LanguageMode::Gnu11;
        return true;
    }
    if (mode_name == "gnu17") {
        language_mode = sysycc::LanguageMode::Gnu17;
        return true;
    }
    if (mode_name == "gnu18") {
        language_mode = sysycc::LanguageMode::Gnu17;
        return true;
    }
    if (mode_name == "gnu2x" || mode_name == "gnu23") {
        language_mode = sysycc::LanguageMode::Gnu2x;
        return true;
    }
    if (mode_name == "sysy") {
        language_mode = sysycc::LanguageMode::Sysy;
        return true;
    }
    return false;
}

bool parse_optimization_level(const std::string &arg,
                              sysycc::OptimizationLevel &optimization_level,
                              std::string &unsupported_level) {
    if (arg == "-O" || arg == "-O1") {
        optimization_level = sysycc::OptimizationLevel::O1;
        return true;
    }
    if (arg == "-O2" || arg == "-O3" || arg == "-Os" || arg == "-Og" ||
        arg == "-Oz" || arg == "-Ofast") {
        optimization_level = sysycc::OptimizationLevel::O1;
        return true;
    }
    if (arg == "-O0") {
        optimization_level = sysycc::OptimizationLevel::O0;
        return true;
    }
    if (arg.rfind("-O", 0) == 0) {
        unsupported_level = arg.substr(2);
    }
    return false;
}

void apply_language_mode_defaults(sysycc::LanguageMode language_mode,
                                  bool &enable_gnu_dialect,
                                  bool &enable_clang_dialect,
                                  bool &enable_builtin_type_extension_pack) {
    switch (language_mode) {
    case sysycc::LanguageMode::C99:
    case sysycc::LanguageMode::C11:
    case sysycc::LanguageMode::C17:
    case sysycc::LanguageMode::C2x:
        enable_gnu_dialect = false;
        enable_clang_dialect = false;
        enable_builtin_type_extension_pack = false;
        return;
    case sysycc::LanguageMode::Gnu99:
    case sysycc::LanguageMode::Gnu11:
    case sysycc::LanguageMode::Gnu17:
    case sysycc::LanguageMode::Gnu2x:
        enable_gnu_dialect = true;
        enable_clang_dialect = false;
        enable_builtin_type_extension_pack = false;
        return;
    case sysycc::LanguageMode::Sysy:
        enable_gnu_dialect = true;
        enable_clang_dialect = true;
        enable_builtin_type_extension_pack = true;
        return;
    }
}

bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_valid_identifier(const std::string &text) {
    if (text.empty() || !is_identifier_start(text.front())) {
        return false;
    }
    for (char ch : text) {
        if (!is_identifier_char(ch)) {
            return false;
        }
    }
    return true;
}

std::string detect_program_name(const char *argv0) {
    if (argv0 == nullptr || argv0[0] == '\0') {
        return "compiler";
    }
    const std::filesystem::path executable_path(argv0);
    const std::string file_name = executable_path.filename().string();
    if (file_name.empty()) {
        return "compiler";
    }

    std::string normalized_name = file_name;
    for (char &ch : normalized_name) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (normalized_name == "sysycc") {
        return "compiler";
    }
    return normalized_name;
}

std::optional<std::string> parse_warning_option_name(
    const std::string &arg, const std::string &prefix) {
    if (arg.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    const std::string option_name = arg.substr(prefix.size());
    if (option_name.empty() ||
        !sysycc::warning_options::is_known_warning_option(option_name)) {
        return std::nullopt;
    }
    return option_name;
}

std::string lowercase_copy(std::string text) {
    for (char &ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

bool is_linker_input_file_path(const std::string &path) {
    const std::string extension =
        lowercase_copy(std::filesystem::path(path).extension().string());
    return extension == ".o" || extension == ".obj" || extension == ".a" ||
           extension == ".so" || extension == ".dylib" || extension == ".lo";
}

bool read_text_file(const std::string &path, std::string &text,
                    std::string &error_message) {
    std::ifstream input(path);
    if (!input.is_open()) {
        error_message = "failed to open response file '" + path + "'";
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
    if (input.bad()) {
        error_message = "failed to read response file '" + path + "'";
        return false;
    }
    return true;
}

bool parse_response_file_text(const std::string &text,
                              std::vector<std::string> &arguments,
                              std::string &error_message) {
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;
    bool escaping = false;

    const auto flush_argument = [&]() {
        if (!current.empty()) {
            arguments.push_back(current);
            current.clear();
        }
    };

    for (char ch : text) {
        if (escaping) {
            current.push_back(ch);
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (in_single_quote) {
            if (ch == '\'') {
                in_single_quote = false;
            } else {
                current.push_back(ch);
            }
            continue;
        }
        if (in_double_quote) {
            if (ch == '"') {
                in_double_quote = false;
            } else {
                current.push_back(ch);
            }
            continue;
        }
        if (ch == '\'') {
            in_single_quote = true;
            continue;
        }
        if (ch == '"') {
            in_double_quote = true;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            flush_argument();
            continue;
        }
        current.push_back(ch);
    }

    if (escaping) {
        current.push_back('\\');
    }
    if (in_single_quote || in_double_quote) {
        error_message = "unterminated quote in response file";
        return false;
    }
    flush_argument();
    return true;
}

bool expand_response_arguments(const std::vector<std::string> &input_arguments,
                               std::vector<std::string> &expanded_arguments,
                               std::string &error_message,
                               unsigned depth = 0) {
    if (depth > 16) {
        error_message = "response file nesting is too deep";
        return false;
    }

    for (const std::string &argument : input_arguments) {
        if (argument.size() <= 1 || argument.front() != '@') {
            expanded_arguments.push_back(argument);
            continue;
        }

        const std::string response_file = argument.substr(1);
        std::string text;
        if (!read_text_file(response_file, text, error_message)) {
            return false;
        }
        std::vector<std::string> nested_arguments;
        if (!parse_response_file_text(text, nested_arguments, error_message)) {
            error_message += " in '" + response_file + "'";
            return false;
        }
        if (!expand_response_arguments(nested_arguments, expanded_arguments,
                                       error_message, depth + 1)) {
            return false;
        }
    }

    return true;
}

} // namespace

bool Cli::finalize_driver_mode() {
    if (depfile_mode_ == sysycc::DepfileMode::None &&
        !depfile_output_file_.empty()) {
        emit_error("'-MF' requires '-MD' or '-MMD'");
        return false;
    }
    if (depfile_mode_ == sysycc::DepfileMode::None &&
        !depfile_targets_.empty()) {
        emit_error("'-MT' and '-MQ' require '-MD' or '-MMD'");
        return false;
    }
    if (depfile_mode_ == sysycc::DepfileMode::None && depfile_add_phony_targets_) {
        emit_error("'-MP' requires '-MD' or '-MMD'");
        return false;
    }

    if (request_emit_llvm_ && !request_emit_assembly_) {
        emit_error("'-emit-llvm' requires '-S'");
        return false;
    }

    if (request_preprocess_only_ && request_emit_assembly_) {
        emit_error("'-E' cannot be used with '-S'");
        return false;
    }
    if (request_preprocess_only_ && request_syntax_only_) {
        emit_error("'-E' cannot be used with '-fsyntax-only'");
        return false;
    }
    if (request_syntax_only_ && request_emit_assembly_) {
        emit_error("'-fsyntax-only' cannot be used with '-S'");
        return false;
    }
    if (request_compile_only_ &&
        (request_preprocess_only_ || request_syntax_only_ ||
         request_emit_assembly_ || request_emit_llvm_)) {
        emit_error("'-c' cannot be combined with other output mode options");
        return false;
    }

    if (request_preprocess_only_) {
        driver_action_ = sysycc::DriverAction::PreprocessOnly;
    } else if (request_syntax_only_) {
        driver_action_ = sysycc::DriverAction::SyntaxOnly;
    } else if (request_compile_only_) {
        driver_action_ = sysycc::DriverAction::CompileOnly;
    } else if (request_emit_assembly_ && request_emit_llvm_) {
        driver_action_ = sysycc::DriverAction::EmitLlvmIr;
    } else if (request_emit_assembly_) {
        driver_action_ = sysycc::DriverAction::EmitAssembly;
    } else {
        if (internal_pipeline_requested_) {
            driver_action_ = sysycc::DriverAction::InternalPipeline;
        } else {
            driver_action_ = sysycc::DriverAction::FullCompile;
        }
    }

    if (depfile_mode_ != sysycc::DepfileMode::None &&
        (driver_action_ == sysycc::DriverAction::PreprocessOnly ||
         driver_action_ == sysycc::DriverAction::SyntaxOnly ||
         driver_action_ == sysycc::DriverAction::InternalPipeline)) {
        emit_error("dependency generation is only supported with output-producing driver actions such as -c, -S, or full-compile linking");
        return false;
    }

    if (!explicit_optimization_level_ &&
        driver_action_ == sysycc::DriverAction::InternalPipeline) {
        optimization_level_ = sysycc::OptimizationLevel::O1;
    }

    if (!explicit_stop_after_) {
        switch (driver_action_) {
        case sysycc::DriverAction::PreprocessOnly:
            stop_after_stage_ = sysycc::StopAfterStage::Preprocess;
            break;
        case sysycc::DriverAction::SyntaxOnly:
            stop_after_stage_ = sysycc::StopAfterStage::Semantic;
            break;
        case sysycc::DriverAction::EmitAssembly:
            stop_after_stage_ = sysycc::StopAfterStage::Asm;
            break;
        case sysycc::DriverAction::EmitLlvmIr:
            stop_after_stage_ = sysycc::StopAfterStage::IR;
            break;
        case sysycc::DriverAction::InternalPipeline:
        case sysycc::DriverAction::FullCompile:
        case sysycc::DriverAction::CompileOnly:
            stop_after_stage_ = sysycc::StopAfterStage::None;
            break;
        }
    }

    emit_asm_ = driver_action_ == sysycc::DriverAction::EmitAssembly;
    if (driver_action_ == sysycc::DriverAction::CompileOnly) {
        emit_asm_ = false;
    }

    // Infer backend from --target if --backend was not explicitly given.
    if (!explicit_backend_ && !target_triple_.empty()) {
        if (const auto inferred =
                infer_backend_kind_from_target_triple(target_triple_);
            inferred.has_value()) {
            backend_kind_ = *inferred;
        }
    }

    // If still no backend chosen, fall back to defaults.
    if (!explicit_backend_ && target_triple_.empty()) {
        backend_kind_ = (driver_action_ == sysycc::DriverAction::EmitAssembly)
                            ? sysycc::BackendKind::AArch64Native
                            : sysycc::BackendKind::LlvmIr;
    }

    // Validate consistency when both --backend and --target are given.
    if (explicit_backend_ && !target_triple_.empty()) {
        const auto expected_backend =
            infer_backend_kind_from_target_triple(target_triple_);
        if (expected_backend.has_value() &&
            *expected_backend != backend_kind_) {
            emit_error("--target=" + target_triple_ +
                       " is incompatible with --backend=" +
                       backend_kind_to_string(backend_kind_));
            return false;
        }
    }

    // Fill in default target triple for native backends if none was given.
    if ((backend_kind_ == sysycc::BackendKind::AArch64Native ||
         backend_kind_ == sysycc::BackendKind::Riscv64Native) &&
        target_triple_.empty()) {
        target_triple_ = default_target_triple_for_backend(backend_kind_);
    }

    return true;
}

bool Cli::finalize_inputs() {
    input_file_.clear();
    source_input_files_.clear();
    linker_input_files_.clear();
    link_only_ = false;

    std::vector<std::string> source_inputs;
    std::vector<std::string> link_inputs;
    for (const std::string &input : positional_inputs_) {
        if (!force_c_input_language_ && is_linker_input_file_path(input)) {
            link_inputs.push_back(input);
        } else {
            source_inputs.push_back(input);
        }
    }

    if (driver_action_ == sysycc::DriverAction::FullCompile) {
        if (source_inputs.empty()) {
            if (link_inputs.empty()) {
                emit_error("missing input file");
                return false;
            }
            if (depfile_mode_ != sysycc::DepfileMode::None) {
                emit_error("dependency generation requires a source input in the current invocation");
                return false;
            }
            linker_input_files_ = std::move(link_inputs);
            link_only_ = true;
            return true;
        }
        if (source_inputs.size() > 1 &&
            depfile_mode_ != sysycc::DepfileMode::None) {
            emit_error("dependency generation with multiple source inputs is not supported yet; compile sources separately");
            return false;
        }

        input_file_ = source_inputs.front();
        source_input_files_ = std::move(source_inputs);
        linker_input_files_ = std::move(link_inputs);
        return true;
    }

    if (!link_inputs.empty()) {
        emit_error("linker input files are only supported during full-compile linking");
        return false;
    }
    if (source_inputs.empty()) {
        emit_error("missing input file");
        return false;
    }
    if (source_inputs.size() > 1) {
        if (driver_action_ == sysycc::DriverAction::CompileOnly) {
            if (!output_file_.empty()) {
                emit_error("cannot specify '-o' with '-c' and multiple source inputs");
                return false;
            }
            if (!depfile_output_file_.empty()) {
                emit_error("'-MF' with '-c' and multiple source inputs is not supported yet; compile sources separately");
                return false;
            }
            if (!depfile_targets_.empty()) {
                emit_error("'-MT' and '-MQ' with '-c' and multiple source inputs are not supported yet; compile sources separately");
                return false;
            }
            input_file_ = source_inputs.front();
            source_input_files_ = std::move(source_inputs);
            return true;
        }
        emit_error("multiple input files are not yet supported: '" +
                   source_inputs.back() + "'");
        return false;
    }

    input_file_ = source_inputs.front();
    source_input_files_ = std::move(source_inputs);
    return true;
}

void Cli::Run(int argc, char *argv[]) {
    program_name_ = detect_program_name(argc > 0 ? argv[0] : nullptr);
    positional_inputs_.clear();
    input_file_.clear();
    source_input_files_.clear();
    linker_input_files_.clear();
    link_only_ = false;
    output_file_.clear();
    depfile_mode_ = sysycc::DepfileMode::None;
    depfile_output_file_.clear();
    depfile_targets_.clear();
    depfile_add_phony_targets_ = false;
    include_directories_.clear();
    quote_include_directories_.clear();
    system_include_directories_.clear();
    after_system_include_directories_.clear();
    command_line_macro_options_.clear();
    forced_include_files_.clear();
    sysroot_.clear();
    isysroot_.clear();
    linker_search_directories_.clear();
    linker_libraries_.clear();
    linker_passthrough_arguments_.clear();
    link_with_pthread_ = false;
    no_stdinc_ = false;
    dump_tokens_ = false;
    dump_parse_ = false;
    dump_ast_ = false;
    dump_ir_ = false;
    dump_core_ir_ = false;
    emit_asm_ = false;
    stop_after_stage_ = sysycc::StopAfterStage::None;
    explicit_stop_after_ = false;
    internal_pipeline_requested_ = false;
    driver_action_ = sysycc::DriverAction::InternalPipeline;
    language_mode_ = sysycc::LanguageMode::Sysy;
    optimization_level_ = sysycc::OptimizationLevel::O0;
    explicit_optimization_level_ = false;
    enable_gnu_dialect_ = true;
    enable_clang_dialect_ = true;
    enable_builtin_type_extension_pack_ = true;
    verbose_ = false;
    warning_policy_ = sysycc::WarningPolicy();
    backend_kind_ = sysycc::BackendKind::LlvmIr;
    explicit_backend_ = false;
    target_triple_.clear();
    force_c_input_language_ = false;
    request_preprocess_only_ = false;
    request_syntax_only_ = false;
    request_emit_assembly_ = false;
    request_emit_llvm_ = false;
    request_compile_only_ = false;
    request_position_independent_ = false;
    request_debug_info_ = false;
    is_help_ = false;
    is_version_ = false;
    has_error_ = false;

    if (argc <= 1) {
        is_help_ = true;
        PrintHelp();
        return;
    }

    std::vector<std::string> raw_arguments;
    raw_arguments.reserve(static_cast<std::size_t>(argc - 1));
    for (int index = 1; index < argc; ++index) {
        raw_arguments.emplace_back(argv[index]);
    }

    std::vector<std::string> expanded_arguments;
    std::string response_file_error;
    if (!expand_response_arguments(raw_arguments, expanded_arguments,
                                   response_file_error)) {
        emit_error(response_file_error);
        return;
    }

    for (std::size_t i = 0; i < expanded_arguments.size(); ++i) {
        const std::string arg = expanded_arguments[i];

        if (arg == "-h" || arg == "--help") {
            is_help_ = true;
            PrintHelp();
            return;
        }

        if (arg == "--version") {
            is_version_ = true;
            PrintVersion();
            return;
        }

        if (arg == "-v") {
            verbose_ = true;
            continue;
        }

        if (arg == "-E") {
            request_preprocess_only_ = true;
            continue;
        }

        if (arg == "-fsyntax-only") {
            request_syntax_only_ = true;
            continue;
        }

        if (arg == "-emit-llvm") {
            request_emit_llvm_ = true;
            continue;
        }

        std::string unsupported_optimization_level;
        if (parse_optimization_level(arg, optimization_level_,
                                     unsupported_optimization_level)) {
            explicit_optimization_level_ = true;
            continue;
        }
        if (arg.rfind("-O", 0) == 0) {
            emit_error("argument to '-O' is not supported: '" +
                       unsupported_optimization_level + "'");
            return;
        }

        if (arg == "-c") {
            request_compile_only_ = true;
            continue;
        }

        if (arg == "-x" || (arg.size() > 2 && arg.rfind("-x", 0) == 0)) {
            const std::string language_name =
                arg == "-x" ? (i + 1 < expanded_arguments.size()
                                   ? expanded_arguments[++i]
                                   : std::string())
                            : arg.substr(2);
            if (language_name.empty()) {
                emit_error("missing argument to '-x'");
                return;
            }
            if (language_name != "c") {
                emit_error("argument to '-x' is not supported: '" +
                           language_name + "'");
                return;
            }
            force_c_input_language_ = true;
            continue;
        }

        if (arg == "-ansi" || arg == "-pedantic" ||
            arg == "-pedantic-errors") {
            continue;
        }

        if (arg == "-pipe" || arg == "-ffunction-sections" ||
            arg == "-fdata-sections" || arg == "-fno-common" ||
            arg == "-fno-strict-aliasing" || arg == "-fwrapv" ||
            arg == "-funsigned-char" || arg == "-fsigned-char" ||
            arg == "-fno-strict-overflow" ||
            arg == "-fno-delete-null-pointer-checks" ||
            arg == "-fno-tree-vectorize" || arg == "-fno-inline" ||
            arg == "-ffreestanding" || arg == "-fhosted" ||
            arg == "-fno-plt" ||
            arg == "-fno-asynchronous-unwind-tables" ||
            arg == "-fasynchronous-unwind-tables" ||
            arg == "-funwind-tables" || arg == "-fno-unwind-tables" ||
            arg == "-fmerge-all-constants" || arg == "-fno-merge-constants" ||
            arg == "-fno-ident" || arg == "-fstrict-aliasing" ||
            arg == "-fno-math-errno" || arg == "-fmath-errno" ||
            arg == "-frounding-math" || arg == "-ftrapping-math" ||
            arg == "-fno-trapping-math" ||
            arg == "-fno-lto" ||
            arg == "-fno-builtin" || arg == "-fno-stack-protector" ||
            arg == "-fstack-protector" || arg == "-fstack-protector-strong" ||
            arg == "-fstack-protector-all" || arg == "-fomit-frame-pointer" ||
            arg == "-fno-omit-frame-pointer" || arg == "-fno-exceptions" ||
            arg == "-fno-rtti" || arg == "-Winvalid-pch" ||
            arg == "-fvisibility=hidden" || arg == "-fcolor-diagnostics" ||
            arg == "-fno-color-diagnostics" || arg == "-Qunused-arguments" ||
            arg == "-m64" || arg == "-mno-red-zone") {
            continue;
        }

        if (arg.rfind("-fdebug-prefix-map=", 0) == 0 ||
            arg.rfind("-ffile-prefix-map=", 0) == 0 ||
            arg.rfind("-fmacro-prefix-map=", 0) == 0 ||
            arg.rfind("-fdiagnostics-color=", 0) == 0) {
            continue;
        }

        if (arg == "-arch") {
            const std::string arch_name =
                i + 1 < expanded_arguments.size()
                    ? expanded_arguments[++i]
                    : std::string();
            if (arch_name.empty()) {
                emit_error("missing argument to '-arch'");
                return;
            }
            if (arch_name != "arm64" && arch_name != "aarch64") {
                emit_error("argument to '-arch' is not supported: '" +
                           arch_name + "'");
                return;
            }
            continue;
        }

        if (arg.rfind("-fvisibility=", 0) == 0) {
            const std::string visibility_mode =
                arg.substr(std::string("-fvisibility=").size());
            if (visibility_mode != "hidden" && visibility_mode != "default" &&
                visibility_mode != "internal") {
                emit_error("argument to '-fvisibility=' is not supported: '" +
                           visibility_mode + "'");
                return;
            }
            continue;
        }

        if (arg == "-fPIC" || arg == "-fPIE" || arg == "-fpie") {
            request_position_independent_ = true;
            continue;
        }

        if (arg == "-fno-pie" || arg == "-fno-pic" || arg == "-fno-PIC") {
            request_position_independent_ = false;
            continue;
        }

        if (arg == "-g") {
            request_debug_info_ = true;
            continue;
        }

        if (arg == "-pthread") {
            link_with_pthread_ = true;
            continue;
        }

        if (arg == "-MD") {
            depfile_mode_ = sysycc::DepfileMode::MD;
            continue;
        }

        if (arg == "-MMD") {
            depfile_mode_ = sysycc::DepfileMode::MMD;
            continue;
        }

        if (arg == "-MP") {
            depfile_add_phony_targets_ = true;
            continue;
        }

        if (arg == "-MF" || (arg.size() > 3 && arg.rfind("-MF", 0) == 0)) {
            depfile_output_file_ =
                arg == "-MF" ? (i + 1 < expanded_arguments.size()
                                    ? expanded_arguments[++i]
                                    : std::string())
                             : arg.substr(3);
            if (depfile_output_file_.empty()) {
                emit_error("missing argument to '-MF'");
                return;
            }
            continue;
        }

        if (arg == "-MT" || (arg.size() > 3 && arg.rfind("-MT", 0) == 0)) {
            const std::string dep_target =
                arg == "-MT" ? (i + 1 < expanded_arguments.size()
                                    ? expanded_arguments[++i]
                                    : std::string())
                             : arg.substr(3);
            if (dep_target.empty()) {
                emit_error("missing argument to '-MT'");
                return;
            }
            depfile_targets_.emplace_back(dep_target, false);
            continue;
        }

        if (arg == "-MQ" || (arg.size() > 3 && arg.rfind("-MQ", 0) == 0)) {
            const std::string dep_target =
                arg == "-MQ" ? (i + 1 < expanded_arguments.size()
                                    ? expanded_arguments[++i]
                                    : std::string())
                             : arg.substr(3);
            if (dep_target.empty()) {
                emit_error("missing argument to '-MQ'");
                return;
            }
            depfile_targets_.emplace_back(dep_target, true);
            continue;
        }

        if (arg == "--dump-tokens" || arg == "--sysy-dump-tokens") {
            internal_pipeline_requested_ = true;
            dump_tokens_ = true;
            continue;
        }

        if (arg == "--dump-parse" || arg == "--sysy-dump-parse") {
            internal_pipeline_requested_ = true;
            dump_parse_ = true;
            continue;
        }

        if (arg == "--dump-ast" || arg == "--sysy-dump-ast") {
            internal_pipeline_requested_ = true;
            dump_ast_ = true;
            continue;
        }

        if (arg == "--dump-ir" || arg == "--sysy-dump-ir") {
            internal_pipeline_requested_ = true;
            dump_ir_ = true;
            continue;
        }

        if (arg == "--dump-core-ir" || arg == "--sysy-dump-core-ir") {
            internal_pipeline_requested_ = true;
            dump_core_ir_ = true;
            continue;
        }

        if (arg == "-S") {
            request_emit_assembly_ = true;
            continue;
        }

        if (arg.rfind("--backend=", 0) == 0 ||
            arg.rfind("--sysy-backend=", 0) == 0) {
            const std::string backend_name =
                arg.substr(arg.find('=') + 1);
            if (!parse_backend_kind(backend_name, backend_kind_)) {
                emit_error("invalid backend kind: " + backend_name);
                return;
            }
            internal_pipeline_requested_ = true;
            explicit_backend_ = true;
            continue;
        }

        if (arg == "--backend" || arg == "--sysy-backend") {
            if (i + 1 >= expanded_arguments.size()) {
                emit_error("missing argument to '" + arg + "'");
                return;
            }

            const std::string backend_name = expanded_arguments[++i];
            if (!parse_backend_kind(backend_name, backend_kind_)) {
                emit_error("invalid backend kind: " + backend_name);
                return;
            }
            internal_pipeline_requested_ = true;
            explicit_backend_ = true;
            continue;
        }

        if (arg.rfind("--target=", 0) == 0 ||
            arg.rfind("--sysy-target=", 0) == 0) {
            internal_pipeline_requested_ = true;
            target_triple_ = arg.substr(std::string("--target=").size());
            if (arg.rfind("--sysy-target=", 0) == 0) {
                target_triple_ = arg.substr(std::string("--sysy-target=").size());
            }
            continue;
        }

        if (arg == "--target" || arg == "--sysy-target") {
            if (i + 1 >= expanded_arguments.size()) {
                emit_error("missing argument to '" + arg + "'");
                return;
            }

            internal_pipeline_requested_ = true;
            target_triple_ = expanded_arguments[++i];
            continue;
        }

        if (arg.rfind("--sysroot=", 0) == 0) {
            sysroot_ = arg.substr(std::string("--sysroot=").size());
            if (sysroot_.empty()) {
                emit_error("missing argument to '--sysroot'");
                return;
            }
            continue;
        }

        if (arg == "--sysroot") {
            if (i + 1 >= expanded_arguments.size()) {
                emit_error("missing argument to '--sysroot'");
                return;
            }
            sysroot_ = expanded_arguments[++i];
            continue;
        }

        if (arg.rfind("--stop-after=", 0) == 0 ||
            arg.rfind("--sysy-stop-after=", 0) == 0) {
            const std::size_t equals_index = arg.find('=');
            const std::string stage_name = arg.substr(equals_index + 1);
            if (!parse_stop_after_stage(stage_name, stop_after_stage_)) {
                emit_error("invalid stop-after stage: " + stage_name);
                return;
            }
            internal_pipeline_requested_ = true;
            explicit_stop_after_ = true;
            continue;
        }

        if (arg == "--stop-after" || arg == "--sysy-stop-after") {
            if (i + 1 >= expanded_arguments.size()) {
                emit_error("missing argument to '" + arg + "'");
                return;
            }

            const std::string stage_name = expanded_arguments[++i];
            if (!parse_stop_after_stage(stage_name, stop_after_stage_)) {
                emit_error("invalid stop-after stage: " + stage_name);
                return;
            }
            internal_pipeline_requested_ = true;
            explicit_stop_after_ = true;
            continue;
        }

        if (arg == "--strict-c99") {
            language_mode_ = sysycc::LanguageMode::C99;
            apply_language_mode_defaults(
                language_mode_, enable_gnu_dialect_, enable_clang_dialect_,
                enable_builtin_type_extension_pack_);
            continue;
        }

        if (arg.rfind("-std=", 0) == 0) {
            const std::string mode_name = arg.substr(std::string("-std=").size());
            if (!parse_language_mode(mode_name, language_mode_)) {
                emit_error("argument to '-std=' is not supported: '" + mode_name +
                           "'");
                return;
            }
            apply_language_mode_defaults(language_mode_, enable_gnu_dialect_,
                                         enable_clang_dialect_,
                                         enable_builtin_type_extension_pack_);
            continue;
        }

        if (arg == "-fgnu-extensions") {
            enable_gnu_dialect_ = true;
            continue;
        }

        if (arg == "-fno-gnu-extensions") {
            enable_gnu_dialect_ = false;
            continue;
        }

        if (arg == "-fclang-extensions") {
            enable_clang_dialect_ = true;
            continue;
        }

        if (arg == "-fno-clang-extensions") {
            enable_clang_dialect_ = false;
            continue;
        }

        if (arg == "-fbuiltin-types") {
            enable_builtin_type_extension_pack_ = true;
            continue;
        }

        if (arg == "-fno-builtin-types") {
            enable_builtin_type_extension_pack_ = false;
            continue;
        }

        if (arg == "--enable-gnu-dialect") {
            enable_gnu_dialect_ = true;
            continue;
        }

        if (arg == "--disable-gnu-dialect") {
            enable_gnu_dialect_ = false;
            continue;
        }

        if (arg == "--enable-clang-dialect") {
            enable_clang_dialect_ = true;
            continue;
        }

        if (arg == "--disable-clang-dialect") {
            enable_clang_dialect_ = false;
            continue;
        }

        if (arg == "--enable-builtin-types") {
            enable_builtin_type_extension_pack_ = true;
            continue;
        }

        if (arg == "--disable-builtin-types") {
            enable_builtin_type_extension_pack_ = false;
            continue;
        }

        if (arg == "-o") {
            if (i + 1 >= expanded_arguments.size()) {
                emit_error("missing argument to '-o'");
                return;
            }

            output_file_ = expanded_arguments[++i];
            continue;
        }

        if (arg == "-nostdinc") {
            no_stdinc_ = true;
            continue;
        }

        if (arg == "-I") {
            if (i + 1 >= expanded_arguments.size()) {
                emit_error("missing argument to '-I'");
                return;
            }

            include_directories_.push_back(expanded_arguments[++i]);
            continue;
        }

        if (arg == "-iquote" ||
            (arg.size() > std::string("-iquote").size() &&
             arg.rfind("-iquote", 0) == 0)) {
            const std::string quote_directory =
                arg == "-iquote" ? (i + 1 < expanded_arguments.size()
                                        ? expanded_arguments[++i]
                                        : std::string())
                                 : arg.substr(std::string("-iquote").size());
            if (quote_directory.empty()) {
                emit_error("missing argument to '-iquote'");
                return;
            }
            quote_include_directories_.push_back(quote_directory);
            continue;
        }

        if (arg == "-isystem" ||
            (arg.size() > std::string("-isystem").size() &&
             arg.rfind("-isystem", 0) == 0)) {
            const std::string system_directory =
                arg == "-isystem" ? (i + 1 < expanded_arguments.size()
                                         ? expanded_arguments[++i]
                                         : std::string())
                                  : arg.substr(std::string("-isystem").size());
            if (system_directory.empty()) {
                emit_error("missing argument to '-isystem'");
                return;
            }
            system_include_directories_.push_back(system_directory);
            continue;
        }

        if (arg == "-idirafter" ||
            (arg.size() > std::string("-idirafter").size() &&
             arg.rfind("-idirafter", 0) == 0)) {
            const std::string after_directory =
                arg == "-idirafter" ? (i + 1 < expanded_arguments.size()
                                           ? expanded_arguments[++i]
                                           : std::string())
                                    : arg.substr(std::string("-idirafter").size());
            if (after_directory.empty()) {
                emit_error("missing argument to '-idirafter'");
                return;
            }
            after_system_include_directories_.push_back(after_directory);
            continue;
        }

        if (arg == "-isysroot" ||
            (arg.size() > std::string("-isysroot").size() &&
             arg.rfind("-isysroot", 0) == 0)) {
            const std::string sdk_root =
                arg == "-isysroot" ? (i + 1 < expanded_arguments.size()
                                          ? expanded_arguments[++i]
                                          : std::string())
                                   : arg.substr(std::string("-isysroot").size());
            if (sdk_root.empty()) {
                emit_error("missing argument to '-isysroot'");
                return;
            }
            isysroot_ = sdk_root;
            continue;
        }

        if (arg == "-include") {
            if (i + 1 >= expanded_arguments.size()) {
                emit_error("missing argument to '-include'");
                return;
            }
            forced_include_files_.push_back(expanded_arguments[++i]);
            continue;
        }

        if (arg == "-L" || (arg.size() > 2 && arg.rfind("-L", 0) == 0)) {
            const std::string search_directory =
                arg == "-L" ? (i + 1 < expanded_arguments.size()
                                   ? expanded_arguments[++i]
                                   : std::string())
                            : arg.substr(2);
            if (search_directory.empty()) {
                emit_error("missing argument to '-L'");
                return;
            }
            linker_search_directories_.push_back(search_directory);
            continue;
        }

        if (arg == "-l" || (arg.size() > 2 && arg.rfind("-l", 0) == 0)) {
            const std::string library_name =
                arg == "-l" ? (i + 1 < expanded_arguments.size()
                                   ? expanded_arguments[++i]
                                   : std::string())
                            : arg.substr(2);
            if (library_name.empty()) {
                emit_error("missing argument to '-l'");
                return;
            }
            linker_libraries_.push_back(library_name);
            continue;
        }

        if (arg.rfind("-Wl,", 0) == 0) {
            if (arg.size() <= std::string("-Wl,").size()) {
                emit_error("missing argument to '-Wl,'");
                return;
            }
            linker_passthrough_arguments_.push_back(arg);
            continue;
        }

        if (arg.size() > 2 && arg.rfind("-I", 0) == 0) {
            include_directories_.push_back(arg.substr(2));
            continue;
        }

        if (arg == "-D" || (arg.size() > 2 && arg.rfind("-D", 0) == 0)) {
            const std::string define_argument =
                arg == "-D" ? (i + 1 < expanded_arguments.size()
                                   ? expanded_arguments[++i]
                                   : std::string())
                            : arg.substr(2);
            if (define_argument.empty()) {
                emit_error("missing argument to '-D'");
                return;
            }

            const std::size_t equals_index = define_argument.find('=');
            const std::string macro_name = define_argument.substr(0, equals_index);
            if (!is_valid_identifier(macro_name)) {
                emit_error("invalid macro name for '-D': '" + macro_name + "'");
                return;
            }

            if (equals_index == std::string::npos) {
                command_line_macro_options_.emplace_back(
                    sysycc::CommandLineMacroActionKind::Define, macro_name);
            } else {
                command_line_macro_options_.emplace_back(
                    sysycc::CommandLineMacroActionKind::Define, macro_name,
                    define_argument.substr(equals_index + 1), true);
            }
            continue;
        }

        if (arg == "-U" || (arg.size() > 2 && arg.rfind("-U", 0) == 0)) {
            const std::string undef_argument =
                arg == "-U" ? (i + 1 < expanded_arguments.size()
                                   ? expanded_arguments[++i]
                                   : std::string())
                            : arg.substr(2);
            if (undef_argument.empty()) {
                emit_error("missing argument to '-U'");
                return;
            }
            if (!is_valid_identifier(undef_argument)) {
                emit_error("invalid macro name for '-U': '" + undef_argument +
                           "'");
                return;
            }

            command_line_macro_options_.emplace_back(
                sysycc::CommandLineMacroActionKind::Undefine, undef_argument);
            continue;
        }

        if (arg == "-Wall") {
            warning_policy_.enable_wall();
            continue;
        }

        if (arg == "-Wextra") {
            warning_policy_.enable_wextra();
            continue;
        }

        if (arg == "-Wshadow" || arg == "-Wundef" ||
            arg == "-Wformat=2" || arg == "-Wstrict-prototypes" ||
            arg == "-Wmissing-prototypes" || arg == "-Wcast-align" ||
            arg == "-Wpointer-arith" || arg == "-Wwrite-strings" ||
            arg == "-Wbad-function-cast" || arg == "-Waggregate-return" ||
            arg == "-Wswitch-enum" || arg == "-Wdouble-promotion" ||
            arg == "-Wfloat-equal" || arg == "-Wredundant-decls" ||
            arg == "-Wnested-externs" || arg == "-Wold-style-definition" ||
            arg == "-Wdeclaration-after-statement" ||
            arg == "-Wmissing-declarations" || arg == "-Wcast-qual" ||
            arg == "-Wvla" || arg == "-Wsystem-headers" ||
            arg == "-Wextra-semi" || arg == "-Wformat" ||
            arg == "-Wformat-security" || arg == "-Wstrict-overflow" ||
            arg == "-Wstrict-overflow=5" || arg == "-Wlogical-op" ||
            arg == "-Wduplicated-cond" || arg == "-Wduplicated-branches" ||
            arg == "-Walloca" || arg == "-Warray-bounds" ||
            arg == "-Wstrict-aliasing" || arg == "-Wstrict-aliasing=2" ||
            arg == "-Wold-style-declaration") {
            continue;
        }

        if (arg == "-Werror=format" || arg == "-Wno-error=format") {
            continue;
        }

        if (arg == "-Werror") {
            warning_policy_.set_all_warnings_as_errors(true);
            continue;
        }

        if (arg == "-Wno-error") {
            warning_policy_.set_all_warnings_as_errors(false);
            continue;
        }

        if (const std::optional<std::string> warning_name =
                parse_warning_option_name(arg, "-Werror=");
            warning_name.has_value()) {
            warning_policy_.set_warning_as_error(*warning_name);
            continue;
        }

        if (const std::optional<std::string> warning_name =
                parse_warning_option_name(arg, "-Wno-error=");
            warning_name.has_value()) {
            warning_policy_.set_warning_not_as_error(*warning_name);
            continue;
        }

        if (const std::optional<std::string> warning_name =
                parse_warning_option_name(arg, "-Wno-");
            warning_name.has_value()) {
            warning_policy_.disable_warning(*warning_name);
            continue;
        }

        if (const std::optional<std::string> warning_name =
                parse_warning_option_name(arg, "-W");
            warning_name.has_value()) {
            warning_policy_.enable_warning(*warning_name);
            continue;
        }

        if (arg.rfind("-Wno-", 0) == 0 ||
            arg == "-Wno-unused-command-line-argument") {
            continue;
        }

        if (!arg.empty() && arg[0] == '-') {
            emit_error("unrecognized command-line option '" + arg + "'");
            return;
        }

        positional_inputs_.push_back(arg);
    }

    if (positional_inputs_.empty()) {
        emit_error("missing input file");
        return;
    }

    if (!finalize_driver_mode()) {
        return;
    }
    if (!finalize_inputs()) {
        return;
    }
}
} // namespace ClI
