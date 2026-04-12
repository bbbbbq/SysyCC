#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sysycc {

enum class AArch64CodegenStatus : std::uint8_t {
    Success,
    InvalidRequest,
    UnsupportedInputFormat,
    Failure,
};

enum class AArch64CodegenDiagnosticSeverity : std::uint8_t {
    Error,
    Warning,
    Note,
};

struct AArch64CodegenOptions {
    std::string target_triple;
    bool position_independent = false;
    bool debug_info = false;
};

struct AArch64CodegenFileRequest {
    std::string input_file_path;
    AArch64CodegenOptions options;
};

struct AArch64CodegenDiagnostic {
    AArch64CodegenDiagnosticSeverity severity =
        AArch64CodegenDiagnosticSeverity::Error;
    std::string stage_name;
    std::string message;
    std::string file_path;
    int line = 0;
    int column = 0;
};

struct AArch64CodegenCapabilities {
    bool supports_ll_files = false;
    bool supports_bc_files = false;
    bool supports_asm_output = true;
    bool supports_object_output = true;
};

struct AArch64AsmCompileResult {
    AArch64CodegenStatus status = AArch64CodegenStatus::Failure;
    std::string asm_text;
    std::vector<AArch64CodegenDiagnostic> diagnostics;
};

struct AArch64ObjectCompileResult {
    AArch64CodegenStatus status = AArch64CodegenStatus::Failure;
    std::vector<std::uint8_t> object_bytes;
    std::vector<AArch64CodegenDiagnostic> diagnostics;
};

AArch64CodegenCapabilities get_aarch64_codegen_capabilities();

AArch64AsmCompileResult
compile_ll_file_to_asm(const AArch64CodegenFileRequest &request);

AArch64ObjectCompileResult
compile_ll_file_to_object(const AArch64CodegenFileRequest &request);

AArch64AsmCompileResult
compile_bc_file_to_asm(const AArch64CodegenFileRequest &request);

AArch64ObjectCompileResult
compile_bc_file_to_object(const AArch64CodegenFileRequest &request);

} // namespace sysycc
