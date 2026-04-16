#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "backend/asm_gen/riscv64/api/riscv64_codegen_api.hpp"

namespace {

bool write_object_file(const std::filesystem::path &path,
                       const std::vector<std::uint8_t> &bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        return false;
    }
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

void print_diagnostics(
    const std::vector<sysycc::Riscv64CodegenDiagnostic> &diagnostics) {
    for (const sysycc::Riscv64CodegenDiagnostic &diagnostic : diagnostics) {
        std::cerr << diagnostic.stage_name << ": " << diagnostic.message
                  << "\n";
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: " << argv[0]
                  << " <input.ll> <input.bc> <output.o>\n";
        return 1;
    }

    sysycc::Riscv64CodegenFileRequest request;
    request.options.target_triple = "riscv64-unknown-linux-gnu";

    request.input_file_path = argv[1];
    const sysycc::Riscv64AsmCompileResult asm_result =
        sysycc::compile_ll_file_to_asm(request);
    print_diagnostics(asm_result.diagnostics);
    if (asm_result.status != sysycc::Riscv64CodegenStatus::Success) {
        return 1;
    }
    if (asm_result.asm_text.find("add") == std::string::npos) {
        std::cerr << "missing add symbol in generated asm\n";
        return 1;
    }

    request.input_file_path = argv[2];
    const sysycc::Riscv64ObjectCompileResult object_result =
        sysycc::compile_bc_file_to_object(request);
    print_diagnostics(object_result.diagnostics);
    if (object_result.status != sysycc::Riscv64CodegenStatus::Success) {
        return 1;
    }
    if (!write_object_file(argv[3], object_result.object_bytes)) {
        std::cerr << "failed to write object output\n";
        return 1;
    }
    return 0;
}
