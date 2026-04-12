#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "usage: aarch64_codegen_api_vector_syntax <input.ll> <output.o>\n";
        return 1;
    }

    sysycc::AArch64CodegenFileRequest request;
    request.input_file_path = argv[1];
    request.options.target_triple = "aarch64-unknown-linux-gnu";

    const sysycc::AArch64AsmCompileResult asm_result =
        sysycc::compile_ll_file_to_asm(request);
    if (asm_result.status != sysycc::AArch64CodegenStatus::Success) {
        for (const auto &diagnostic : asm_result.diagnostics) {
            std::cerr << diagnostic.stage_name << ": " << diagnostic.message
                      << "\n";
        }
        return 1;
    }
    if (asm_result.asm_text.find(".globl vec_demo") == std::string::npos ||
        asm_result.asm_text.find("ldr w") == std::string::npos ||
        asm_result.asm_text.find("str w") == std::string::npos ||
        asm_result.asm_text.find("add w") == std::string::npos) {
        std::cerr << "unexpected vector asm output\n";
        return 1;
    }

    const sysycc::AArch64ObjectCompileResult object_result =
        sysycc::compile_ll_file_to_object(request);
    if (object_result.status != sysycc::AArch64CodegenStatus::Success ||
        object_result.object_bytes.size() < 4 ||
        object_result.object_bytes[0] != 0x7f ||
        object_result.object_bytes[1] != static_cast<std::uint8_t>('E') ||
        object_result.object_bytes[2] != static_cast<std::uint8_t>('L') ||
        object_result.object_bytes[3] != static_cast<std::uint8_t>('F')) {
        std::cerr << "unexpected vector object output\n";
        return 1;
    }

    std::ofstream ofs(argv[2], std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "failed to open vector object output path\n";
        return 1;
    }
    ofs.write(reinterpret_cast<const char *>(object_result.object_bytes.data()),
              static_cast<std::streamsize>(object_result.object_bytes.size()));
    if (!ofs.good()) {
        std::cerr << "failed to write vector object bytes\n";
        return 1;
    }
    return 0;
}
