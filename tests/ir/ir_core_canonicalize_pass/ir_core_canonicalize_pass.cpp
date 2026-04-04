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

template <typename T>
T *find_instruction_by_name(CoreIrBasicBlock &block, const std::string &name) {
    for (const auto &instruction : block.get_instructions()) {
        if (instruction != nullptr && instruction->get_name() == name) {
            return dynamic_cast<T *>(instruction.get());
        }
    }
    return nullptr;
}

void assert_i1_compare(const CoreIrCompareInst *compare) {
    assert(compare != nullptr);
    assert(dynamic_cast<const CoreIrIntegerType *>(compare->get_type()) != nullptr);
    assert(static_cast<const CoreIrIntegerType *>(compare->get_type())
               ->get_bit_width() == 1);
}

void assert_instruction_parents(const CoreIrFunction &function) {
    for (const auto &block : function.get_basic_blocks()) {
        assert(block != nullptr);
        for (const auto &instruction : block->get_instructions()) {
            assert(instruction != nullptr);
            assert(instruction->get_parent() == block.get());
        }
    }
}

std::string canonicalize_and_print(std::unique_ptr<CoreIrContext> context,
                                   CoreIrModule *module) {
    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);
    CoreIrRawPrinter printer;
    return printer.print_module(*module);
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
    auto *f32_type = context->create_type<CoreIrFloatType>(CoreIrFloatKind::Float32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
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
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
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

    auto *i1_compare_function = module->create_function<CoreIrFunction>(
        "branch_from_i1_compare_wrappers", void_function_type, false);
    auto *i1_entry =
        i1_compare_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *i1_true =
        i1_compare_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *i1_false =
        i1_compare_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *compare_i1 = i1_entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp_i1", one, two);
    auto *zext_compare = i1_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::ZeroExtend, i32_type, "zext_cmp", compare_i1);
    auto *wrapped_ne = i1_entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::NotEqual, i32_type, "cmp_ne_zero",
        zext_compare, zero);
    auto *wrapped_ne_branch = i1_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, wrapped_ne, i1_true, i1_false);
    i1_true->create_instruction<CoreIrReturnInst>(void_type);
    i1_false->create_instruction<CoreIrReturnInst>(void_type);

    auto *eq_function = module->create_function<CoreIrFunction>(
        "branch_from_eq_wrapper", void_function_type, false);
    auto *eq_entry =
        eq_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *eq_true =
        eq_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *eq_false =
        eq_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *compare_i1_eq = eq_entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp_i1_eq", one, two);
    auto *sext_compare = eq_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::SignExtend, i32_type, "sext_cmp", compare_i1_eq);
    auto *wrapped_eq = eq_entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i32_type, "cmp_eq_zero", sext_compare,
        zero);
    auto *wrapped_eq_branch = eq_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, wrapped_eq, eq_true, eq_false);
    eq_true->create_instruction<CoreIrReturnInst>(void_type);
    eq_false->create_instruction<CoreIrReturnInst>(void_type);

    auto *pointer_function = module->create_function<CoreIrFunction>(
        "branch_from_pointer_value", void_function_type, false);
    auto *pointer_entry =
        pointer_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *pointer_true =
        pointer_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *pointer_false =
        pointer_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *slot =
        pointer_function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *pointer_value = pointer_entry->create_instruction<
        CoreIrAddressOfStackSlotInst>(ptr_i32_type, "addr", slot);
    auto *pointer_branch = pointer_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, pointer_value, pointer_true, pointer_false);
    pointer_true->create_instruction<CoreIrReturnInst>(void_type);
    pointer_false->create_instruction<CoreIrReturnInst>(void_type);

    auto *float_function = module->create_function<CoreIrFunction>(
        "branch_from_float_value", void_function_type, false);
    auto *float_entry =
        float_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *float_true =
        float_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *float_false =
        float_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *float_value =
        context->create_constant<CoreIrConstantFloat>(f32_type, "1.5");
    auto *float_branch = float_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, float_value, float_true, float_false);
    float_true->create_instruction<CoreIrReturnInst>(void_type);
    float_false->create_instruction<CoreIrReturnInst>(void_type);

    auto *not_i1_function = module->create_function<CoreIrFunction>(
        "branch_from_not_i1_wrapper", void_function_type, false);
    auto *not_i1_entry =
        not_i1_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *not_i1_true =
        not_i1_function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *not_i1_false =
        not_i1_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *compare_i1_not = not_i1_entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp_i1_not", one, two);
    auto *logical_not_i1 = not_i1_entry->create_instruction<CoreIrUnaryInst>(
        CoreIrUnaryOpcode::LogicalNot, i1_type, "not_i1_cmp", compare_i1_not);
    auto *zext_not_compare = not_i1_entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::ZeroExtend, i32_type, "zext_not_cmp", logical_not_i1);
    auto *not_i1_branch =
        not_i1_entry->create_instruction<CoreIrCondJumpInst>(
            void_type, zext_not_compare, not_i1_true, not_i1_false);
    not_i1_true->create_instruction<CoreIrReturnInst>(void_type);
    not_i1_false->create_instruction<CoreIrReturnInst>(void_type);

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
    assert(extend_compare->get_lhs() == extended_value);

    auto *trunc_compare =
        dynamic_cast<CoreIrCompareInst *>(trunc_branch->get_condition());
    assert_i1_compare(trunc_compare);
    assert(trunc_compare->get_predicate() ==
           CoreIrComparePredicate::NotEqual);
    assert(trunc_compare->get_lhs() == truncated_value);

    auto *wrapped_ne_compare =
        dynamic_cast<CoreIrCompareInst *>(wrapped_ne_branch->get_condition());
    assert_i1_compare(wrapped_ne_compare);
    assert(wrapped_ne_compare->get_predicate() ==
           CoreIrComparePredicate::SignedLess);

    auto *wrapped_eq_compare =
        dynamic_cast<CoreIrCompareInst *>(wrapped_eq_branch->get_condition());
    assert_i1_compare(wrapped_eq_compare);
    assert(wrapped_eq_compare->get_predicate() ==
           CoreIrComparePredicate::SignedGreaterEqual);

    auto *pointer_compare =
        dynamic_cast<CoreIrCompareInst *>(pointer_branch->get_condition());
    assert_i1_compare(pointer_compare);
    assert(pointer_compare->get_predicate() ==
           CoreIrComparePredicate::NotEqual);
    assert(pointer_compare->get_lhs() == pointer_value);
    assert(dynamic_cast<CoreIrConstantNull *>(pointer_compare->get_rhs()) != nullptr);

    auto *float_compare =
        dynamic_cast<CoreIrCompareInst *>(float_branch->get_condition());
    assert_i1_compare(float_compare);
    assert(float_compare->get_predicate() == CoreIrComparePredicate::NotEqual);
    assert(float_compare->get_lhs() == float_value);
    auto *float_zero =
        dynamic_cast<CoreIrConstantFloat *>(float_compare->get_rhs());
    assert(float_zero != nullptr);
    assert(float_zero->get_literal_text() == "0.0");

    auto *wrapped_not_compare =
        dynamic_cast<CoreIrCompareInst *>(not_i1_branch->get_condition());
    assert_i1_compare(wrapped_not_compare);
    assert(wrapped_not_compare->get_predicate() ==
           CoreIrComparePredicate::SignedGreaterEqual);
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

    assert(function->get_basic_blocks().size() == 1);
    assert(dynamic_cast<CoreIrReturnInst *>(entry->get_instructions().back().get()) !=
           nullptr);
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

    auto *canonical_load =
        dynamic_cast<CoreIrLoadInst *>(entry->get_instructions()[0].get());
    assert(canonical_load != nullptr);
    assert(canonical_load->get_stack_slot() == slot);
    assert(canonical_load->get_address() == nullptr);
    assert(entry->get_instructions().size() == 2);
}

