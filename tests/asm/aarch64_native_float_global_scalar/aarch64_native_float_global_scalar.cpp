#include <memory>
#include <string>

#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "tests/asm/aarch64_native_backend_test_utils.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *f16_type = context->create_type<CoreIrFloatType>(CoreIrFloatKind::Float16);
    auto *f32_type = context->create_type<CoreIrFloatType>(CoreIrFloatKind::Float32);
    auto *f64_type = context->create_type<CoreIrFloatType>(CoreIrFloatKind::Float64);
    auto *module =
        context->create_module<CoreIrModule>("aarch64_native_float_global_scalar");

    auto *half_value =
        context->create_constant<CoreIrConstantFloat>(f16_type, "1.0");
    auto *float_value =
        context->create_constant<CoreIrConstantFloat>(f32_type, "1.0f");
    auto *double_value =
        context->create_constant<CoreIrConstantFloat>(f64_type, "1.5");

    module->create_global<CoreIrGlobal>("g_half", f16_type, half_value, false, false);
    module->create_global<CoreIrGlobal>("g_float", f32_type, float_value, false,
                                        false);
    module->create_global<CoreIrGlobal>("g_double", f64_type, double_value, false,
                                        false);

    const std::string asm_text = test::emit_aarch64_native_asm(*module);
    test::assert_contains(asm_text, ".arch armv8.2-a+fp16");
    test::assert_contains(asm_text, "g_half:\n  .hword 0x3c00");
    test::assert_contains(asm_text, "g_float:\n  .word 0x3f800000");
    test::assert_contains(asm_text, "g_double:\n  .xword 0x3ff8000000000000");
    return 0;
}
