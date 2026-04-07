#include <memory>
#include <string>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "tests/asm/aarch64_native_backend_test_utils.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("aarch64_native_phi_short_circuit");
    auto *function =
        module->create_function<CoreIrFunction>("logic", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *rhs = function->create_basic_block<CoreIrBasicBlock>("rhs");
    auto *rhs_true = function->create_basic_block<CoreIrBasicBlock>("rhs_true");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");

    auto *true_value = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *false_result = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *true_result = context->create_constant<CoreIrConstantInt>(i32_type, 1);

    entry->create_instruction<CoreIrCondJumpInst>(void_type, true_value, rhs, merge);
    rhs->create_instruction<CoreIrCondJumpInst>(void_type, true_value, rhs_true, merge);
    rhs_true->create_instruction<CoreIrJumpInst>(void_type, merge);

    auto *phi = merge->create_instruction<CoreIrPhiInst>(i32_type, "logic_value");
    phi->add_incoming(entry, false_result);
    phi->add_incoming(rhs, false_result);
    phi->add_incoming(rhs_true, true_result);
    merge->create_instruction<CoreIrReturnInst>(void_type, phi);

    const std::string asm_text = test::emit_aarch64_native_asm(*module);
    test::assert_contains(asm_text, ".Llogic_entry_to_merge_phi:");
    test::assert_contains(asm_text, ".Llogic_rhs_to_merge_phi:");
    test::assert_contains(asm_text, ".Llogic_rhs_true_to_merge_phi:");
    return 0;
}