void test_canonicalizes_nested_gep() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *struct_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, i32_type});
    auto *array_type = context->create_type<CoreIrArrayType>(struct_type, 2);
    auto *ptr_array_type = context->create_type<CoreIrPointerType>(array_type);
    auto *ptr_struct_type = context->create_type<CoreIrPointerType>(struct_type);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_nested_gep");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("value", array_type, 8);
    auto *address = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array_type, "addr", slot);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *inner = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_struct_type, "inner", address, std::vector<CoreIrValue *>{zero, one});
    auto *outer = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "outer", inner, std::vector<CoreIrValue *>{zero, one});
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", outer);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    auto *flattened =
        dynamic_cast<CoreIrGetElementPtrInst *>(load->get_address());
    assert(flattened != nullptr);
    assert(flattened->get_base() == address);
    assert(flattened->get_index_count() == 3);
    assert(flattened->get_index(0) == zero);
    assert(flattened->get_index(1) == one);
    assert(flattened->get_index(2) == one);
}

void test_canonicalizes_stackslot_zero_index_gep_access() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_stackslot_gep");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *forty_two =
        context->create_constant<CoreIrConstantInt>(i32_type, 42);
    auto *addr_for_store = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_store", slot);
    auto *gep_for_store = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep_store", addr_for_store,
        std::vector<CoreIrValue *>{zero});
    entry->create_instruction<CoreIrStoreInst>(void_type, forty_two, gep_for_store);
    auto *addr_for_load = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_load", slot);
    auto *gep_for_load = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep_load", addr_for_load,
        std::vector<CoreIrValue *>{zero});
    auto *load = entry->create_instruction<CoreIrLoadInst>(
        i32_type, "load", gep_for_load);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    auto *canonical_store =
        dynamic_cast<CoreIrStoreInst *>(entry->get_instructions()[0].get());
    auto *canonical_load =
        dynamic_cast<CoreIrLoadInst *>(entry->get_instructions()[1].get());
    assert(canonical_store != nullptr);
    assert(canonical_load != nullptr);
    assert(canonical_store->get_stack_slot() == slot);
    assert(canonical_store->get_address() == nullptr);
    assert(canonical_load->get_stack_slot() == slot);
    assert(canonical_load->get_address() == nullptr);
    assert(entry->get_instructions().size() == 3);
}

