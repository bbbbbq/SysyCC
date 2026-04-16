#include "cli/cli.hpp"
#include "common/diagnostic/diagnostic_formatter.hpp"
#include "compiler/complier.hpp"
#include "compiler/complier_option.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

std::string backend_kind_name(sysycc::BackendKind backend_kind) {
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

std::string language_mode_name(sysycc::LanguageMode language_mode) {
    switch (language_mode) {
    case sysycc::LanguageMode::Sysy:
        return "sysy";
    case sysycc::LanguageMode::C99:
        return "c99";
    case sysycc::LanguageMode::Gnu99:
        return "gnu99";
    }
    return "unknown";
}

std::string optimization_level_name(
    sysycc::OptimizationLevel optimization_level) {
    switch (optimization_level) {
    case sysycc::OptimizationLevel::O0:
        return "O0";
    case sysycc::OptimizationLevel::O1:
        return "O1";
    }
    return "unknown";
}

std::string driver_action_name(sysycc::DriverAction driver_action) {
    switch (driver_action) {
    case sysycc::DriverAction::InternalPipeline:
        return "internal-pipeline";
    case sysycc::DriverAction::FullCompile:
        return "full-compile";
    case sysycc::DriverAction::CompileOnly:
        return "compile-only";
    case sysycc::DriverAction::PreprocessOnly:
        return "preprocess-only";
    case sysycc::DriverAction::SyntaxOnly:
        return "syntax-only";
    case sysycc::DriverAction::EmitAssembly:
        return "emit-assembly";
    case sysycc::DriverAction::EmitLlvmIr:
        return "emit-llvm-ir";
    }
    return "unknown";
}

void print_verbose_configuration(const ClI::Cli &cli,
                                 const sysycc::ComplierOption &option) {
    std::cerr << cli.get_program_name() << " version " << cli.get_version()
              << '\n';
    std::cerr << "driver action: "
              << driver_action_name(option.get_driver_action()) << '\n';
    std::cerr << "language mode: "
              << language_mode_name(option.get_language_mode()) << '\n';
    std::cerr << "optimization level: "
              << optimization_level_name(option.get_optimization_level())
              << '\n';
    std::cerr << "gnu extensions: "
              << (option.get_enable_gnu_dialect() ? "enabled" : "disabled")
              << '\n';
    std::cerr << "clang extensions: "
              << (option.get_enable_clang_dialect() ? "enabled" : "disabled")
              << '\n';
    std::cerr << "builtin types: "
              << (option.get_enable_builtin_type_extension_pack() ? "enabled"
                                                                  : "disabled")
              << '\n';
    std::cerr << "backend: "
              << backend_kind_name(
                     option.get_backend_options().get_backend_kind())
              << '\n';
    if (!option.get_backend_options().get_target_triple().empty()) {
        std::cerr << "target: "
                  << option.get_backend_options().get_target_triple() << '\n';
    }
    std::cerr << "include search paths:\n";
    for (const std::string &include_directory :
         option.get_include_directories()) {
        std::cerr << "  " << include_directory << '\n';
    }
    std::cerr << "system include search paths:\n";
    for (const std::string &system_include_directory :
         option.get_system_include_directories()) {
        std::cerr << "  " << system_include_directory << '\n';
    }
}

bool read_file_text(const std::filesystem::path &file_path, std::string &text) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        return false;
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    text = oss.str();
    return true;
}

bool emit_primary_text_output(const std::string &program_name,
                              const std::string &output_file,
                              const std::string &text) {
    if (output_file.empty()) {
        std::cout << text;
        return true;
    }

    const std::filesystem::path output_path(output_file);
    if (output_path.has_parent_path()) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    std::ofstream ofs(output_path);
    if (!ofs.is_open()) {
        std::cerr << program_name << ": error: failed to open output file '"
                  << output_file << "'\n";
        return false;
    }

    ofs << text;
    if (!ofs.good()) {
        std::cerr << program_name << ": error: failed to write output file '"
                  << output_file << "'\n";
        return false;
    }
    return true;
}

std::string default_output_file_for_action(
    const sysycc::ComplierOption &option) {
    const std::filesystem::path input_path(option.get_input_file());
    switch (option.get_driver_action()) {
    case sysycc::DriverAction::EmitLlvmIr:
        return input_path.stem().string() + ".ll";
    case sysycc::DriverAction::EmitAssembly:
        return input_path.stem().string() + ".s";
    case sysycc::DriverAction::CompileOnly:
        return input_path.stem().string() + ".o";
    default:
        return option.get_output_file();
    }
}

bool emit_driver_primary_output(const ClI::Cli &cli,
                                const sysycc::ComplierOption &option,
                                const sysycc::CompilerContext &context) {
    if (option.get_driver_action() == sysycc::DriverAction::PreprocessOnly) {
        std::string preprocessed_text;
        if (!read_file_text(context.get_preprocessed_file_path(),
                            preprocessed_text)) {
            std::cerr << cli.get_program_name()
                      << ": error: failed to read preprocessed output\n";
            return false;
        }
        return emit_primary_text_output(cli.get_program_name(),
                                        option.get_output_file(),
                                        preprocessed_text);
    }

    if (option.get_driver_action() == sysycc::DriverAction::EmitLlvmIr) {
        if (context.get_ir_result() == nullptr) {
            std::cerr << cli.get_program_name()
                      << ": error: missing LLVM IR output\n";
            return false;
        }
        const std::string output_file =
            option.get_output_file().empty()
                ? default_output_file_for_action(option)
                : option.get_output_file();
        return emit_primary_text_output(cli.get_program_name(), output_file,
                                        context.get_ir_result()->get_text());
    }

    if (option.get_driver_action() == sysycc::DriverAction::CompileOnly) {
        return context.get_object_result() != nullptr;
    }

    return true;
}

} // namespace

int main(int argc, char *argv[]) {
    ClI::Cli cli;
    cli.Run(argc, argv);

    if (cli.get_is_help() || cli.get_is_version()) {
        return 0;
    }

    if (cli.get_has_error() || !cli.has_input_file()) {
        return 1;
    }

    sysycc::ComplierOption option;
    cli.set_compiler_option(option);
    if (option.get_verbose()) {
        print_verbose_configuration(cli, option);
    }
    sysycc::Complier complier(option);

    sysycc::PassResult result = complier.Run();
    const sysycc::DiagnosticEngine &diagnostic_engine =
        complier.get_context().get_diagnostic_engine();
    if (!result.ok) {
        if (!diagnostic_engine.get_diagnostics().empty()) {
            sysycc::DiagnosticFormatter::print_diagnostics(std::cerr,
                                                           diagnostic_engine);
        } else {
            std::cerr << result.message << '\n';
        }
        return 1;
    }

    if (!diagnostic_engine.get_diagnostics().empty()) {
        sysycc::DiagnosticFormatter::print_diagnostics(std::cerr,
                                                       diagnostic_engine);
    }

    if (diagnostic_engine.has_error()) {
        return 1;
    }

    if (!emit_driver_primary_output(cli, option, complier.get_context())) {
        return 1;
    }

    if (!result.message.empty()) {
        std::cout << result.message << '\n';
    }
    return 0;
}
