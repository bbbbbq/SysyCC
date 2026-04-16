#include "backend/asm_gen/riscv64/api/riscv64_codegen_api.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include "backend/asm_gen/riscv64/support/riscv64_llvm_target_machine_bridge.hpp"

namespace sysycc {

namespace {

Riscv64CodegenDiagnostic make_error_diagnostic(std::string stage_name,
                                               std::string message,
                                               std::string file_path = {},
                                               int line = 0,
                                               int column = 0) {
    Riscv64CodegenDiagnostic diagnostic;
    diagnostic.severity = Riscv64CodegenDiagnosticSeverity::Error;
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
    result.status = Riscv64CodegenStatus::InvalidRequest;
    result.diagnostics.push_back(make_error_diagnostic(
        "api", std::move(message), std::move(file_path)));
    return result;
}

template <typename ResultT>
ResultT validate_request(const Riscv64CodegenFileRequest &request) {
    if (request.input_file_path.empty()) {
        return make_invalid_request_result<ResultT>(
            "missing input file path for the RISC-V64 codegen API request");
    }

    std::error_code error_code;
    const std::filesystem::path input_path(request.input_file_path);
    const bool exists = std::filesystem::exists(input_path, error_code);
    if (error_code) {
        return make_invalid_request_result<ResultT>(
            "failed to inspect input file path for the RISC-V64 codegen API request",
            request.input_file_path);
    }
    if (!exists) {
        return make_invalid_request_result<ResultT>(
            "input file does not exist for the RISC-V64 codegen API request",
            request.input_file_path);
    }
    if (!std::filesystem::is_regular_file(input_path, error_code) ||
        error_code) {
        return make_invalid_request_result<ResultT>(
            "input path is not a regular file for the RISC-V64 codegen API request",
            request.input_file_path);
    }

    return ResultT{};
}

} // namespace

Riscv64CodegenCapabilities get_riscv64_codegen_capabilities() {
    Riscv64CodegenCapabilities capabilities;
    capabilities.supports_ll_files = true;
    capabilities.supports_bc_files = true;
    capabilities.supports_asm_output = true;
    capabilities.supports_object_output = true;
    return capabilities;
}

Riscv64AsmCompileResult
compile_ll_file_to_asm(const Riscv64CodegenFileRequest &request) {
    Riscv64AsmCompileResult validation_result =
        validate_request<Riscv64AsmCompileResult>(request);
    if (validation_result.status == Riscv64CodegenStatus::InvalidRequest) {
        return validation_result;
    }
    return compile_llvm_ir_file_to_riscv64_asm(request);
}

Riscv64ObjectCompileResult
compile_ll_file_to_object(const Riscv64CodegenFileRequest &request) {
    Riscv64ObjectCompileResult validation_result =
        validate_request<Riscv64ObjectCompileResult>(request);
    if (validation_result.status == Riscv64CodegenStatus::InvalidRequest) {
        return validation_result;
    }
    return compile_llvm_ir_file_to_riscv64_object(request);
}

Riscv64AsmCompileResult
compile_bc_file_to_asm(const Riscv64CodegenFileRequest &request) {
    Riscv64AsmCompileResult validation_result =
        validate_request<Riscv64AsmCompileResult>(request);
    if (validation_result.status == Riscv64CodegenStatus::InvalidRequest) {
        return validation_result;
    }
    return compile_llvm_bitcode_file_to_riscv64_asm(request);
}

Riscv64ObjectCompileResult
compile_bc_file_to_object(const Riscv64CodegenFileRequest &request) {
    Riscv64ObjectCompileResult validation_result =
        validate_request<Riscv64ObjectCompileResult>(request);
    if (validation_result.status == Riscv64CodegenStatus::InvalidRequest) {
        return validation_result;
    }
    return compile_llvm_bitcode_file_to_riscv64_object(request);
}

} // namespace sysycc