void test_canonicalizes_stackslot_zero_index_gep_chains() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_canonicalize_stackslot_gep_chain");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr", slot);
    auto *gep0 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep0", addr, std::vector<CoreIrValue *>{zero});
    auto *gep1 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep1", gep0, std::vector<CoreIrValue *>{zero});
    entry->create_instruction<CoreIrStoreInst>(void_type, seven, gep1);
    auto *load = entry->create_instruction<CoreIrLoadInst>(
        i32_type, "load", gep1);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    auto *canonical_store =
        dynamic_cast<CoreIrStoreInst *>(entry->get_instructions()[0].get());
    auto *canonical_load =
        dynamic_cast<CoreIrLoadInst *>(entry->get_instructions()[1].get());
    assert(canonical_store != nullptr);
    assert(canonical_load != nullptr);
    assert(canonical_store->get_stack_slot() == slot);
    assert(canonical_store->get_address() == nullptr);
    assert(canonical_load->get_stack_slot() == slot);
    assert(canonical_load->get_address() == nullptr);
    assert(entry->get_instructions().size() == 3);
}

void test_preserves_unsafe_nested_gep() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *struct_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, i32_type});
    auto *array_type = context->create_type<CoreIrArrayType>(struct_type, 4);
    auto *ptr_array_type = context->create_type<CoreIrPointerType>(array_type);
    auto *ptr_struct_type = context->create_type<CoreIrPointerType>(struct_type);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_unsafe_gep");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("value", array_type, 8);
    auto *address = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array_type, "addr", slot);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *inner = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_struct_type, "inner", address, std::vector<CoreIrValue *>{zero, one});
    auto *outer = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "outer", inner, std::vector<CoreIrValue *>{one});
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", outer);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(load->get_address() == outer);
    assert(outer->get_base() == inner);
}

void test_canonicalizes_multidim_nested_gep() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array3_i32 =
        context->create_type<CoreIrArrayType>(i32_type, 3);
    auto *array2_array3 =
        context->create_type<CoreIrArrayType>(array3_i32, 2);
    auto *ptr_outer = context->create_type<CoreIrPointerType>(array2_array3);
    auto *ptr_inner = context->create_type<CoreIrPointerType>(array3_i32);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_canonicalize_multidim_gep");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("value", array2_array3, 4);
    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_outer, "addr", slot);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *inner = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_inner, "inner", addr, std::vector<CoreIrValue *>{zero, one});
    auto *outer = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32, "outer", inner, std::vector<CoreIrValue *>{two});
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", outer);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    auto *flattened =
        dynamic_cast<CoreIrGetElementPtrInst *>(load->get_address());
    assert(flattened != nullptr);
    assert(flattened != inner);
    assert(flattened->get_base() == addr);
    assert(flattened->get_index_count() == 3);
    assert(flattened->get_index(0) == zero);
    assert(flattened->get_index(1) == one);
    assert(flattened->get_index(2) == two);
}

std::string build_stackslot_access_module(bool wrapped) {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("canon_stackslot_access_demo");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    CoreIrLoadInst *load = nullptr;
    if (wrapped) {
        auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
            ptr_i32_type, "addr", slot);
        auto *gep0 = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type, "gep0", addr, std::vector<CoreIrValue *>{zero});
        auto *gep1 = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type, "gep1", gep0, std::vector<CoreIrValue *>{zero});
        entry->create_instruction<CoreIrStoreInst>(void_type, seven, gep1);
        load = entry->create_instruction<CoreIrLoadInst>(i32_type, "load", gep1);
    } else {
        entry->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
        load = entry->create_instruction<CoreIrLoadInst>(i32_type, "load", slot);
    }
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    return canonicalize_and_print(std::move(context), module);
}

std::string build_global_access_module(bool wrapped) {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("canon_global_access_demo");
    auto *zero_init = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *global =
        module->create_global<CoreIrGlobal>("g_value", i32_type, zero_init, false, true);
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *addr = entry->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_i32_type, "addr", global);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    CoreIrValue *load_addr = addr;
    if (wrapped) {
        auto *gep0 = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type, "gep0", addr, std::vector<CoreIrValue *>{zero});
        load_addr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type, "gep1", gep0, std::vector<CoreIrValue *>{zero});
    }
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", load_addr);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    return canonicalize_and_print(std::move(context), module);
}

void test_equivalent_access_forms_print_identically() {
    const std::string wrapped_stackslot = build_stackslot_access_module(true);
    const std::string direct_stackslot = build_stackslot_access_module(false);
    assert(wrapped_stackslot == direct_stackslot);

    const std::string wrapped_global = build_global_access_module(true);
    const std::string direct_global = build_global_access_module(false);
    assert(wrapped_global == direct_global);
}

