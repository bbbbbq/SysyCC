#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

void run_pass(CompilerContext &compiler_context) {
    CoreIrCanonicalizePass canonicalize_pass;
    assert(canonicalize_pass.Run(compiler_context).ok);
    assert(!compiler_context.get_diagnostic_engine().has_error());
}

void assert_i1_compare(const CoreIrCompareInst *compare) {
    assert(compare != nullptr);
    assert(dynamic_cast<const CoreIrIntegerType *>(compare->get_type()) != nullptr);
    assert(static_cast<const CoreIrIntegerType *>(compare->get_type())
               ->get_bit_width() == 1);
}

void test_preserves_already_canonical_ir() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type =
        context->create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{}, false);
    CoreIrModule *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_pass");
    CoreIrFunction *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    CoreIrBasicBlock *entry =
        function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    CoreIrRawPrinter printer;
    const std::string before = printer.print_module(*module);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    const std::string after = printer.print_module(*module);
    assert(before == after);
}

void test_canonicalizes_branch_conditions() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i8_type = context->create_type<CoreIrIntegerType>(8);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *i64_type = context->create_type<CoreIrIntegerType>(64);
    auto *void_function_type = context->create_type<CoreIrFunctionType>(
        void_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_branch");

    auto *cmp_function = module->create_function<CoreIrFunction>(
        "branch_from_compare", void_function_type, false);
    auto *entry = cmp_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *true_block =
        cmp_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *false_block =
        cmp_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *compare = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i32_type, "cmp32", one, two);
    auto *branch = entry->create_instruction<CoreIrCondJumpInst>(
        void_type, compare, true_block, false_block);
    true_block->create_instruction<CoreIrReturnInst>(void_type);
    false_block->create_instruction<CoreIrReturnInst>(void_type);

    auto *not_function = module->create_function<CoreIrFunction>(
        "branch_from_logical_not", void_function_type, false);
    auto *not_entry = not_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *not_true =
        not_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *not_false =
        not_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *compare_eq = not_entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i32_type, "cmp_eq", one, two);
    auto *logical_not = not_entry->create_instruction<CoreIrUnaryInst>(
        CoreIrUnaryOpcode::LogicalNot, i32_type, "not_cmp", compare_eq);
    auto *not_branch = not_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, logical_not, not_true, not_false);
    not_true->create_instruction<CoreIrReturnInst>(void_type);
    not_false->create_instruction<CoreIrReturnInst>(void_type);

    auto *value_function = module->create_function<CoreIrFunction>(
        "branch_from_integer_value", void_function_type, false);
    auto *value_entry =
        value_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *value_true =
        value_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *value_false =
        value_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *value_branch = value_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, one, value_true, value_false);
    value_true->create_instruction<CoreIrReturnInst>(void_type);
    value_false->create_instruction<CoreIrReturnInst>(void_type);

    auto *extend_function = module->create_function<CoreIrFunction>(
        "branch_from_extend_cast", void_function_type, false);
    auto *extend_entry =
        extend_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *extend_true =
        extend_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *extend_false =
        extend_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *small_nonzero = context->create_constant<CoreIrConstantInt>(i8_type, 1);
    auto *extended_value = extend_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::ZeroExtend, i32_type, "zext_cond", small_nonzero);
    auto *extend_branch = extend_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, extended_value, extend_true, extend_false);
    extend_true->create_instruction<CoreIrReturnInst>(void_type);
    extend_false->create_instruction<CoreIrReturnInst>(void_type);

    auto *trunc_function = module->create_function<CoreIrFunction>(
        "branch_from_truncate_cast", void_function_type, false);
    auto *trunc_entry =
        trunc_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *trunc_true =
        trunc_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *trunc_false =
        trunc_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *wide_value =
        context->create_constant<CoreIrConstantInt>(i64_type, 0x100000000ULL);
    auto *truncated_value = trunc_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::Truncate, i32_type, "trunc_cond", wide_value);
    auto *trunc_branch = trunc_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, truncated_value, trunc_true, trunc_false);
    trunc_true->create_instruction<CoreIrReturnInst>(void_type);
    trunc_false->create_instruction<CoreIrReturnInst>(void_type);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    auto *canonical_compare =
        dynamic_cast<CoreIrCompareInst *>(branch->get_condition());
    assert_i1_compare(canonical_compare);
    assert(canonical_compare->get_predicate() ==
           CoreIrComparePredicate::SignedLess);

    auto *canonical_not_compare =
        dynamic_cast<CoreIrCompareInst *>(not_branch->get_condition());
    assert_i1_compare(canonical_not_compare);
    assert(canonical_not_compare->get_predicate() ==
           CoreIrComparePredicate::NotEqual);

    auto *value_compare =
        dynamic_cast<CoreIrCompareInst *>(value_branch->get_condition());
    assert_i1_compare(value_compare);
    assert(value_compare->get_predicate() ==
           CoreIrComparePredicate::NotEqual);
    assert(value_compare->get_lhs() == one);
    assert(dynamic_cast<CoreIrConstantInt *>(value_compare->get_rhs()) != nullptr);

    auto *extend_compare =
        dynamic_cast<CoreIrCompareInst *>(extend_branch->get_condition());
    assert_i1_compare(extend_compare);
    assert(extend_compare->get_predicate() ==
           CoreIrComparePredicate::NotEqual);
    assert(extend_compare->get_lhs() == small_nonzero);

    auto *trunc_compare =
        dynamic_cast<CoreIrCompareInst *>(trunc_branch->get_condition());
    assert_i1_compare(trunc_compare);
    assert(trunc_compare->get_predicate() ==
           CoreIrComparePredicate::NotEqual);
    assert(trunc_compare->get_lhs() == truncated_value);
}

