#include "backend/asm_gen/aarch64/support/aarch64_codegen_backend_bridge.hpp"

#include <cctype>
#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <unistd.h>

#include "backend/asm_gen/aarch64/api/aarch64_restricted_llvm_ir_parser.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_import_model_core_ir_bridge.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/asm_gen/aarch64/passes/aarch64_backend_pipeline.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "backend/asm_gen/backend_options.hpp"
#include "common/diagnostic/diagnostic.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

std::string trim_copy(std::string text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.erase(text.begin());
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

bool is_benign_module_asm_line(const std::string &line) {
    const std::string trimmed = trim_copy(line);
    return trimmed.empty() || trimmed == ".text" || trimmed == ".data" ||
           trimmed == ".bss" || trimmed == ".rodata" ||
           trimmed.rfind(".section ", 0) == 0 ||
           trimmed.rfind(".p2align ", 0) == 0;
}

AArch64CodegenDiagnostic make_error_diagnostic(std::string stage_name,
                                               std::string message,
                                               std::string file_path = {},
                                               int line = 0,
                                               int column = 0) {
    AArch64CodegenDiagnostic diagnostic;
    diagnostic.severity = AArch64CodegenDiagnosticSeverity::Error;
    diagnostic.stage_name = std::move(stage_name);
    diagnostic.message = std::move(message);
    diagnostic.file_path = std::move(file_path);
    diagnostic.line = line;
    diagnostic.column = column;
    return diagnostic;
}

AArch64CodegenDiagnostic make_note_diagnostic(std::string stage_name,
                                              std::string message) {
    AArch64CodegenDiagnostic diagnostic;
    diagnostic.severity = AArch64CodegenDiagnosticSeverity::Note;
    diagnostic.stage_name = std::move(stage_name);
    diagnostic.message = std::move(message);
    return diagnostic;
}

AArch64CodegenDiagnosticSeverity map_diagnostic_severity(
    DiagnosticLevel level) {
    switch (level) {
    case DiagnosticLevel::Warning:
        return AArch64CodegenDiagnosticSeverity::Warning;
    case DiagnosticLevel::Note:
        return AArch64CodegenDiagnosticSeverity::Note;
    case DiagnosticLevel::Error:
    default:
        return AArch64CodegenDiagnosticSeverity::Error;
    }
}

void append_backend_diagnostics(
    const DiagnosticEngine &diagnostics,
    std::vector<AArch64CodegenDiagnostic> &output) {
    for (const Diagnostic &diagnostic : diagnostics.get_diagnostics()) {
        AArch64CodegenDiagnostic converted;
        converted.severity = map_diagnostic_severity(diagnostic.get_level());
        converted.stage_name = get_diagnostic_stage_name(diagnostic.get_stage());
        converted.message = diagnostic.get_message();
        if (const SourceFile *file = diagnostic.get_source_span().get_file();
            file != nullptr) {
            converted.file_path = file->get_path();
        }
        converted.line = diagnostic.get_source_span().get_line_begin();
        converted.column = diagnostic.get_source_span().get_col_begin();
        output.push_back(std::move(converted));
    }
}

bool has_error_diagnostics(
    const std::vector<AArch64CodegenDiagnostic> &diagnostics) {
    for (const AArch64CodegenDiagnostic &diagnostic : diagnostics) {
        if (diagnostic.severity == AArch64CodegenDiagnosticSeverity::Error) {
            return true;
        }
    }
    return false;
}

std::size_t count_machine_blocks(const AArch64MachineModule &machine_module) {
    std::size_t blocks = 0;
    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        blocks += function.get_blocks().size();
    }
    return blocks;
}

std::size_t count_machine_instructions(const AArch64MachineModule &machine_module) {
    std::size_t instructions = 0;
    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        for (const AArch64MachineBlock &block : function.get_blocks()) {
            instructions += block.get_instructions().size();
        }
    }
    return instructions;
}

std::size_t count_object_relocations(const AArch64ObjectModule &object_module) {
    std::size_t relocations = 0;
    for (const AArch64DataObject &data_object : object_module.get_data_objects()) {
        for (const AArch64DataFragment &fragment : data_object.get_fragments()) {
            relocations += fragment.get_relocations().size();
        }
    }
    return relocations;
}