std::string build_stackslot_structural_address_module(bool nested) {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array3_i32 = context->create_type<CoreIrArrayType>(i32_type, 3);
    auto *struct_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{array3_i32});
    auto *array2_struct = context->create_type<CoreIrArrayType>(struct_type, 2);
    auto *ptr_outer = context->create_type<CoreIrPointerType>(array2_struct);
    auto *ptr_array = context->create_type<CoreIrPointerType>(array3_i32);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("canon_addr_demo");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("value", array2_struct, 4);
    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_outer, "addr", slot);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    CoreIrValue *element_ptr = nullptr;
    if (nested) {
        auto *struct_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            context->create_type<CoreIrPointerType>(struct_type), "step0", addr,
            std::vector<CoreIrValue *>{zero, one});
        auto *array_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_array, "step1", struct_ptr, std::vector<CoreIrValue *>{zero, zero});
        element_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "element", array_ptr, std::vector<CoreIrValue *>{two});
    } else {
        element_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "element", addr, std::vector<CoreIrValue *>{zero, one, zero, two});
    }
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", element_ptr);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    return canonicalize_and_print(std::move(context), module);
}

std::string build_global_structural_address_module(bool nested) {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array3_i32 = context->create_type<CoreIrArrayType>(i32_type, 3);
    auto *struct_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{array3_i32});
    auto *array2_struct = context->create_type<CoreIrArrayType>(struct_type, 2);
    auto *ptr_outer = context->create_type<CoreIrPointerType>(array2_struct);
    auto *ptr_array = context->create_type<CoreIrPointerType>(array3_i32);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("canon_addr_demo");
    auto *zero_init = context->create_constant<CoreIrConstantAggregate>(
        array2_struct,
        std::vector<const CoreIrConstant *>{
            context->create_constant<CoreIrConstantAggregate>(
                struct_type,
                std::vector<const CoreIrConstant *>{context->create_constant<
                    CoreIrConstantAggregate>(array3_i32,
                                            std::vector<const CoreIrConstant *>{
                                                context->create_constant<CoreIrConstantInt>(
                                                    i32_type, 0),
                                                context->create_constant<CoreIrConstantInt>(
                                                    i32_type, 0),
                                                context->create_constant<CoreIrConstantInt>(
                                                    i32_type, 0)})}),
            context->create_constant<CoreIrConstantAggregate>(
                struct_type,
                std::vector<const CoreIrConstant *>{context->create_constant<
                    CoreIrConstantAggregate>(array3_i32,
                                            std::vector<const CoreIrConstant *>{
                                                context->create_constant<CoreIrConstantInt>(
                                                    i32_type, 0),
                                                context->create_constant<CoreIrConstantInt>(
                                                    i32_type, 0),
                                                context->create_constant<CoreIrConstantInt>(
                                                    i32_type, 0)})})});
    auto *global =
        module->create_global<CoreIrGlobal>("g_values", array2_struct, zero_init, false, true);
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *addr = entry->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_outer, "addr", global);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    CoreIrValue *element_ptr = nullptr;
    if (nested) {
        auto *struct_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            context->create_type<CoreIrPointerType>(struct_type), "step0", addr,
            std::vector<CoreIrValue *>{zero, one});
        auto *array_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_array, "step1", struct_ptr, std::vector<CoreIrValue *>{zero, zero});
        element_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "element", array_ptr, std::vector<CoreIrValue *>{two});
    } else {
        element_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "element", addr, std::vector<CoreIrValue *>{zero, one, zero, two});
    }
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", element_ptr);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    return canonicalize_and_print(std::move(context), module);
}

void test_equivalent_address_forms_print_identically() {
    const std::string nested_stackslot =
        build_stackslot_structural_address_module(true);
    const std::string direct_stackslot =
        build_stackslot_structural_address_module(false);
    assert(nested_stackslot == direct_stackslot);

    const std::string nested_global =
        build_global_structural_address_module(true);
    const std::string direct_global =
        build_global_structural_address_module(false);
    assert(nested_global == direct_global);
}

std::string build_deep_stackslot_address_module(bool nested) {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *leaf_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, i32_type});
    auto *leaf_array_type = context->create_type<CoreIrArrayType>(leaf_type, 3);
    auto *inner_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, leaf_array_type});
    auto *outer_type = context->create_type<CoreIrArrayType>(inner_type, 2);
    auto *ptr_outer = context->create_type<CoreIrPointerType>(outer_type);
    auto *ptr_inner = context->create_type<CoreIrPointerType>(inner_type);
    auto *ptr_leaf_array =
        context->create_type<CoreIrPointerType>(leaf_array_type);
    auto *ptr_leaf = context->create_type<CoreIrPointerType>(leaf_type);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("canon_deep_stackslot_demo");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", outer_type, 4);
    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_outer, "addr", slot);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    CoreIrValue *element_ptr = nullptr;
    if (nested) {
        auto *inner_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_inner, "inner_ptr", addr, std::vector<CoreIrValue *>{zero, one});
        auto *leaf_array_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_leaf_array, "leaf_array_ptr", inner_ptr,
            std::vector<CoreIrValue *>{zero, one});
        auto *leaf_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_leaf, "leaf_ptr", leaf_array_ptr,
            std::vector<CoreIrValue *>{zero, two});
        element_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "element_ptr", leaf_ptr,
            std::vector<CoreIrValue *>{zero, one});
    } else {
        element_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "element_ptr", addr,
            std::vector<CoreIrValue *>{zero, one, one, two, one});
    }
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", element_ptr);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    return canonicalize_and_print(std::move(context), module);
}

