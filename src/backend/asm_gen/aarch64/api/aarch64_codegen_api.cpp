#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include "backend/asm_gen/aarch64/api/aarch64_llvm_bitcode_loader.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_codegen_backend_bridge.hpp"

namespace sysycc {

namespace {

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

template <typename ResultT>
ResultT make_invalid_request_result(std::string message,
                                    std::string file_path = {}) {
    ResultT result;
    result.status = AArch64CodegenStatus::InvalidRequest;
    result.diagnostics.push_back(make_error_diagnostic(
        "api", std::move(message), std::move(file_path)));
    return result;
}

template <typename ResultT>
ResultT validate_request(const AArch64CodegenFileRequest &request) {
    if (request.input_file_path.empty()) {
        return make_invalid_request_result<ResultT>(
            "missing input file path for the AArch64 codegen API request");
    }

    std::error_code error_code;
    const std::filesystem::path input_path(request.input_file_path);
    const bool exists = std::filesystem::exists(input_path, error_code);
    if (error_code) {
        return make_invalid_request_result<ResultT>(
            "failed to inspect input file path for the AArch64 codegen API request",
            request.input_file_path);
    }
    if (!exists) {
        return make_invalid_request_result<ResultT>(
            "input file does not exist for the AArch64 codegen API request",
            request.input_file_path);
    }
    if (!std::filesystem::is_regular_file(input_path, error_code) || error_code) {
        return make_invalid_request_result<ResultT>(
            "input path is not a regular file for the AArch64 codegen API request",
            request.input_file_path);
    }

    return ResultT{};
}

} // namespace

AArch64CodegenCapabilities get_aarch64_codegen_capabilities() {
    AArch64CodegenCapabilities capabilities;
    capabilities.supports_ll_files = true;
    capabilities.supports_bc_files = true;
    capabilities.supports_asm_output = true;
    capabilities.supports_object_output = true;
    return capabilities;
}

AArch64AsmCompileResult
compile_ll_file_to_asm(const AArch64CodegenFileRequest &request) {
    AArch64AsmCompileResult validation_result =
        validate_request<AArch64AsmCompileResult>(request);
    if (validation_result.status == AArch64CodegenStatus::InvalidRequest) {
        return validation_result;
    }
    return compile_restricted_llvm_file_to_asm(request);
}

AArch64ObjectCompileResult
compile_ll_file_to_object(const AArch64CodegenFileRequest &request) {
    AArch64ObjectCompileResult validation_result =
        validate_request<AArch64ObjectCompileResult>(request);
    if (validation_result.status == AArch64CodegenStatus::InvalidRequest) {
        return validation_result;
    }
    return compile_restricted_llvm_file_to_object(request);
}

AArch64AsmCompileResult
compile_bc_file_to_asm(const AArch64CodegenFileRequest &request) {
    AArch64AsmCompileResult validation_result =
        validate_request<AArch64AsmCompileResult>(request);
    if (validation_result.status == AArch64CodegenStatus::InvalidRequest) {
        return validation_result;
    }

    const AArch64BitcodeTextModule loaded =
        load_llvm_bitcode_as_text(request.input_file_path);
    if (!loaded.ok) {
        AArch64AsmCompileResult result;
        result.status = AArch64CodegenStatus::Failure;
        result.diagnostics = loaded.diagnostics;
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(make_error_diagnostic(
                "llvm-bitcode",
                "failed to read LLVM bitcode in-process",
                request.input_file_path));
        }
        return result;
    }
    return compile_restricted_llvm_text_to_asm(request, request.input_file_path,
                                               loaded.textual_llvm_ir);
}

AArch64ObjectCompileResult
compile_bc_file_to_object(const AArch64CodegenFileRequest &request) {
    AArch64ObjectCompileResult validation_result =
        validate_request<AArch64ObjectCompileResult>(request);
    if (validation_result.status == AArch64CodegenStatus::InvalidRequest) {
        return validation_result;
    }

    const AArch64BitcodeTextModule loaded =
        load_llvm_bitcode_as_text(request.input_file_path);
    if (!loaded.ok) {
        AArch64ObjectCompileResult result;
        result.status = AArch64CodegenStatus::Failure;
        result.diagnostics = loaded.diagnostics;
        if (result.diagnostics.empty()) {
            result.diagnostics.push_back(make_error_diagnostic(
                "llvm-bitcode",
                "failed to read LLVM bitcode in-process",
                request.input_file_path));
        }
        return result;
    }
    return compile_restricted_llvm_text_to_object(
        request, request.input_file_path, loaded.textual_llvm_ir);
}

} // namespace sysycc
