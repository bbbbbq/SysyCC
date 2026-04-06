#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/lcssa/core_ir_lcssa_pass.hpp"
#include "backend/ir/loop_idiom/core_ir_loop_idiom_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

CompilerContext make_compiler_context(std::unique_ptr<CoreIrContext> context,
                                      CoreIrModule *module) {
    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    return compiler_context;
}

void test_folds_counted_additive_reduction() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("loop_idiom_add");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);
    auto *ten = context->create_constant<CoreIrConstantInt>(i32_type, 10);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *sum = header->create_instruction<CoreIrPhiInst>(i32_type, "sum");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, ten);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *next_iv = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "iv.next", iv, one);
    auto *next_sum = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum.next", sum, three);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *lcssa_use = exit->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "use", sum, zero);
    exit->create_instruction<CoreIrReturnInst>(void_type, lcssa_use);

    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next_iv);
    sum->add_incoming(entry, zero);
    sum->add_incoming(body, next_sum);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrLcssaPass lcssa;
    assert(lcssa.Run(compiler_context).ok);
    CoreIrLoopIdiomPass pass;
    assert(pass.Run(compiler_context).ok);

    auto *entry_jump =
        dynamic_cast<CoreIrJumpInst *>(entry->get_instructions().back().get());
    assert(entry_jump != nullptr);
    assert(entry_jump->get_target_block() == exit);

    bool has_mul = false;
    bool has_final_add = false;
    for (const auto &instruction : entry->get_instructions()) {
        auto *binary = dynamic_cast<CoreIrBinaryInst *>(instruction.get());
        if (binary == nullptr) {
            continue;
        }
        has_mul = has_mul || binary->get_binary_opcode() == CoreIrBinaryOpcode::Mul;
        has_final_add =
            has_final_add || binary->get_binary_opcode() == CoreIrBinaryOpcode::Add;
    }
    assert(has_mul);
    assert(has_final_add);
}

} // namespace

int main() {
    test_folds_counted_additive_reduction();
    return 0;
}

