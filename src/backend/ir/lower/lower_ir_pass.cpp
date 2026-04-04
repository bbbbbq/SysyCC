#include "backend/ir/lower/lower_ir_pass.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/ir_kind.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "backend/ir/lower/lowering/core_ir_target_backend.hpp"
#include "backend/ir/lower/lowering/core_ir_target_backend_factory.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
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

    context.set_ir_result(std::move(ir_result));
    if (context.get_dump_ir() && context.get_ir_result() != nullptr) {
        const std::filesystem::path output_dir("build/intermediate_results");
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
