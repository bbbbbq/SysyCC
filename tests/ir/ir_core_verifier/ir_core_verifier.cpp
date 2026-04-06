#include <cassert>
#include <memory>

#include "backend/ir/verify/core_ir_verifier.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

using namespace sysycc;

namespace {

void test_valid_module_passes_verification() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("valid_module");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    entry->create_instruction<CoreIrReturnInst>(void_type, seven);

    CoreIrVerifier verifier;
    const CoreIrVerifyResult module_result = verifier.verify_module(*module);
    const CoreIrVerifyResult function_result = verifier.verify_function(*function);
    assert(module_result.ok);
    assert(module_result.issues.empty());
    assert(function_result.ok);
    assert(function_result.issues.empty());
}

void test_invalid_terminator_layout() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("terminator_layout");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    entry->create_instruction<CoreIrJumpInst>(void_type, exit);
    entry->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i32_type,
                                                "late", one, two);
    exit->create_instruction<CoreIrReturnInst>(void_type, one);

    CoreIrVerifier verifier;
    const CoreIrVerifyResult result = verifier.verify_module(*module);
    assert(!result.ok);
}

void test_invalid_phi_position() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("phi_layout");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    entry->create_instruction<CoreIrBinaryInst>(CoreIrBinaryOpcode::Add, i32_type,
                                                "sum", one, two);
    auto *phi = entry->create_instruction<CoreIrPhiInst>(i32_type, "phi");
    phi->add_incoming(entry, one);
    entry->create_instruction<CoreIrReturnInst>(void_type, one);

    CoreIrVerifier verifier;
    const CoreIrVerifyResult result = verifier.verify_function(*function);
    assert(!result.ok);
}

void test_phi_predecessor_mismatch() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("phi_pred_mismatch");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *left = function->create_basic_block<CoreIrBasicBlock>("left");
    auto *right = function->create_basic_block<CoreIrBasicBlock>("right");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, cond, left, right);
    left->create_instruction<CoreIrJumpInst>(void_type, merge);
    right->create_instruction<CoreIrJumpInst>(void_type, merge);
    auto *phi = merge->create_instruction<CoreIrPhiInst>(i32_type, "phi");
    phi->add_incoming(left, one);
    merge->create_instruction<CoreIrReturnInst>(void_type, two);

    CoreIrVerifier verifier;
    const CoreIrVerifyResult result = verifier.verify_module(*module);
    assert(!result.ok);
}

void test_dangling_use_def() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("dangling_use");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *sum = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", one, two);
    entry->create_instruction<CoreIrReturnInst>(void_type, sum);
    one->remove_use(sum, 0);

    CoreIrVerifier verifier;
    const CoreIrVerifyResult result = verifier.verify_module(*module);
    assert(!result.ok);
}

void test_invalid_parent_pointer() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("parent_mismatch");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *other = function->create_basic_block<CoreIrBasicBlock>("other");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *ret = entry->create_instruction<CoreIrReturnInst>(void_type, one);
    ret->set_parent(other);

    CoreIrVerifier verifier;
    const CoreIrVerifyResult result = verifier.verify_module(*module);
    assert(!result.ok);
}

} // namespace

int main() {
    test_valid_module_passes_verification();
    test_invalid_terminator_layout();
    test_invalid_phi_position();
    test_phi_predecessor_mismatch();
    test_dangling_use_def();
    test_invalid_parent_pointer();
    return 0;
}
