#include "backend/asm_gen/aarch64/aarch64_asm_gen_pass.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_backend_pipeline.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_llvm_ir_artifacts(CompilerContext &context,
                                          const char *pass_name) {
    const std::string message =
        std::string(pass_name) +
        " requires lowered LLVM IR artifacts before native AArch64 codegen";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

DiagnosticStage
map_api_diagnostic_stage(const AArch64CodegenDiagnostic &diagnostic) {
    (void)diagnostic;
    return DiagnosticStage::Compiler;
}

void append_api_diagnostics(
    CompilerContext &context,
    const std::vector<AArch64CodegenDiagnostic> &diagnostics) {
    DiagnosticEngine &diagnostic_engine = context.get_diagnostic_engine();
    for (const AArch64CodegenDiagnostic &diagnostic : diagnostics) {
        switch (diagnostic.severity) {
        case AArch64CodegenDiagnosticSeverity::Warning:
            diagnostic_engine.add_warning(map_api_diagnostic_stage(diagnostic),
                                          diagnostic.message);
            break;
        case AArch64CodegenDiagnosticSeverity::Note:
            diagnostic_engine.add_note(map_api_diagnostic_stage(diagnostic),
                                       diagnostic.message);
            break;
        case AArch64CodegenDiagnosticSeverity::Error:
        default:
            diagnostic_engine.add_error(map_api_diagnostic_stage(diagnostic),
                                        diagnostic.message);
            break;
        }
    }
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

std::filesystem::path
get_object_output_file_path(const CompilerContext &context) {
    const std::string &configured_output_file =
        context.get_backend_options().get_output_file();
    if (!configured_output_file.empty()) {
        return configured_output_file;
    }
    const std::filesystem::path input_path(context.get_input_file());
    return input_path.stem().string() + ".o";
}

PassResult write_object_output_file(const std::filesystem::path &output_file,
                                    const ObjectResult &object_result) {
    if (output_file.has_parent_path()) {
        std::filesystem::create_directories(output_file.parent_path());
    }
    std::ofstream ofs(output_file, std::ios::binary);
    if (!ofs.is_open()) {
        return PassResult::Failure("failed to open object output file");
    }
    const std::vector<std::uint8_t> &bytes = object_result.get_bytes();
    ofs.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!ofs.good()) {
        return PassResult::Failure("failed to write object output file");
    }
    return PassResult::Success();
}

AArch64CodegenFileRequest
build_codegen_request(const CompilerContext &context,
                      const std::string &input_file_path) {
    AArch64CodegenFileRequest request;
    request.input_file_path = input_file_path;
    request.options.target_triple =
        context.get_backend_options().get_target_triple();
    request.options.position_independent =
        context.get_backend_options().get_position_independent();
    request.options.debug_info = context.get_backend_options().get_debug_info();
    return request;
}

struct NativeAArch64ModuleArtifacts {
    AArch64AsmModule asm_module;
    AArch64MachineModule machine_module;
    AArch64ObjectModule object_module;
};

std::unique_ptr<NativeAArch64ModuleArtifacts>
try_build_native_aarch64_from_core_ir(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return nullptr;
    }

    auto artifacts = std::make_unique<NativeAArch64ModuleArtifacts>();
    AArch64BackendPipeline pipeline;
    if (!pipeline.build_and_finalize_module(
            *module, context.get_backend_options(),
            context.get_diagnostic_engine(), artifacts->asm_module,
            artifacts->machine_module, artifacts->object_module)) {
        return nullptr;
    }
    return artifacts;
}

