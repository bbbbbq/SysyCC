#include "backend/ir/lower/lower_ir_pass.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/shared/ir_kind.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "backend/ir/lower/lowering/core_ir_target_backend.hpp"
#include "backend/ir/lower/lowering/core_ir_target_backend_factory.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_bitcode_loader.hpp"
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

PassResult materialize_llvm_ir_artifacts(CompilerContext &context) {
    context.set_llvm_ir_text_artifact_file_path("");
    context.set_llvm_ir_bitcode_artifact_file_path("");
    if (context.get_ir_result() == nullptr ||
        context.get_ir_result()->get_kind() != IrKind::LLVM) {
        return PassResult::Success();
    }

    const bool should_materialize_artifacts =
        context.get_backend_options().get_backend_kind() ==
            BackendKind::AArch64Native ||
        context.get_backend_options().get_backend_kind() ==
            BackendKind::Riscv64Native ||
        context.get_dump_ir() ||
        context.get_stop_after_stage() == StopAfterStage::IR;
    if (!should_materialize_artifacts) {
        return PassResult::Success();
    }

    const std::filesystem::path output_dir =
        sysycc::get_intermediate_results_dir();
    std::filesystem::create_directories(output_dir);

    const std::filesystem::path input_path(context.get_input_file());
    const std::filesystem::path ll_file =
        output_dir / (input_path.stem().string() + ".ll");
    const std::filesystem::path bc_file =
        output_dir / (input_path.stem().string() + ".bc");

    {
        std::ofstream ofs(ll_file);
        if (!ofs.is_open()) {
            return PassResult::Failure(
                "failed to open llvm ir text artifact file");
        }
        ofs << context.get_ir_result()->get_text();
        if (!ofs.good()) {
            return PassResult::Failure(
                "failed to write llvm ir text artifact file");
        }
    }
    context.set_llvm_ir_text_artifact_file_path(ll_file.string());

    const AArch64BitcodeWriteResult bitcode_result =
        write_llvm_ir_text_to_bitcode_file(
            ll_file.string(), context.get_ir_result()->get_text(),
            bc_file.string());
    if (!bitcode_result.ok) {
        for (const AArch64CodegenDiagnostic &diagnostic :
             bitcode_result.diagnostics) {
            context.get_diagnostic_engine().add_error(
                DiagnosticStage::Compiler, diagnostic.message);
        }
        return PassResult::Failure(
            "failed to materialize llvm bitcode artifact file");
    }
    context.set_llvm_ir_bitcode_artifact_file_path(bc_file.string());

    if (context.get_dump_ir()) {
        context.set_ir_dump_file_path(ll_file.string());
    }

    return PassResult::Success();
}

} // namespace

PassKind LowerIrPass::Kind() const { return PassKind::LowerIr; }

const char *LowerIrPass::Name() const { return "LowerIrPass"; }

PassResult LowerIrPass::Run(CompilerContext &context) {
    context.clear_ir_result();
    context.set_ir_dump_file_path("");
    context.set_llvm_ir_text_artifact_file_path("");
    context.set_llvm_ir_bitcode_artifact_file_path("");

    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    PassResult core_ir_dump_result = maybe_dump_core_ir(context, *module);
    if (!core_ir_dump_result.ok) {
        return core_ir_dump_result;
    }

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

    context.set_ir_result(std::move(ir_result));
    PassResult artifact_result = materialize_llvm_ir_artifacts(context);
    if (!artifact_result.ok) {
        return artifact_result;
    }

    return PassResult::Success();
}

} // namespace sysycc