std::string build_deep_global_address_module(bool nested) {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *leaf_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, i32_type});
    auto *leaf_array_type = context->create_type<CoreIrArrayType>(leaf_type, 2);
    auto *outer_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, leaf_array_type});
    auto *ptr_outer = context->create_type<CoreIrPointerType>(outer_type);
    auto *ptr_leaf_array =
        context->create_type<CoreIrPointerType>(leaf_array_type);
    auto *ptr_leaf = context->create_type<CoreIrPointerType>(leaf_type);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("canon_deep_global_demo");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *zero_leaf = context->create_constant<CoreIrConstantAggregate>(
        leaf_type, std::vector<const CoreIrConstant *>{zero, zero});
    auto *zero_leaf_array = context->create_constant<CoreIrConstantAggregate>(
        leaf_array_type,
        std::vector<const CoreIrConstant *>{zero_leaf, zero_leaf});
    auto *zero_init = context->create_constant<CoreIrConstantAggregate>(
        outer_type, std::vector<const CoreIrConstant *>{zero, zero_leaf_array});
    auto *global =
        module->create_global<CoreIrGlobal>("g_value", outer_type, zero_init, false, true);
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *addr = entry->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_outer, "addr", global);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);

    CoreIrValue *element_ptr = nullptr;
    if (nested) {
        auto *leaf_array_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_leaf_array, "leaf_array_ptr", addr,
            std::vector<CoreIrValue *>{zero, one});
        auto *leaf_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_leaf, "leaf_ptr", leaf_array_ptr,
            std::vector<CoreIrValue *>{zero, one});
        element_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "element_ptr", leaf_ptr,
            std::vector<CoreIrValue *>{zero, one});
    } else {
        element_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32, "element_ptr", addr,
            std::vector<CoreIrValue *>{zero, one, one, one});
    }
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", element_ptr);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    return canonicalize_and_print(std::move(context), module);
}

void test_deeper_mixed_address_forms_print_identically() {
    const std::string nested_stackslot =
        build_deep_stackslot_address_module(true);
    const std::string direct_stackslot =
        build_deep_stackslot_address_module(false);
    assert(nested_stackslot == direct_stackslot);

    const std::string nested_global =
        build_deep_global_address_module(true);
    const std::string direct_global =
        build_deep_global_address_module(false);
    assert(nested_global == direct_global);
}

void test_canonicalizes_struct_array_member_gep_chain() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array3_i32 =
        context->create_type<CoreIrArrayType>(i32_type, 3);
    auto *struct_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{array3_i32});
    auto *array2_struct =
        context->create_type<CoreIrArrayType>(struct_type, 2);
    auto *ptr_outer = context->create_type<CoreIrPointerType>(array2_struct);
    auto *ptr_struct = context->create_type<CoreIrPointerType>(struct_type);
    auto *ptr_array = context->create_type<CoreIrPointerType>(array3_i32);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_canonicalize_struct_array_member_gep");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("value", array2_struct, 4);
    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_outer, "addr", slot);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *struct_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_struct, "struct_ptr", addr, std::vector<CoreIrValue *>{zero, one});
    auto *array_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_array, "array_ptr", struct_ptr,
        std::vector<CoreIrValue *>{zero, zero});
    auto *element_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32, "element_ptr", array_ptr, std::vector<CoreIrValue *>{zero, two});
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", element_ptr);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    auto *flattened =
        dynamic_cast<CoreIrGetElementPtrInst *>(load->get_address());
    assert(flattened != nullptr);
    assert(flattened->get_base() == addr);
    assert(flattened->get_index_count() == 4);
    assert(flattened->get_index(0) == zero);
    assert(flattened->get_index(1) == one);
    assert(flattened->get_index(2) == zero);
    assert(flattened->get_index(3) == two);
}

void test_preserves_pointer_arithmetic_after_array_decay() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array2_i32 = context->create_type<CoreIrArrayType>(i32_type, 2);
    auto *ptr_array = context->create_type<CoreIrPointerType>(array2_i32);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_canonicalize_array_decay_pointer_arith");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *index =
        function->create_parameter<CoreIrParameter>(i32_type, "idx");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", array2_i32, 4);
    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array, "addr", slot);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *decayed = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32, "decayed", addr, std::vector<CoreIrValue *>{zero, one});
    auto *offset = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32, "offset", decayed, std::vector<CoreIrValue *>{index});
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", offset);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(load->get_address() == offset);
    assert(offset->get_base() == decayed);
}