void test_canonicalizes_integer_casts() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i8_type = context->create_type<CoreIrIntegerType>(8);
    auto *i16_type = context->create_type<CoreIrIntegerType>(16);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_casts");

    auto *merge_function = module->create_function<CoreIrFunction>(
        "merge_casts", function_type, false);
    auto *merge_entry =
        merge_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *small = context->create_constant<CoreIrConstantInt>(i8_type, 7);
    auto *inner_zext = merge_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::ZeroExtend, i16_type, "zext16", small);
    auto *outer_zext = merge_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::ZeroExtend, i32_type, "zext32", inner_zext);
    auto *merge_return =
        merge_entry->create_instruction<CoreIrReturnInst>(void_type, outer_zext);

    auto *identity_function = module->create_function<CoreIrFunction>(
        "identity_cast", function_type, false);
    auto *identity_entry =
        identity_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *value = context->create_constant<CoreIrConstantInt>(i32_type, 42);
    auto *identity_cast = identity_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::ZeroExtend, i32_type, "identity", value);
    auto *identity_return =
        identity_entry->create_instruction<CoreIrReturnInst>(void_type, identity_cast);

    auto *roundtrip_function = module->create_function<CoreIrFunction>(
        "roundtrip_cast", function_type, false);
    auto *roundtrip_entry =
        roundtrip_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *roundtrip_value = context->create_constant<CoreIrConstantInt>(i8_type, 9);
    auto *roundtrip_inner = roundtrip_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::SignExtend, i32_type, "sext32", roundtrip_value);
    auto *roundtrip_outer = roundtrip_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::Truncate, i8_type, "trunc8", roundtrip_inner);
    auto *roundtrip_return = roundtrip_entry->create_instruction<CoreIrReturnInst>(
        void_type, roundtrip_outer);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(outer_zext->get_operand() == small);
    assert(merge_return->get_return_value() == outer_zext);
    assert(merge_entry->get_instructions().size() == 2);

    assert(identity_return->get_return_value() == value);
    assert(identity_entry->get_instructions().size() == 1);

    assert(roundtrip_return->get_return_value() == roundtrip_value);
    assert(roundtrip_entry->get_instructions().size() == 1);
}

void test_removes_trampoline_blocks() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *void_function_type = context->create_type<CoreIrFunctionType>(
        void_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_cfg");
    auto *function =
        module->create_function<CoreIrFunction>("main", void_function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *trampoline =
        function->create_basic_block<CoreIrBasicBlock>("trampoline");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *entry_jump = entry->create_instruction<CoreIrJumpInst>(void_type, trampoline);
    trampoline->create_instruction<CoreIrJumpInst>(void_type, exit);
    exit->create_instruction<CoreIrReturnInst>(void_type);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(function->get_basic_blocks().size() == 2);
    assert(entry_jump->get_target_block() == exit);
}

void test_canonicalizes_zero_index_gep() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_gep");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *address = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr", slot);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *gep = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep0", address, std::vector<CoreIrValue *>{zero});
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", gep);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(load->get_address() == address);
    assert(entry->get_instructions().size() == 3);
}

} // namespace

int main() {
    test_preserves_already_canonical_ir();
    test_canonicalizes_branch_conditions();
    test_canonicalizes_integer_casts();
    test_removes_trampoline_blocks();
    test_canonicalizes_zero_index_gep();
    return 0;
}
