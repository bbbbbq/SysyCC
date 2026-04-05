#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

void test_folds_phi_and_identity_cast() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_instcombine_phi_cast");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *merge = function->create_basic_block<CoreIrBasicBlock>("merge");
    auto *five = context->create_constant<CoreIrConstantInt>(i32_type, 5);

    entry->create_instruction<CoreIrJumpInst>(void_type, merge);
    auto *phi = merge->create_instruction<CoreIrPhiInst>(i32_type, "merged");
    phi->add_incoming(entry, five);
    auto *identity_cast = merge->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::ZeroExtend, i32_type, "identity", phi);
    merge->create_instruction<CoreIrReturnInst>(void_type, identity_cast);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrInstCombinePass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("phi i32") == std::string::npos);
    assert(text.find("%identity =") == std::string::npos);
    assert(text.find("ret i32 5") != std::string::npos);
}

void test_folds_binary_and_compare_rules() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_instcombine_values");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i1_type, 1);

    auto *sum = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", two, three);
    auto *xor_self = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Xor, i32_type, "xor_self", sum, sum);
    auto *cmp = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cmp_same", xor_self, xor_self);
    auto *wrapped = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::NotEqual, i1_type, "wrapped", cmp, zero);
    entry->create_instruction<CoreIrReturnInst>(void_type, wrapped);
    (void)one;

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrInstCombinePass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%sum =") == std::string::npos);
    assert(text.find("%xor_self =") == std::string::npos);
    assert(text.find("%cmp_same =") == std::string::npos);
    assert(text.find("%wrapped =") == std::string::npos);
    assert(text.find("ret i1 1") != std::string::npos);
}

void test_canonicalizes_condjump_and_memory_shapes() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_instcombine_branch_memory");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *true_block = function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *false_block =
        function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr", slot);
    auto *gep = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep0", addr, std::vector<CoreIrValue *>{zero});
    entry->create_instruction<CoreIrStoreInst>(void_type, seven, gep);
    auto *load =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "load", gep);
    auto *cmp = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cmp", load, seven);
    auto *cmp_wrapped = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::NotEqual, i1_type, "cmp_wrap", cmp, zero);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, cmp_wrapped, true_block,
                                                  false_block);
    true_block->create_instruction<CoreIrReturnInst>(void_type, seven);
    false_block->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrInstCombinePass pass;
    assert(pass.Run(compiler_context).ok);

    auto *store = dynamic_cast<CoreIrStoreInst *>(entry->get_instructions()[0].get());
    auto *canonical_load =
        dynamic_cast<CoreIrLoadInst *>(entry->get_instructions()[1].get());
    auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
        entry->get_instructions().back().get());
    assert(store != nullptr);
    assert(canonical_load != nullptr);
    assert(branch != nullptr);
    assert(store->get_stack_slot() == slot);
    assert(store->get_address() == nullptr);
    assert(canonical_load->get_stack_slot() == slot);
    assert(canonical_load->get_address() == nullptr);
    assert(dynamic_cast<CoreIrCompareInst *>(branch->get_condition()) != nullptr);
}

void test_flattens_safe_nested_gep() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array_type = context->create_type<CoreIrArrayType>(i32_type, 4);
    auto *ptr_array_type = context->create_type<CoreIrPointerType>(array_type);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_instcombine_gep");
    auto *global = module->create_global<CoreIrGlobal>(
        "arr", array_type,
        context->create_constant<CoreIrConstantZeroInitializer>(array_type),
        false, false);
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);

    auto *addr = entry->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_array_type, "addr", global);
    auto *gep0 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep0", addr, std::vector<CoreIrValue *>{zero, one});
    auto *gep1 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep1", gep0, std::vector<CoreIrValue *>{zero});
    auto *load = entry->create_instruction<CoreIrLoadInst>(i32_type, "load", gep1);
    entry->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrInstCombinePass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%gep1 =") == std::string::npos);
    assert(text.find("%gep0 = gep") != std::string::npos);
}

void test_does_not_rewrite_store_call_or_terminator() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *callee_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_instcombine_side_effects");
    module->create_function<CoreIrFunction>("callee", callee_type, false);
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *true_block = function->create_basic_block<CoreIrBasicBlock>("true_block");
    auto *false_block =
        function->create_basic_block<CoreIrBasicBlock>("false_block");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *true_const = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr", slot);
    auto *call = entry->create_instruction<CoreIrCallInst>(
        i32_type, "call", "callee", callee_type, std::vector<CoreIrValue *>{addr});
    auto *store =
        entry->create_instruction<CoreIrStoreInst>(void_type, one, slot);
    auto *branch = entry->create_instruction<CoreIrCondJumpInst>(
        void_type, true_const, true_block, false_block);
    true_block->create_instruction<CoreIrReturnInst>(void_type, call);
    false_block->create_instruction<CoreIrReturnInst>(void_type, one);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrInstCombinePass pass;
    assert(pass.Run(compiler_context).ok);

    assert(call->get_parent() == entry);
    assert(store->get_parent() == entry);
    assert(branch->get_parent() == entry);
}

} // namespace

int main() {
    test_folds_phi_and_identity_cast();
    test_folds_binary_and_compare_rules();
    test_canonicalizes_condjump_and_memory_shapes();
    test_flattens_safe_nested_gep();
    test_does_not_rewrite_store_call_or_terminator();
    return 0;
}