PassResult emit_native_aarch64_asm_from_artifacts(
    CompilerContext &context, const NativeAArch64ModuleArtifacts &artifacts) {
    AArch64BackendPipeline pipeline;
    std::unique_ptr<AsmResult> asm_result =
        pipeline.emit_asm_result(artifacts.asm_module, artifacts.machine_module,
                                 artifacts.object_module);
    if (asm_result == nullptr) {
        return PassResult::Failure("failed to emit native AArch64 assembly");
    }

    context.set_asm_result(std::move(asm_result));
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

PassResult emit_native_aarch64_object_from_artifacts(
    CompilerContext &context, const NativeAArch64ModuleArtifacts &artifacts) {
    const std::filesystem::path output_file =
        get_object_output_file_path(context);
    AArch64BackendPipeline pipeline;
    std::unique_ptr<ObjectResult> object_result = pipeline.emit_object_result(
        artifacts.machine_module, artifacts.object_module,
        context.get_backend_options(), output_file,
        context.get_diagnostic_engine());
    if (object_result == nullptr ||
        context.get_diagnostic_engine().has_error()) {
        return PassResult::Failure(
            "failed to emit native AArch64 object output");
    }

    context.set_object_result(std::move(object_result));
    PassResult write_result =
        write_object_output_file(output_file, *context.get_object_result());
    if (!write_result.ok) {
        return write_result;
    }
    context.set_object_dump_file_path(output_file.string());
    return PassResult::Success();
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
    if (context.get_stop_after_stage() == StopAfterStage::CoreIr ||
        context.get_stop_after_stage() == StopAfterStage::IR) {
        return PassResult::Success();
    }

    if (context.get_backend_options().get_debug_info()) {
        std::unique_ptr<NativeAArch64ModuleArtifacts> artifacts =
            try_build_native_aarch64_from_core_ir(context);
        if (artifacts != nullptr) {
            return context.get_emit_asm()
                       ? emit_native_aarch64_asm_from_artifacts(context,
                                                                *artifacts)
                       : emit_native_aarch64_object_from_artifacts(context,
                                                                   *artifacts);
        }
        if (context.get_diagnostic_engine().has_error()) {
            return PassResult::Failure("failed to generate native AArch64 "
                                       "output from Core IR artifacts");
        }
    }

    const std::string &bc_artifact =
        context.get_llvm_ir_bitcode_artifact_file_path();
    const std::string &ll_artifact =
        context.get_llvm_ir_text_artifact_file_path();
    if (bc_artifact.empty() && ll_artifact.empty()) {
        return fail_missing_llvm_ir_artifacts(context, Name());
    }

    const bool prefer_bitcode = !bc_artifact.empty();
    const std::string &artifact_path =
        prefer_bitcode ? bc_artifact : ll_artifact;
    const AArch64CodegenFileRequest request =
        build_codegen_request(context, artifact_path);

    if (context.get_emit_asm()) {
        AArch64AsmCompileResult asm_result =
            prefer_bitcode ? compile_bc_file_to_asm(request)
                           : compile_ll_file_to_asm(request);
        append_api_diagnostics(context, asm_result.diagnostics);
        if (asm_result.status != AArch64CodegenStatus::Success) {
            return PassResult::Failure("failed to generate native AArch64 "
                                       "assembly from LLVM IR artifacts");
        }

        context.set_asm_result(std::make_unique<AsmResult>(
            AsmTargetKind::AArch64, asm_result.asm_text));
        const std::filesystem::path output_file =
            get_asm_output_file_path(context);
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

    AArch64ObjectCompileResult object_result =
        prefer_bitcode ? compile_bc_file_to_object(request)
                       : compile_ll_file_to_object(request);
    append_api_diagnostics(context, object_result.diagnostics);
    if (object_result.status != AArch64CodegenStatus::Success) {
        return PassResult::Failure("failed to generate native AArch64 object "
                                   "output from LLVM IR artifacts");
    }

    context.set_object_result(std::make_unique<ObjectResult>(
        ObjectTargetKind::ElfAArch64, object_result.object_bytes));
    const std::filesystem::path output_file =
        get_object_output_file_path(context);
    PassResult write_result =
        write_object_output_file(output_file, *context.get_object_result());
    if (!write_result.ok) {
        return write_result;
    }
    context.set_object_dump_file_path(output_file.string());
    return PassResult::Success();
}

} // namespace sysycc