std::string summarize_object_pipeline_state(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const BackendOptions &backend_options,
    const std::filesystem::path &object_path = {}) {
    std::string summary = "target='" + backend_options.get_target_triple() + "'";
    summary += ", pic=";
    summary += backend_options.get_position_independent() ? "on" : "off";
    summary += ", debug=";
    summary += backend_options.get_debug_info() ? "on" : "off";
    summary += ", functions=" + std::to_string(machine_module.get_functions().size());
    summary += ", blocks=" + std::to_string(count_machine_blocks(machine_module));
    summary +=
        ", instructions=" + std::to_string(count_machine_instructions(machine_module));
    summary +=
        ", data_objects=" + std::to_string(object_module.get_data_objects().size());
    summary +=
        ", data_relocations=" + std::to_string(count_object_relocations(object_module));
    summary += ", symbols=" + std::to_string(object_module.get_symbols().size());
    if (!object_path.empty()) {
        summary += ", temp_object='" + object_path.string() + "'";
    }
    return summary;
}

BackendOptions make_backend_options(const AArch64CodegenFileRequest &request,
                                    const std::string &imported_triple) {
    BackendOptions backend_options;
    backend_options.set_backend_kind(BackendKind::AArch64Native);
    backend_options.set_target_triple(
        !request.options.target_triple.empty()
            ? request.options.target_triple
            : (!imported_triple.empty() ? imported_triple
                                        : "aarch64-unknown-linux-gnu"));
    backend_options.set_position_independent(
        request.options.position_independent);
    backend_options.set_debug_info(request.options.debug_info);
    return backend_options;
}

std::filesystem::path create_temp_object_path() {
    std::filesystem::path temp_directory;
    std::error_code error_code;
    temp_directory = std::filesystem::temp_directory_path(error_code);
    if (error_code) {
        return {};
    }

    std::string pattern =
        (temp_directory / "sysycc_aarch64_codegen_XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const int fd = mkstemp(buffer.data());
    if (fd < 0) {
        return {};
    }
    close(fd);
    return std::filesystem::path(buffer.data());
}

AArch64AsmCompileResult emit_module_to_asm(
    const AArch64CoreIrImportedModule &imported,
    const AArch64CodegenFileRequest &request) {
    DiagnosticEngine diagnostics;
    AArch64AsmCompileResult result;
    AArch64AsmModule asm_module;
    AArch64MachineModule machine_module;
    AArch64ObjectModule object_module;
    AArch64BackendPipeline pipeline;
    if (!pipeline.build_and_finalize_module(
            *imported.module,
            make_backend_options(request, imported.source_target_triple),
            diagnostics, asm_module, machine_module, object_module)) {
        append_backend_diagnostics(diagnostics, result.diagnostics);
        result.status = AArch64CodegenStatus::Failure;
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(make_error_diagnostic(
                "aarch64-backend",
                "failed to compile the restricted LLVM IR module to AArch64 assembly"));
        }
        return result;
    }
    for (const std::string &module_asm_line : imported.module_asm_lines) {
        asm_module.append_module_asm_line(module_asm_line);
    }
    std::unique_ptr<AsmResult> asm_result =
        pipeline.emit_asm_result(asm_module, machine_module, object_module);
    append_backend_diagnostics(diagnostics, result.diagnostics);
    if (asm_result == nullptr || diagnostics.has_error()) {
        result.status = AArch64CodegenStatus::Failure;
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(make_error_diagnostic(
                "aarch64-backend",
                "failed to compile the restricted LLVM IR module to AArch64 assembly"));
        }
        return result;
    }
    result.status = AArch64CodegenStatus::Success;
    result.asm_text = asm_result->get_text();
    return result;
}

