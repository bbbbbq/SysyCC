#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr
            << "usage: aarch64_codegen_api_bitcast_constant <input.ll> <output.o>\n";
        return 1;
    }

    sysycc::AArch64CodegenFileRequest request;
    request.input_file_path = argv[1];
    request.options.target_triple = "aarch64-unknown-linux-gnu";

    const sysycc::AArch64AsmCompileResult asm_result =
        sysycc::compile_ll_file_to_asm(request);
    if (asm_result.status != sysycc::AArch64CodegenStatus::Success) {
        std::cerr << "asm compile failed\n";
        for (const auto &diagnostic : asm_result.diagnostics) {
            std::cerr << diagnostic.stage_name << ": " << diagnostic.message
                      << "\n";
        }
        return 1;
    }
    if (asm_result.asm_text.find("seed:") == std::string::npos ||
        asm_result.asm_text.find("seed_ptr:") == std::string::npos ||
        asm_result.asm_text.find("pair:") == std::string::npos ||
        asm_result.asm_text.find("field_via_i8:") == std::string::npos ||
        asm_result.asm_text.find(".globl read_seed") == std::string::npos ||
        asm_result.asm_text.find("bl read_seed") == std::string::npos ||
        asm_result.asm_text.find("seed_alias") != std::string::npos ||
        asm_result.asm_text.find("read_seed_alias") != std::string::npos) {
        std::cerr << "unexpected asm output\n";
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
        std::cerr << "unexpected object output\n";
        return 1;
    }

    std::ofstream ofs(argv[2], std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "failed to open output object path\n";
        return 1;
    }
    ofs.write(reinterpret_cast<const char *>(object_result.object_bytes.data()),
              static_cast<std::streamsize>(object_result.object_bytes.size()));
    if (!ofs.good()) {
        std::cerr << "failed to write object bytes\n";
        return 1;
    }
    return 0;
}
