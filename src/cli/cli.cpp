#include "cli.hpp"

#include <cctype>
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

bool parse_language_mode(const std::string &mode_name,
                         sysycc::LanguageMode &language_mode) {
    if (mode_name == "c99") {
        language_mode = sysycc::LanguageMode::C99;
        return true;
    }
    if (mode_name == "gnu99") {
        language_mode = sysycc::LanguageMode::Gnu99;
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
        enable_gnu_dialect = false;
        enable_clang_dialect = false;
        enable_builtin_type_extension_pack = false;
        return;
    case sysycc::LanguageMode::Gnu99:
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

    if (!explicit_backend_) {
        backend_kind_ = (driver_action_ == sysycc::DriverAction::EmitAssembly ||
                         driver_action_ == sysycc::DriverAction::CompileOnly)
                            ? sysycc::BackendKind::AArch64Native
                            : sysycc::BackendKind::LlvmIr;
    }

    if ((driver_action_ == sysycc::DriverAction::EmitAssembly ||
         driver_action_ == sysycc::DriverAction::CompileOnly) &&
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
            emit_error("multiple source inputs with -c are not supported yet; compile sources separately");
            return false;
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
    system_include_directories_.clear();
    command_line_macro_options_.clear();
    forced_include_files_.clear();
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

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

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
                arg == "-x" ? (i + 1 < argc ? argv[++i] : std::string())
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

        if (arg == "-pipe" || arg == "-ffunction-sections" ||
            arg == "-fdata-sections" || arg == "-fno-common" ||
            arg == "-Winvalid-pch" || arg == "-fvisibility=hidden") {
            continue;
        }

        if (arg == "-arch") {
            const std::string arch_name =
                i + 1 < argc ? std::string(argv[++i]) : std::string();
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
            if (visibility_mode != "hidden") {
                emit_error("argument to '-fvisibility=' is not supported: '" +
                           visibility_mode + "'");
                return;
            }
            continue;
        }

        if (arg == "-fPIC") {
            request_position_independent_ = true;
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
                arg == "-MF" ? (i + 1 < argc ? argv[++i] : std::string())
                             : arg.substr(3);
            if (depfile_output_file_.empty()) {
                emit_error("missing argument to '-MF'");
                return;
            }
            continue;
        }

        if (arg == "-MT" || (arg.size() > 3 && arg.rfind("-MT", 0) == 0)) {
            const std::string dep_target =
                arg == "-MT" ? (i + 1 < argc ? argv[++i] : std::string())
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
                arg == "-MQ" ? (i + 1 < argc ? argv[++i] : std::string())
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
            if (i + 1 >= argc) {
                emit_error("missing argument to '" + arg + "'");
                return;
            }

            const std::string backend_name = argv[++i];
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
            if (i + 1 >= argc) {
                emit_error("missing argument to '" + arg + "'");
                return;
            }

            internal_pipeline_requested_ = true;
            target_triple_ = argv[++i];
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
            if (i + 1 >= argc) {
                emit_error("missing argument to '" + arg + "'");
                return;
            }

            const std::string stage_name = argv[++i];
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
            if (i + 1 >= argc) {
                emit_error("missing argument to '-o'");
                return;
            }

            output_file_ = argv[++i];
            continue;
        }

        if (arg == "-nostdinc") {
            no_stdinc_ = true;
            continue;
        }

        if (arg == "-I") {
            if (i + 1 >= argc) {
                emit_error("missing argument to '-I'");
                return;
            }

            include_directories_.push_back(argv[++i]);
            continue;
        }

        if (arg == "-isystem") {
            if (i + 1 >= argc) {
                emit_error("missing argument to '-isystem'");
                return;
            }

            system_include_directories_.push_back(argv[++i]);
            continue;
        }

        if (arg == "-include") {
            if (i + 1 >= argc) {
                emit_error("missing argument to '-include'");
                return;
            }
            forced_include_files_.push_back(argv[++i]);
            continue;
        }

        if (arg == "-L" || (arg.size() > 2 && arg.rfind("-L", 0) == 0)) {
            const std::string search_directory =
                arg == "-L" ? (i + 1 < argc ? argv[++i] : std::string())
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
                arg == "-l" ? (i + 1 < argc ? argv[++i] : std::string())
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
                arg == "-D" ? (i + 1 < argc ? argv[++i] : std::string())
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
                arg == "-U" ? (i + 1 < argc ? argv[++i] : std::string())
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
