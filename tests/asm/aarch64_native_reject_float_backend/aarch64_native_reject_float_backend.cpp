#include <cassert>
#include <memory>
#include <string>

#include "backend/asm_gen/aarch64/aarch64_asm_backend.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "backend/asm_gen/backend_options.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
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

    BackendOptions options;
    options.set_backend_kind(BackendKind::AArch64Native);
    options.set_target_triple("aarch64-unknown-linux-gnu");

    DiagnosticEngine diagnostics;
    AArch64AsmBackend backend;
    std::unique_ptr<AsmResult> result =
        backend.Generate(*module, options, diagnostics);

    assert(result == nullptr);
    assert(diagnostics.has_error());
    bool found_expected_message = false;
    for (const Diagnostic &diagnostic : diagnostics.get_diagnostics()) {
        if (diagnostic.get_message().find(
                "non-zero float128 global initializers are not yet supported") !=
            std::string::npos) {
            found_expected_message = true;
            break;
        }
    }
    assert(found_expected_message);
    return 0;
}
