#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"

#include <iostream>
#include <string>

namespace {

bool has_blockaddress_diagnostic(
    const std::vector<sysycc::AArch64CodegenDiagnostic> &diagnostics) {
    for (const auto &diagnostic : diagnostics) {
        if (diagnostic.message.find("blockaddress constants are not yet representable") !=
                std::string::npos &&
            diagnostic.message.find("@foo, %target") != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: aarch64_codegen_api_blockaddress_boundary <input.ll>\n";
        return 1;
    }

    sysycc::AArch64CodegenFileRequest request;
    request.input_file_path = argv[1];
    request.options.target_triple = "aarch64-unknown-linux-gnu";

    const sysycc::AArch64AsmCompileResult asm_result =
        sysycc::compile_ll_file_to_asm(request);
    if (asm_result.status == sysycc::AArch64CodegenStatus::Success ||
        !has_blockaddress_diagnostic(asm_result.diagnostics)) {
        std::cerr << "expected asm compile to fail with a precise blockaddress diagnostic\n";
        return 1;
    }

    const sysycc::AArch64ObjectCompileResult object_result =
        sysycc::compile_ll_file_to_object(request);
    if (object_result.status == sysycc::AArch64CodegenStatus::Success ||
        !has_blockaddress_diagnostic(object_result.diagnostics)) {
        std::cerr << "expected object compile to fail with a precise blockaddress diagnostic\n";
        return 1;
    }

    return 0;
}
