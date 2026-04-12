#include <memory>
#include <string>
#include <vector>

#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "tests/asm/aarch64_native_backend_test_utils.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *f64_type = context->create_type<CoreIrFloatType>(CoreIrFloatKind::Float64);
    auto *f32_type = context->create_type<CoreIrFloatType>(CoreIrFloatKind::Float32);
    auto *mixed_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, f64_type, f32_type});
    auto *module = context->create_module<CoreIrModule>(
        "aarch64_native_float_global_aggregate");

    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantFloat>(f64_type, "2.0");
    auto *three = context->create_constant<CoreIrConstantFloat>(f32_type, "3.0f");
    auto *init = context->create_constant<CoreIrConstantAggregate>(
        mixed_type, std::vector<const CoreIrConstant *>{one, two, three});

    module->create_global<CoreIrGlobal>("g_pair", mixed_type, init, false, false);

    const std::string asm_text = test::emit_aarch64_native_asm(*module);
    test::assert_contains(asm_text, "g_pair:\n  .word 0x00000001\n  .zero 4");
    test::assert_contains(asm_text, "\n  .xword 0x4000000000000000");
    test::assert_contains(asm_text, "\n  .word 0x40400000\n  .zero 4");
    return 0;
}
