#include <cassert>
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
    auto *module = context->create_module<CoreIrModule>(
        "aarch64_native_phi_parallel_copy_cycle");
    auto *function =
        module->create_function<CoreIrFunction>("cycle", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");

    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *lhs = header->create_instruction<CoreIrPhiInst>(i32_type, "lhs");
    auto *rhs = header->create_instruction<CoreIrPhiInst>(i32_type, "rhs");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "continue_loop", lhs,
        three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *sum =
        exit->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i32_type,
                                                   "sum", lhs, rhs);
    exit->create_instruction<CoreIrReturnInst>(void_type, sum);

    lhs->add_incoming(entry, one);
    lhs->add_incoming(body, rhs);
    rhs->add_incoming(entry, two);
    rhs->add_incoming(body, lhs);

    const std::string asm_text = test::emit_aarch64_native_asm(*module);
    const std::string edge_label = ".Lcycle_body_to_header_phi:";
    test::assert_contains(asm_text, edge_label);
    const std::size_t edge_start = asm_text.find(edge_label);
    const std::size_t edge_end = asm_text.find("b .Lcycle_header", edge_start);
    assert(edge_start != std::string::npos);
    assert(edge_end != std::string::npos);
    const std::string edge_block = asm_text.substr(edge_start, edge_end - edge_start);
    assert(test::count_occurrences(edge_block, "\n  mov ") >= 3);
    return 0;
}
