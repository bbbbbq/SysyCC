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
        context->create_module<CoreIrModule>("aarch64_native_phi_loop_header");
    auto *function =
        module->create_function<CoreIrFunction>("loopphi", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");

    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *four = context->create_constant<CoreIrConstantInt>(i32_type, 4);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *index = header->create_instruction<CoreIrPhiInst>(i32_type, "index");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "loop_cond", index, four);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *next = body->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add,
                                                            i32_type, "next", index,
                                                            one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, index);

    index->add_incoming(entry, zero);
    index->add_incoming(body, next);

    const std::string asm_text = test::emit_aarch64_native_asm(*module);
    test::assert_contains(asm_text, ".Lloopphi_entry_to_header_phi:");
    test::assert_contains(asm_text, ".Lloopphi_body_to_header_phi:");
    test::assert_contains(asm_text, "cbnz ");
    return 0;
}
