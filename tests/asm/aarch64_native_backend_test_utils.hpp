#pragma once

#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"
#include "backend/ir/lower/lowering/llvm/core_ir_llvm_target_backend.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc::test {

inline std::filesystem::path write_core_ir_as_temp_llvm_ir(
    const CoreIrModule &module, DiagnosticEngine &diagnostics) {
    CoreIrLlvmTargetBackend llvm_backend;
    std::unique_ptr<IRResult> ir_result = llvm_backend.Lower(module, diagnostics);
    assert(ir_result != nullptr);
    assert(!diagnostics.has_error());

    std::filesystem::path temp_directory =
        std::filesystem::temp_directory_path();
    std::string pattern =
        (temp_directory / "sysycc_aarch64_test_XXXXXX.ll").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const int fd = mkstemps(buffer.data(), 3);
    assert(fd >= 0);
    close(fd);

    const std::filesystem::path file_path(buffer.data());
    std::ofstream ofs(file_path);
    assert(ofs.is_open());
    ofs << ir_result->get_text();
    assert(ofs.good());
    return file_path;
}

inline std::string emit_aarch64_native_asm(const CoreIrModule &module) {
    DiagnosticEngine diagnostics;
    const std::filesystem::path llvm_ir_file =
        write_core_ir_as_temp_llvm_ir(module, diagnostics);

    AArch64CodegenFileRequest request;
    request.input_file_path = llvm_ir_file.string();
    request.options.target_triple = "aarch64-unknown-linux-gnu";
    const AArch64AsmCompileResult result = compile_ll_file_to_asm(request);
    std::error_code remove_error;
    std::filesystem::remove(llvm_ir_file, remove_error);

    assert(result.status == AArch64CodegenStatus::Success);
    assert(result.diagnostics.empty());
    return result.asm_text;
}

inline void assert_contains(const std::string &text, const std::string &needle) {
    assert(text.find(needle) != std::string::npos);
}

inline std::size_t count_occurrences(const std::string &text,
                                     const std::string &needle) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

} // namespace sysycc::test