void test_preserves_dynamic_struct_pointer_arithmetic() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *struct_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, i32_type});
    auto *array_type = context->create_type<CoreIrArrayType>(struct_type, 2);
    auto *ptr_array = context->create_type<CoreIrPointerType>(array_type);
    auto *ptr_struct = context->create_type<CoreIrPointerType>(struct_type);
    auto *ptr_i32 = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_canonicalize_dynamic_struct_pointer_arith");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *index =
        function->create_parameter<CoreIrParameter>(i32_type, "idx");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", array_type, 8);
    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array, "addr", slot);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *struct_ptr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_struct, "struct_ptr", addr, std::vector<CoreIrValue *>{zero, one});
    auto *offset = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32, "offset", struct_ptr, std::vector<CoreIrValue *>{index});
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", offset);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(load->get_address() == offset);
    assert(offset->get_base() == struct_ptr);
}

void test_cfg_second_stage_rules() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *void_function_type = context->create_type<CoreIrFunctionType>(
        void_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_cfg_phase2");

    auto *same_target_function = module->create_function<CoreIrFunction>(
        "same_target_cond", void_function_type, false);
    auto *entry =
        same_target_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *target =
        same_target_function->create_basic_block<CoreIrBasicBlock>("target");
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, one, target, target);
    target->create_instruction<CoreIrReturnInst>(void_type);

    auto *trampoline_chain_function = module->create_function<CoreIrFunction>(
        "trampoline_chain", void_function_type, false);
    auto *chain_entry =
        trampoline_chain_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *cond_target = trampoline_chain_function->create_basic_block<CoreIrBasicBlock>(
        "cond_target");
    auto *trampoline_a =
        trampoline_chain_function->create_basic_block<CoreIrBasicBlock>("trampoline_a");
    auto *trampoline_b =
        trampoline_chain_function->create_basic_block<CoreIrBasicBlock>("trampoline_b");
    auto *chain_exit =
        trampoline_chain_function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *chain_cond = context->create_constant<CoreIrConstantInt>(i1_type, 0);
    auto *chain_branch = chain_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, chain_cond, cond_target, trampoline_a);
    cond_target->create_instruction<CoreIrJumpInst>(void_type, trampoline_b);
    trampoline_a->create_instruction<CoreIrJumpInst>(void_type, trampoline_b);
    trampoline_b->create_instruction<CoreIrJumpInst>(void_type, chain_exit);
    chain_exit->create_instruction<CoreIrReturnInst>(void_type);

    auto *linear_function = module->create_function<CoreIrFunction>(
        "linear_merge", void_function_type, false);
    auto *linear_entry =
        linear_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *middle =
        linear_function->create_basic_block<CoreIrBasicBlock>("middle");
    auto *zero32 = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    linear_entry->create_instruction<CoreIrJumpInst>(void_type, middle);
    middle->create_instruction<CoreIrReturnInst>(void_type, zero32);

    auto *constant_cond_function = module->create_function<CoreIrFunction>(
        "constant_cond", void_function_type, false);
    auto *constant_entry =
        constant_cond_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *constant_true = constant_cond_function->create_basic_block<CoreIrBasicBlock>(
        "true_block");
    auto *constant_false =
        constant_cond_function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *true_value = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *false_value = context->create_constant<CoreIrConstantInt>(i32_type, 9);
    auto *zero1 = context->create_constant<CoreIrConstantInt>(i1_type, 0);
    constant_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, zero1, constant_true, constant_false);
    constant_true->create_instruction<CoreIrReturnInst>(void_type, true_value);
    constant_false->create_instruction<CoreIrReturnInst>(void_type, false_value);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(dynamic_cast<CoreIrJumpInst *>(entry->get_instructions().back().get()) ==
           nullptr);
    assert(dynamic_cast<CoreIrReturnInst *>(entry->get_instructions().back().get()) !=
           nullptr);
    assert(trampoline_chain_function->get_basic_blocks().size() == 1);
    auto *chain_entry_terminator =
        trampoline_chain_function->get_basic_blocks().front()->get_instructions().back().get();
    assert(dynamic_cast<CoreIrReturnInst *>(chain_entry_terminator) != nullptr);
    assert(linear_function->get_basic_blocks().size() == 1);
    assert(constant_cond_function->get_basic_blocks().size() == 1);
    auto *constant_return = dynamic_cast<CoreIrReturnInst *>(
        constant_cond_function->get_basic_blocks().front()->get_instructions().back().get());
    assert(constant_return != nullptr);
    auto *constant_result =
        dynamic_cast<CoreIrConstantInt *>(constant_return->get_return_value());
    assert(constant_result != nullptr);
    assert(constant_result->get_value() == 9);
}

