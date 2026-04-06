#include "backend/asm_gen/aarch64/aarch64_asm_gen_pass.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/aarch64_asm_backend.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_backend_pipeline.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "backend/asm_gen/object_result.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
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

std::filesystem::path get_asm_output_file_path(const CompilerContext &context) {
    const std::string &configured_output_file =
        context.get_backend_options().get_output_file();
    if (!configured_output_file.empty()) {
        return configured_output_file;
    }
    const std::filesystem::path input_path(context.get_input_file());
    return input_path.stem().string() + ".s";
}

std::filesystem::path get_object_output_file_path(const CompilerContext &context) {
    const std::string &configured_output_file =
        context.get_backend_options().get_output_file();
    if (!configured_output_file.empty()) {
        return configured_output_file;
    }
    const std::filesystem::path input_path(context.get_input_file());
    return input_path.stem().string() + ".o";
}

} // namespace

PassKind AArch64AsmGenPass::Kind() const { return PassKind::CodeGen; }

const char *AArch64AsmGenPass::Name() const { return "AArch64AsmGenPass"; }

PassResult AArch64AsmGenPass::Run(CompilerContext &context) {
    context.clear_asm_result();
    context.clear_object_result();
    context.set_asm_dump_file_path("");
    context.set_object_dump_file_path("");

    if ((!context.get_emit_asm() && !context.get_emit_object()) ||
        context.get_backend_options().get_backend_kind() !=
            BackendKind::AArch64Native) {
        return PassResult::Success();
    }

    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    AArch64AsmBackend backend;
    std::unique_ptr<AsmResult> asm_result =
        backend.Generate(*module, context.get_backend_options(),
                         context.get_diagnostic_engine());
    if (asm_result == nullptr) {
        return PassResult::Failure("failed to generate AArch64 assembly");
    }

    context.set_asm_result(std::move(asm_result));
    if (context.get_emit_asm()) {
        const std::filesystem::path output_file = get_asm_output_file_path(context);
        if (output_file.has_parent_path()) {
            std::filesystem::create_directories(output_file.parent_path());
        }
        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            return PassResult::Failure("failed to open asm output file");
        }
        ofs << context.get_asm_result()->get_text();
        context.set_asm_dump_file_path(output_file.string());
        return PassResult::Success();
    }

    const std::filesystem::path object_file = get_object_output_file_path(context);
    AArch64BackendPipeline backend_pipeline;
    std::unique_ptr<ObjectResult> object_result =
        backend_pipeline.emit_object_result(context.get_asm_result()->get_text(),
                                            context.get_backend_options(),
                                            object_file,
                                            context.get_diagnostic_engine());
    if (object_result == nullptr) {
        return PassResult::Failure("failed to assemble native AArch64 object output");
    }
    context.set_object_result(std::move(object_result));
    context.set_object_dump_file_path(object_file.string());
    return PassResult::Success();
}

} // namespace sysycc