AArch64ObjectCompileResult emit_module_to_object(
    const AArch64CoreIrImportedModule &imported,
    const AArch64CodegenFileRequest &request) {
    DiagnosticEngine diagnostics;
    AArch64ObjectCompileResult result;
    const BackendOptions backend_options =
        make_backend_options(request, imported.source_target_triple);
    for (const std::string &module_asm_line : imported.module_asm_lines) {
        if (!is_benign_module_asm_line(module_asm_line)) {
            result.status = AArch64CodegenStatus::UnsupportedInputFormat;
            result.diagnostics.push_back(make_error_diagnostic(
                "api",
                "object emission only accepts benign module asm directives in the current bridge: " +
                    module_asm_line));
            return result;
        }
    }

    AArch64AsmModule asm_module;
    AArch64MachineModule machine_module;
    AArch64ObjectModule object_module;
    AArch64BackendPipeline pipeline;
    if (!pipeline.build_and_finalize_module(
            *imported.module, backend_options, diagnostics, asm_module, machine_module,
            object_module)) {
        append_backend_diagnostics(diagnostics, result.diagnostics);
        result.diagnostics.push_back(make_note_diagnostic(
            "aarch64-object-pipeline",
            "AArch64 object pipeline state after machine lowering/ABI finalization "
            "failure: " +
                summarize_object_pipeline_state(machine_module, object_module,
                                                backend_options)));
        result.status = AArch64CodegenStatus::Failure;
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(make_error_diagnostic(
                "aarch64-machine-pipeline",
                "failed to lower the restricted LLVM IR module into finalized "
                "native AArch64 machine/object state"));
        }
        return result;
    }

    const std::filesystem::path object_path = create_temp_object_path();
    if (object_path.empty()) {
        result.status = AArch64CodegenStatus::Failure;
        result.diagnostics.push_back(make_error_diagnostic(
            "api", "failed to create a temporary object output path"));
        return result;
    }

    std::unique_ptr<ObjectResult> object_result = pipeline.emit_object_result(
        machine_module, object_module, backend_options, object_path, diagnostics);
    append_backend_diagnostics(diagnostics, result.diagnostics);
    std::error_code remove_error;
    std::filesystem::remove(object_path, remove_error);
    if (object_result == nullptr || diagnostics.has_error()) {
        result.diagnostics.push_back(make_note_diagnostic(
            "aarch64-object-emission",
            "AArch64 object emission state after writer/readback failure: " +
                summarize_object_pipeline_state(machine_module, object_module,
                                                backend_options, object_path)));
        result.status = AArch64CodegenStatus::Failure;
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(make_error_diagnostic(
                "aarch64-object-emission",
                "failed to emit a native AArch64 object file from the restricted "
                "LLVM IR module during object writer/readback"));
        }
        return result;
    }

    result.status = AArch64CodegenStatus::Success;
    result.object_bytes = object_result->get_bytes();
    return result;
}

template <typename ResultT>
ResultT compile_imported_module(const AArch64CoreIrImportedModule &imported,
                                const AArch64CodegenFileRequest &request) {
    ResultT result;
    if (imported.module == nullptr || has_error_diagnostics(imported.diagnostics)) {
        result.status = AArch64CodegenStatus::UnsupportedInputFormat;
        result.diagnostics = imported.diagnostics;
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(make_error_diagnostic(
                "llvm-import",
                "failed to import the restricted LLVM IR module",
                request.input_file_path));
        }
        return result;
    }

    if constexpr (std::is_same<ResultT, AArch64AsmCompileResult>::value) {
        return emit_module_to_asm(imported, request);
    } else {
        return emit_module_to_object(imported, request);
    }
}

} // namespace

AArch64AsmCompileResult
compile_restricted_llvm_file_to_asm(const AArch64CodegenFileRequest &request) {
    return compile_imported_module<AArch64AsmCompileResult>(
        lower_llvm_import_model_to_core_ir(
            parse_restricted_llvm_ir_file(request.input_file_path)),
        request);
}

AArch64ObjectCompileResult compile_restricted_llvm_file_to_object(
    const AArch64CodegenFileRequest &request) {
    return compile_imported_module<AArch64ObjectCompileResult>(
        lower_llvm_import_model_to_core_ir(
            parse_restricted_llvm_ir_file(request.input_file_path)),
        request);
}

AArch64AsmCompileResult compile_restricted_llvm_text_to_asm(
    const AArch64CodegenFileRequest &request, const std::string &source_name,
    const std::string &text) {
    return compile_imported_module<AArch64AsmCompileResult>(
        lower_llvm_import_model_to_core_ir(
            parse_restricted_llvm_ir_text(source_name, text)),
        request);
}

AArch64ObjectCompileResult compile_restricted_llvm_text_to_object(
    const AArch64CodegenFileRequest &request, const std::string &source_name,
    const std::string &text) {
    return compile_imported_module<AArch64ObjectCompileResult>(
        lower_llvm_import_model_to_core_ir(
            parse_restricted_llvm_ir_text(source_name, text)),
        request);
}

} // namespace sysycc