void test_cfg_reaches_fixed_point_and_preserves_parents() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_canonicalize_cfg_fixed_point");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *dead = function->create_basic_block<CoreIrBasicBlock>("dead");
    auto *live = function->create_basic_block<CoreIrBasicBlock>("live");
    auto *trampoline =
        function->create_basic_block<CoreIrBasicBlock>("trampoline");
    auto *sink = function->create_basic_block<CoreIrBasicBlock>("sink");
    auto *orphan = function->create_basic_block<CoreIrBasicBlock>("orphan");
    auto *zero = context->create_constant<CoreIrConstantInt>(i1_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *nine = context->create_constant<CoreIrConstantInt>(i32_type, 9);

    entry->create_instruction<CoreIrCondJumpInst>(void_type, zero, dead, live);
    dead->create_instruction<CoreIrJumpInst>(void_type, trampoline);
    live->create_instruction<CoreIrCondJumpInst>(void_type, one, sink, sink);
    trampoline->create_instruction<CoreIrJumpInst>(void_type, sink);
    sink->create_instruction<CoreIrReturnInst>(void_type, seven);
    orphan->create_instruction<CoreIrReturnInst>(void_type, nine);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    CoreIrRawPrinter printer;
    const std::string after_first = printer.print_module(*module);
    assert(function->get_basic_blocks().size() == 1);
    assert_instruction_parents(*function);
    auto *return_inst = dynamic_cast<CoreIrReturnInst *>(
        function->get_basic_blocks().front()->get_instructions().back().get());
    assert(return_inst != nullptr);
    auto *return_value =
        dynamic_cast<CoreIrConstantInt *>(return_inst->get_return_value());
    assert(return_value != nullptr);
    assert(return_value->get_value() == 7);

    run_pass(compiler_context);

    const std::string after_second = printer.print_module(*module);
    assert(after_first == after_second);
    assert_instruction_parents(*function);
}

void test_canonicalizes_integer_boolean_expressions() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_exprs");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *parameter =
        function->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *add_zero = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "add_zero", parameter, zero);
    auto *mul_one = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i32_type, "mul_one", add_zero, one);
    auto *cmp = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i32_type, "cmp", one, parameter);
    auto *cmp_i1 = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, context->create_type<CoreIrIntegerType>(1),
        "cmp_i1", parameter, one);
    auto *zext_cmp = entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::ZeroExtend, i32_type, "zext_cmp", cmp_i1);
    auto *cmp_bool = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i32_type, "cmp_bool", zext_cmp, zero);
    auto *result = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "result", mul_one, cmp_bool);
    entry->create_instruction<CoreIrReturnInst>(void_type, result);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(result->get_lhs() == parameter);
    assert(cmp->get_lhs() == parameter);
    assert(cmp->get_rhs() == one);
    assert(cmp->get_predicate() == CoreIrComparePredicate::SignedGreater);
    auto *canonical_cmp_bool =
        find_instruction_by_name<CoreIrCompareInst>(*entry, "cmp_bool");
    assert(canonical_cmp_bool != nullptr);
    assert(canonical_cmp_bool->get_lhs() == parameter);
    assert(canonical_cmp_bool->get_rhs() == one);
    assert(canonical_cmp_bool->get_predicate() ==
           CoreIrComparePredicate::SignedGreaterEqual);
}

void test_canonicalizes_expression_identities() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_expr_identity");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *parameter =
        function->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *sub_zero = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, i32_type, "sub_zero", parameter, zero);
    auto *div_one = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::SDiv, i32_type, "div_one", sub_zero, one);
    auto *xor_zero = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Xor, i32_type, "xor_zero", div_one, zero);
    entry->create_instruction<CoreIrReturnInst>(void_type, xor_zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    auto *return_inst =
        dynamic_cast<CoreIrReturnInst *>(entry->get_instructions().back().get());
    assert(return_inst != nullptr);
    assert(return_inst->get_return_value() == parameter);
    assert(entry->get_instructions().size() == 1);
}

