#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "backend/asm_gen/aarch64/api/aarch64_codegen_api.hpp"
#include "backend/ir/lower/lowering/llvm/core_ir_llvm_target_backend.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *f128_type =
        context->create_type<CoreIrFloatType>(CoreIrFloatKind::Float128);
    auto *module =
        context->create_module<CoreIrModule>("aarch64_native_reject_float_backend");
    auto *value =
        context->create_constant<CoreIrConstantFloat>(f128_type, "1.0");
    module->create_global<CoreIrGlobal>("g", f128_type, value, true, false);

    DiagnosticEngine diagnostics;
    CoreIrLlvmTargetBackend llvm_backend;
    std::unique_ptr<IRResult> ir_result = llvm_backend.Lower(*module, diagnostics);
    assert(ir_result != nullptr);
    assert(!diagnostics.has_error());

    std::filesystem::path temp_directory =
        std::filesystem::temp_directory_path();
    std::string pattern =
        (temp_directory / "sysycc_aarch64_reject_XXXXXX.ll").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const int fd = mkstemps(buffer.data(), 3);
    assert(fd >= 0);
    close(fd);
    const std::filesystem::path llvm_ir_file(buffer.data());
    {
        std::ofstream ofs(llvm_ir_file);
        assert(ofs.is_open());
        ofs << ir_result->get_text();
        assert(ofs.good());
    }

    AArch64CodegenFileRequest request;
    request.input_file_path = llvm_ir_file.string();
    request.options.target_triple = "aarch64-unknown-linux-gnu";
    const AArch64AsmCompileResult result = compile_ll_file_to_asm(request);
    std::error_code remove_error;
    std::filesystem::remove(llvm_ir_file, remove_error);

    assert(result.status != AArch64CodegenStatus::Success);
    bool found_expected_message = false;
    for (const AArch64CodegenDiagnostic &diagnostic : result.diagnostics) {
        if (diagnostic.message.find(
                "non-zero float128 global initializers are not yet supported") !=
            std::string::npos) {
            found_expected_message = true;
            break;
        }
    }
    assert(found_expected_message);
    return 0;
}
