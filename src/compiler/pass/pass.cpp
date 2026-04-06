#include "pass.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"

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

bool should_stop_after_pass(const CompilerContext &context, PassKind pass_kind) {
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
                   : pass_kind == PassKind::CoreIrCanonicalize;
    case StopAfterStage::IR:
        return pass_kind == PassKind::LowerIr;
    case StopAfterStage::Asm:
        return pass_kind == PassKind::CodeGen;
    }

    return false;
}

bool should_run_pass(const CompilerContext &context, PassKind pass_kind) {
    switch (pass_kind) {
    case PassKind::CoreIrConstFold:
    case PassKind::CoreIrDce:
        return context.get_optimization_level() == OptimizationLevel::O1;
    case PassKind::Preprocess:
    case PassKind::Lex:
    case PassKind::Parse:
    case PassKind::Ast:
    case PassKind::Semantic:
    case PassKind::BuildCoreIr:
    case PassKind::CoreIrCanonicalize:
    case PassKind::LowerIr:
    case PassKind::CodeGen:
        return true;
    }

    return true;
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

} // namespace

void PassManager::AddPass(std::unique_ptr<Pass> pass) {
    if (pass == nullptr) {
        return;
    }

    if (get_pass_by_kind(pass->Kind()) != nullptr) {
        throw std::runtime_error(std::string("duplicate pass kind: ") +
                                 pass->Name());
    }

    passes_.push_back(std::move(pass));
}

Pass *PassManager::get_pass_by_kind(PassKind kind) const {
    for (const std::unique_ptr<Pass> &pass : passes_) {
        if (pass != nullptr && pass->Kind() == kind) {
            return pass.get();
        }
    }

    return nullptr;
}

PassResult PassManager::Run(CompilerContext &context) {
    for (const std::unique_ptr<Pass> &pass : passes_) {
        if (pass == nullptr) {
            return PassResult::Failure("encountered null pass");
        }

        if (!should_run_pass(context, pass->Kind())) {
            continue;
        }

        PassResult result = pass->Run(context);
        if (!result.ok) {
            return result;
        }
        if (context.get_diagnostic_engine().has_error()) {
            return PassResult::Failure(
                first_error_message(context.get_diagnostic_engine()));
        }
        if (should_stop_after_pass(context, pass->Kind())) {
            maybe_dump_core_ir_before_stop(context);
            return PassResult::Success();
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