void test_canonicalizes_commutative_and_self_expressions() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_canonicalize_expr_commutative");

    auto *reorder_function = module->create_function<CoreIrFunction>(
        "reorder_constants", function_type, false);
    auto *reorder_param =
        reorder_function->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *reorder_entry =
        reorder_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *five = context->create_constant<CoreIrConstantInt>(i32_type, 5);
    auto *add = reorder_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "add", five, reorder_param);
    reorder_entry->create_instruction<CoreIrReturnInst>(void_type, add);

    auto *self_function = module->create_function<CoreIrFunction>(
        "self_ops", function_type, false);
    auto *self_param =
        self_function->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *self_entry =
        self_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *self_sub = self_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, i32_type, "self_sub", self_param, self_param);
    auto *self_xor = self_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Xor, i32_type, "self_xor", self_sub, self_sub);
    self_entry->create_instruction<CoreIrReturnInst>(void_type, self_xor);

    auto *all_ones_function = module->create_function<CoreIrFunction>(
        "all_ones", function_type, false);
    auto *all_ones_param =
        all_ones_function->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *all_ones_entry =
        all_ones_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero =
        context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *all_ones =
        context->create_constant<CoreIrConstantInt>(i32_type, 0xffffffffu);
    auto *and_all_ones = all_ones_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::And, i32_type, "and_all_ones", all_ones,
        all_ones_param);
    auto *shift_zero = all_ones_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::LShr, i32_type, "shift_zero", and_all_ones, zero);
    auto *mul_zero = all_ones_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i32_type, "mul_zero", shift_zero, zero);
    auto *or_all_ones = all_ones_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Or, i32_type, "or_all_ones", mul_zero, all_ones);
    all_ones_entry->create_instruction<CoreIrReturnInst>(void_type, or_all_ones);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(add->get_lhs() == reorder_param);
    assert(add->get_rhs() == five);

    auto *self_return =
        dynamic_cast<CoreIrReturnInst *>(self_entry->get_instructions().back().get());
    assert(self_return != nullptr);
    auto *self_zero =
        dynamic_cast<CoreIrConstantInt *>(self_return->get_return_value());
    assert(self_zero != nullptr);
    assert(self_zero->get_value() == 0);

    auto *all_ones_return = dynamic_cast<CoreIrReturnInst *>(
        all_ones_entry->get_instructions().back().get());
    assert(all_ones_return != nullptr);
    auto *all_ones_value =
        dynamic_cast<CoreIrConstantInt *>(all_ones_return->get_return_value());
    assert(all_ones_value != nullptr);
    assert(all_ones_value->get_value() == 0xffffffffu);
}

void test_canonicalizes_general_compare_orientation() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_canonicalize_compare_orientation");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *parameter =
        function->create_parameter<CoreIrParameter>(ptr_i32_type, "ptr");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *null_value =
        context->create_constant<CoreIrConstantNull>(ptr_i32_type);
    auto *compare = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i32_type, "cmp", null_value, parameter);
    entry->create_instruction<CoreIrReturnInst>(void_type, compare);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    assert(compare->get_lhs() == parameter);
    assert(compare->get_rhs() == null_value);
    assert(compare->get_predicate() == CoreIrComparePredicate::Equal);
}

void test_preserves_float_self_subtraction() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *f32_type =
        context->create_type<CoreIrFloatType>(CoreIrFloatKind::Float32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        f32_type, std::vector<const CoreIrType *>{f32_type}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_canonicalize_float_self_sub");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *parameter =
        function->create_parameter<CoreIrParameter>(f32_type, "x");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *self_sub = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Sub, f32_type, "self_sub", parameter, parameter);
    entry->create_instruction<CoreIrReturnInst>(void_type, self_sub);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    auto *float_return =
        dynamic_cast<CoreIrReturnInst *>(entry->get_instructions().back().get());
    assert(float_return != nullptr);
    assert(float_return->get_return_value() == self_sub);
    assert(dynamic_cast<CoreIrConstantInt *>(float_return->get_return_value()) ==
           nullptr);
}

void test_canonicalizes_stackslot_access() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_stackslot");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *addr_for_store = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_store", slot);
    auto *forty_two =
        context->create_constant<CoreIrConstantInt>(i32_type, 42);
    auto *store = entry->create_instruction<CoreIrStoreInst>(
        void_type, forty_two, addr_for_store);
    auto *addr_for_load = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_load", slot);
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", addr_for_load);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    run_pass(compiler_context);

    auto *canonical_store =
        dynamic_cast<CoreIrStoreInst *>(entry->get_instructions()[0].get());
    auto *canonical_load =
        dynamic_cast<CoreIrLoadInst *>(entry->get_instructions()[1].get());
    assert(canonical_store != nullptr);
    assert(canonical_load != nullptr);
    assert(canonical_store->get_stack_slot() == slot);
    assert(canonical_store->get_address() == nullptr);
    assert(canonical_load->get_stack_slot() == slot);
    assert(canonical_load->get_address() == nullptr);
}

} // namespace

int main() {
    test_preserves_already_canonical_ir();
    test_canonicalizes_branch_conditions();
    test_canonicalizes_integer_casts();
    test_removes_trampoline_blocks();
    test_canonicalizes_zero_index_gep();
    test_canonicalizes_nested_gep();
    test_canonicalizes_stackslot_zero_index_gep_access();
    test_canonicalizes_stackslot_zero_index_gep_chains();
    test_equivalent_access_forms_print_identically();
    test_preserves_unsafe_nested_gep();
    test_preserves_pointer_arithmetic_after_array_decay();
    test_preserves_dynamic_struct_pointer_arithmetic();
    test_canonicalizes_multidim_nested_gep();
    test_canonicalizes_struct_array_member_gep_chain();
    test_equivalent_address_forms_print_identically();
    test_deeper_mixed_address_forms_print_identically();
    test_cfg_second_stage_rules();
    test_cfg_reaches_fixed_point_and_preserves_parents();
    test_canonicalizes_integer_boolean_expressions();
    test_canonicalizes_expression_identities();
    test_canonicalizes_commutative_and_self_expressions();
    test_canonicalizes_general_compare_orientation();
    test_preserves_float_self_subtraction();
    test_canonicalizes_stackslot_access();
    return 0;
}
