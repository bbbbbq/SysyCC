#include "backend/ir/lower/lower_ir_pass.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <cstdlib>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/shared/ir_kind.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "backend/ir/lower/lowering/core_ir_target_backend.hpp"
#include "backend/ir/lower/lowering/core_ir_target_backend_factory.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"
#include "common/intermediate_results_path.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

PassResult maybe_dump_core_ir(CompilerContext &context, const CoreIrModule &module) {
    context.set_core_ir_dump_file_path("");
    if (!context.get_dump_core_ir()) {
        return PassResult::Success();
    }

    const std::filesystem::path output_dir("build/intermediate_results");
    std::filesystem::create_directories(output_dir);
    const std::filesystem::path input_path(context.get_input_file());
    const std::filesystem::path output_file =
        output_dir / (input_path.stem().string() + ".core-ir.txt");
    std::ofstream ofs(output_file);
    if (!ofs.is_open()) {
        return PassResult::Failure("failed to open core ir dump file");
    }

    CoreIrRawPrinter printer;
    ofs << printer.print_module(module);
    context.set_core_ir_dump_file_path(output_file.string());
    return PassResult::Success();
}

std::string shell_quote(const std::string &text) {
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

std::optional<std::filesystem::path> find_executable_in_path(const std::string &name) {
    const char *path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return std::nullopt;
    }
    std::stringstream stream(path_env);
    std::string path_entry;
    while (std::getline(stream, path_entry, ':')) {
        if (path_entry.empty()) {
            continue;
        }
        const std::filesystem::path candidate =
            std::filesystem::path(path_entry) / name;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

bool read_text_file(const std::filesystem::path &file_path, std::string &text) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return true;
}

bool maybe_reoptimize_llvm_ir(CompilerContext &context, IRResult &ir_result) {
    const char *reopt_env = std::getenv("SYSYCC_LLVM_IR_REOPT_O3");
    if (reopt_env == nullptr || std::string_view(reopt_env) != "1") {
        return true;
    }
    std::optional<std::filesystem::path> clang =
        find_executable_in_path("clang");
    if (!clang.has_value()) {
        context.get_diagnostic_engine().add_warning(
            DiagnosticStage::Compiler,
            "skipping llvm ir reopt: clang not found in PATH");
        return true;
    }

    const std::filesystem::path output_dir = sysycc::get_intermediate_results_dir();
    std::filesystem::create_directories(output_dir);
    const std::filesystem::path input_path(context.get_input_file());
    const std::string stem =
        input_path.stem().empty() ? std::string("sysycc") : input_path.stem().string();
    const std::string nonce =
        std::to_string(static_cast<unsigned long long>(std::rand()));
    const std::filesystem::path temp_input =
        output_dir / ("." + stem + ".reopt." + nonce + ".input.ll");
    const std::filesystem::path temp_output =
        output_dir / ("." + stem + ".reopt." + nonce + ".output.ll");

    {
        std::ofstream ofs(temp_input);
        if (!ofs.is_open()) {
            context.get_diagnostic_engine().add_warning(
                DiagnosticStage::Compiler,
                "skipping llvm ir reopt: failed to create temporary input file");
            return true;
        }
        ofs << ir_result.get_text();
    }

    const std::string command =
        shell_quote(clang->string()) +
        " -O3 -S -emit-llvm -x ir " + shell_quote(temp_input.string()) +
        " -o " + shell_quote(temp_output.string());
    const int exit_code = std::system(command.c_str());
    if (exit_code != 0) {
        context.get_diagnostic_engine().add_warning(
            DiagnosticStage::Compiler,
            "skipping llvm ir reopt: clang -O3 failed on temporary llvm ir");
        std::filesystem::remove(temp_input);
        std::filesystem::remove(temp_output);
        return true;
    }

    std::string optimized_text;
    if (!read_text_file(temp_output, optimized_text) || optimized_text.empty()) {
        context.get_diagnostic_engine().add_warning(
            DiagnosticStage::Compiler,
            "skipping llvm ir reopt: failed to read optimized llvm ir");
        std::filesystem::remove(temp_input);
        std::filesystem::remove(temp_output);
        return true;
    }

    ir_result = IRResult(ir_result.get_kind(), std::move(optimized_text));
    std::filesystem::remove(temp_input);
    std::filesystem::remove(temp_output);
    return true;
}

} // namespace

PassKind LowerIrPass::Kind() const { return PassKind::LowerIr; }

const char *LowerIrPass::Name() const { return "LowerIrPass"; }

PassResult LowerIrPass::Run(CompilerContext &context) {
    if (context.get_backend_options().get_backend_kind() !=
        BackendKind::LlvmIr) {
        context.clear_ir_result();
        context.set_ir_dump_file_path("");
        return PassResult::Success();
    }

    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    PassResult core_ir_dump_result = maybe_dump_core_ir(context, *module);
    if (!core_ir_dump_result.ok) {
        return core_ir_dump_result;
    }

    context.clear_ir_result();
    context.set_ir_dump_file_path("");

    std::unique_ptr<CoreIrTargetBackend> target_backend =
        create_core_ir_target_backend(IrKind::LLVM);
    if (target_backend == nullptr) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            "failed to create core ir target backend");
        return PassResult::Failure("failed to create core ir target backend");
    }

    std::unique_ptr<IRResult> ir_result =
        target_backend->Lower(*module,
                              context.get_diagnostic_engine());
    if (ir_result == nullptr) {
        return PassResult::Failure("failed to lower ir result");
    }
    if (!maybe_reoptimize_llvm_ir(context, *ir_result)) {
        return PassResult::Failure("failed to reoptimize llvm ir result");
    }

    context.set_ir_result(std::move(ir_result));
    if (context.get_dump_ir() && context.get_ir_result() != nullptr) {
        const std::filesystem::path output_dir =
            sysycc::get_intermediate_results_dir();
        std::filesystem::create_directories(output_dir);

        const std::filesystem::path input_path(context.get_input_file());
        std::string extension = ".ir";
        switch (context.get_ir_result()->get_kind()) {
        case IrKind::LLVM:
            extension = ".ll";
            break;
        case IrKind::None:
        case IrKind::AArch64:
            break;
        }
        const std::filesystem::path output_file =
            output_dir / (input_path.stem().string() + extension);
        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            return PassResult::Failure("failed to open ir dump file");
        }
        ofs << context.get_ir_result()->get_text();
        context.set_ir_dump_file_path(output_file.string());
    }

    return PassResult::Success();
}

} // namespace sysycc
