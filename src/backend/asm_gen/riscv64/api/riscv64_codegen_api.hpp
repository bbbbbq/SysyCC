#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sysycc {

enum class Riscv64CodegenStatus : std::uint8_t {
    Success,
    InvalidRequest,
    UnsupportedInputFormat,
    Failure,
};

enum class Riscv64CodegenDiagnosticSeverity : std::uint8_t {
    Error,
    Warning,
    Note,
};

struct Riscv64CodegenOptions {
    std::string target_triple;
    bool position_independent = false;
    bool debug_info = false;
};

struct Riscv64CodegenFileRequest {
    std::string input_file_path;
    Riscv64CodegenOptions options;
};

struct Riscv64CodegenDiagnostic {
    Riscv64CodegenDiagnosticSeverity severity =
        Riscv64CodegenDiagnosticSeverity::Error;
    std::string stage_name;
    std::string message;
    std::string file_path;
    int line = 0;
    int column = 0;
};

struct Riscv64CodegenCapabilities {
    bool supports_ll_files = false;
    bool supports_bc_files = false;
    bool supports_asm_output = true;
    bool supports_object_output = true;
};

struct Riscv64AsmCompileResult {
    Riscv64CodegenStatus status = Riscv64CodegenStatus::Failure;
    std::string asm_text;
    std::vector<Riscv64CodegenDiagnostic> diagnostics;
};

struct Riscv64ObjectCompileResult {
    Riscv64CodegenStatus status = Riscv64CodegenStatus::Failure;
    std::vector<std::uint8_t> object_bytes;
    std::vector<Riscv64CodegenDiagnostic> diagnostics;
};

Riscv64CodegenCapabilities get_riscv64_codegen_capabilities();

Riscv64AsmCompileResult
compile_ll_file_to_asm(const Riscv64CodegenFileRequest &request);

Riscv64ObjectCompileResult
compile_ll_file_to_object(const Riscv64CodegenFileRequest &request);

Riscv64AsmCompileResult
compile_bc_file_to_asm(const Riscv64CodegenFileRequest &request);

Riscv64ObjectCompileResult
compile_bc_file_to_object(const Riscv64CodegenFileRequest &request);

} // namespace sysycc
