#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"

#include <fstream>
#include <cstdint>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    if (argc != 4) {
        std::cerr << "usage: aarch64_codegen_api_smoke <input.ll> <input.bc> <output.o>\n";
        return 1;
    }

    const sysycc::AArch64CodegenCapabilities capabilities =
        sysycc::get_aarch64_codegen_capabilities();
    if (!capabilities.supports_ll_files || !capabilities.supports_bc_files ||
        !capabilities.supports_asm_output ||
        !capabilities.supports_object_output) {
        std::cerr << "unexpected AArch64 codegen capability surface\n";
        return 1;
    }

    sysycc::AArch64CodegenFileRequest ll_request;
    ll_request.input_file_path = argv[1];
    ll_request.options.target_triple = "aarch64-unknown-linux-gnu";

    const sysycc::AArch64AsmCompileResult asm_result =
        sysycc::compile_ll_file_to_asm(ll_request);
    if (asm_result.status != sysycc::AArch64CodegenStatus::Success ||
        asm_result.asm_text.find(".globl add") == std::string::npos ||
        asm_result.asm_text.find("bl add") == std::string::npos ||
        asm_result.asm_text.find("values:") == std::string::npos) {
        std::cerr << "unexpected LLVM IR text asm compile result\n";
        return 1;
    }

    const sysycc::AArch64ObjectCompileResult ll_object_result =
        sysycc::compile_ll_file_to_object(ll_request);
    if (ll_object_result.status != sysycc::AArch64CodegenStatus::Success ||
        ll_object_result.object_bytes.size() < 4 ||
        ll_object_result.object_bytes[0] != 0x7f ||
        ll_object_result.object_bytes[1] != static_cast<std::uint8_t>('E') ||
        ll_object_result.object_bytes[2] != static_cast<std::uint8_t>('L') ||
        ll_object_result.object_bytes[3] != static_cast<std::uint8_t>('F')) {
        std::cerr << "unexpected LLVM IR text object compile result\n";
        return 1;
    }

    std::ofstream ofs(argv[3], std::ios::binary);
    if (!ofs.is_open()) {
        std::cerr << "failed to open object output path\n";
        return 1;
    }
    ofs.write(reinterpret_cast<const char *>(ll_object_result.object_bytes.data()),
              static_cast<std::streamsize>(ll_object_result.object_bytes.size()));
    if (!ofs.good()) {
        std::cerr << "failed to write object output bytes\n";
        return 1;
    }

    sysycc::AArch64CodegenFileRequest bc_request;
    bc_request.input_file_path = argv[2];
    bc_request.options.target_triple = "aarch64-unknown-linux-gnu";
    bc_request.options.position_independent = true;

    const sysycc::AArch64AsmCompileResult bc_asm_result =
        sysycc::compile_bc_file_to_asm(bc_request);
    if (bc_asm_result.status != sysycc::AArch64CodegenStatus::Success ||
        bc_asm_result.asm_text.find(".globl add") == std::string::npos ||
        bc_asm_result.asm_text.find("bl add") == std::string::npos) {
        std::cerr << "unexpected LLVM bitcode asm compile result\n";
        return 1;
    }

    const sysycc::AArch64ObjectCompileResult bc_result =
        sysycc::compile_bc_file_to_object(bc_request);
    if (bc_result.status != sysycc::AArch64CodegenStatus::Success ||
        bc_result.object_bytes.size() < 4 ||
        bc_result.object_bytes[0] != 0x7f ||
        bc_result.object_bytes[1] != static_cast<std::uint8_t>('E') ||
        bc_result.object_bytes[2] != static_cast<std::uint8_t>('L') ||
        bc_result.object_bytes[3] != static_cast<std::uint8_t>('F')) {
        std::cerr << "unexpected LLVM bitcode object compile result\n";
        return 1;
    }

    std::cout << "aarch64 codegen api smoke passed\n";
    return 0;
}
