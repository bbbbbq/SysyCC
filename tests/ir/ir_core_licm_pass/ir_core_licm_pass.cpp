#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>

#include "backend/ir/licm/core_ir_licm_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
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

void test_hoists_invariant_binary_but_not_phi_or_variant_user() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_binary");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *sum = header->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", one, two);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    auto *variant = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "variant", sum, iv);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, variant);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(sum->get_parent() == entry);
    assert(variant->get_parent() == body);
    assert(iv->get_parent() == header);
}

void test_hoists_load_without_loop_write() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_load_safe");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    entry->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *load = header->create_instruction<CoreIrLoadInst>(i32_type, "load", slot);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(load->get_parent() == entry);
}

void test_does_not_hoist_argument_load_across_other_argument_store() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type,
        std::vector<const CoreIrType *>{ptr_i32_type, ptr_i32_type, i32_type},
        false);
    auto *module = context->create_module<CoreIrModule>("licm_arg_roots");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *src = function->create_parameter<CoreIrParameter>(ptr_i32_type, "src");
    auto *dst = function->create_parameter<CoreIrParameter>(ptr_i32_type, "dst");
    auto *k = function->create_parameter<CoreIrParameter>(i32_type, "k");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *four = context->create_constant<CoreIrConstantInt>(i32_type, 4);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, four);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *src_ptr = body->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "src.ptr", src, std::vector<CoreIrValue *>{k});
    auto *src_load =
        body->create_instruction<CoreIrLoadInst>(i32_type, "src.load", src_ptr);
    auto *dst_ptr = body->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "dst.ptr", dst, std::vector<CoreIrValue *>{iv});
    body->create_instruction<CoreIrStoreInst>(void_type, src_load, dst_ptr);
    auto *next_iv = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "iv.next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next_iv);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(src_ptr->get_parent() == entry);
    assert(src_load->get_parent() == body);
    assert(dst_ptr->get_parent() == body);
}

void test_does_not_hoist_must_alias_load() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_load_must_alias");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *nine = context->create_constant<CoreIrConstantInt>(i32_type, 9);

    entry->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *load = header->create_instruction<CoreIrLoadInst>(i32_type, "load", slot);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    body->create_instruction<CoreIrStoreInst>(void_type, nine, slot);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(load->get_parent() == header);
}

void test_does_not_hoist_may_alias_load() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array_type = context->create_type<CoreIrArrayType>(i32_type, 2);
    auto *ptr_array_type = context->create_type<CoreIrPointerType>(array_type);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_load_may_alias");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("array", array_type, 8);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *zero_array =
        context->create_constant<CoreIrConstantZeroInitializer>(array_type);

    entry->create_instruction<CoreIrStoreInst>(void_type, zero_array, slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *whole_load =
        header->create_instruction<CoreIrLoadInst>(array_type, "whole", slot);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    auto *addr = body->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array_type, "addr", slot);
    auto *field_ptr = body->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "field.ptr", addr, std::vector<CoreIrValue *>{one});
    body->create_instruction<CoreIrStoreInst>(void_type, one, field_ptr);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(whole_load->get_parent() == header);
}

void test_does_not_hoist_dynamic_index_argument_load_without_loop_write() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type, i32_type}, false);
    auto *module = context->create_module<CoreIrModule>("licm_load_unknown");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *ptr = function->create_parameter<CoreIrParameter>(ptr_i32_type, "ptr");
    auto *idx = function->create_parameter<CoreIrParameter>(i32_type, "idx");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *gep = header->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "elem.ptr", ptr, std::vector<CoreIrValue *>{idx});
    auto *load = header->create_instruction<CoreIrLoadInst>(i32_type, "load", gep);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(gep->get_parent() == entry);
    assert(load->get_parent() == header);
}

void test_does_not_hoist_non_must_execute_body_instruction() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_non_must_execute");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    auto *sum = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", one, two);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(sum->get_parent() == body);
}

void test_hoists_single_use_invariant_with_constant_trip_count() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_trip_count");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *ten = context->create_constant<CoreIrConstantInt>(i32_type, 10);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *single_use = header->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "single_use", one, one);
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, ten);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "next", iv, one);
    body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "consume", single_use, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(single_use->get_parent() == entry);
}

void test_hoists_from_inner_loop_to_outer_preheader() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_nested");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *outer_header = function->create_basic_block<CoreIrBasicBlock>("outer.header");
    auto *inner_preheader =
        function->create_basic_block<CoreIrBasicBlock>("inner.preheader");
    auto *inner_header =
        function->create_basic_block<CoreIrBasicBlock>("inner.header");
    auto *inner_body = function->create_basic_block<CoreIrBasicBlock>("inner.body");
    auto *outer_latch =
        function->create_basic_block<CoreIrBasicBlock>("outer.latch");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    entry->create_instruction<CoreIrJumpInst>(void_type, outer_header);
    outer_header->create_instruction<CoreIrJumpInst>(void_type, inner_preheader);
    inner_preheader->create_instruction<CoreIrJumpInst>(void_type, inner_header);
    auto *sum = inner_header->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", one, two);
    auto *doubled = inner_header->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i32_type, "doubled", sum, two);
    inner_header->create_instruction<CoreIrCondJumpInst>(void_type, cond, inner_body,
                                                         outer_latch);
    inner_body->create_instruction<CoreIrJumpInst>(void_type, inner_header);
    outer_latch->create_instruction<CoreIrCondJumpInst>(void_type, cond, outer_header,
                                                        exit);
    exit->create_instruction<CoreIrReturnInst>(void_type, doubled);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(sum->get_parent() == entry);
    assert(doubled->get_parent() == inner_header);
}

void test_hoists_load_backed_by_single_use_addrof_chain() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("licm_addrof_profitability");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    entry->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *addr = header->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr", slot);
    auto *load =
        header->create_instruction<CoreIrLoadInst>(i32_type, "load", addr);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(addr->get_parent() == entry);
    assert(load->get_parent() == entry);
}

void test_hoists_redundant_invariant_store() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_store_safe");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    entry->create_instruction<CoreIrStoreInst>(void_type, zero, slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *store = header->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    auto *load = body->create_instruction<CoreIrLoadInst>(i32_type, "load", slot);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, load);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(!text.empty());
}

void test_does_not_hoist_store_when_read_precedes_it() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_store_unsafe");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    entry->create_instruction<CoreIrStoreInst>(void_type, zero, slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *header_load =
        header->create_instruction<CoreIrLoadInst>(i32_type, "header.load", slot);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    auto *store = body->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, header_load);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(store->get_parent() == body);
}

void test_hoists_speculatively_safe_stackslot_load() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_speculative_load");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    entry->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    body->create_instruction<CoreIrCondJumpInst>(void_type, cond, then_block, header);
    auto *load =
        then_block->create_instruction<CoreIrLoadInst>(i32_type, "load", slot);
    then_block->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(load->get_parent() == entry);
}

void test_hoists_invariant_store_past_unknown_later_read() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array_type = context->create_type<CoreIrArrayType>(i32_type, 4);
    auto *ptr_array_type = context->create_type<CoreIrPointerType>(array_type);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("licm_store_unknown_read");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("array", array_type, 4);
    auto *index_slot =
        function->create_stack_slot<CoreIrStackSlot>("idx", i32_type, 4);
    auto *cond = context->create_constant<CoreIrConstantInt>(i1_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    entry->create_instruction<CoreIrStoreInst>(void_type, zero, index_slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *base = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array_type, "base", slot);
    auto *field0 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "field0", base, std::vector<CoreIrValue *>{zero, zero});
    auto *store = header->create_instruction<CoreIrStoreInst>(void_type, seven, field0);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cond, body, exit);
    auto *idx_load =
        body->create_instruction<CoreIrLoadInst>(i32_type, "idx.load", index_slot);
    auto *unknown_addr = body->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "unknown.addr", base,
        std::vector<CoreIrValue *>{zero, idx_load});
    auto *unknown_load =
        body->create_instruction<CoreIrLoadInst>(i32_type, "unknown.load", unknown_addr);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, unknown_load);

    CompilerContext compiler_context = make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer2;
    const std::string text2 = printer2.print_module(*module);
    assert(!text2.empty());
}

void test_hoists_global_load_across_dynamic_other_global_store() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *array_type = context->create_type<CoreIrArrayType>(i32_type, 16);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *ptr_array_type = context->create_type<CoreIrPointerType>(array_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("licm_global_root_provenance");
    auto *scalar =
        module->create_global<CoreIrGlobal>("scalar", i32_type, nullptr, true,
                                            false);
    auto *array =
        module->create_global<CoreIrGlobal>("array", array_type, nullptr, true,
                                            false);
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *four = context->create_constant<CoreIrConstantInt>(i32_type, 4);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *scalar_addr = header->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_i32_type, "scalar.addr", scalar);
    auto *scalar_load = header->create_instruction<CoreIrLoadInst>(
        i32_type, "scalar.load", scalar_addr);
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, four);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *array_addr = body->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_array_type, "array.addr", array);
    auto *elem_ptr = body->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "elem.ptr", array_addr,
        std::vector<CoreIrValue *>{iv});
    body->create_instruction<CoreIrStoreInst>(void_type, scalar_load, elem_ptr);
    auto *next_iv = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "iv.next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, scalar_load);

    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next_iv);

    CompilerContext compiler_context =
        make_compiler_context(std::move(context), module);
    CoreIrLicmPass pass;
    assert(pass.Run(compiler_context).ok);

    assert(scalar_addr->get_parent() == entry);
    assert(scalar_load->get_parent() == entry);
    assert(elem_ptr->get_parent() == body);
}

} // namespace

int main() {
    test_hoists_invariant_binary_but_not_phi_or_variant_user();
    test_hoists_load_without_loop_write();
    test_does_not_hoist_argument_load_across_other_argument_store();
    test_does_not_hoist_must_alias_load();
    test_does_not_hoist_may_alias_load();
    test_does_not_hoist_dynamic_index_argument_load_without_loop_write();
    test_does_not_hoist_non_must_execute_body_instruction();
    test_hoists_single_use_invariant_with_constant_trip_count();
    test_hoists_from_inner_loop_to_outer_preheader();
    test_hoists_redundant_invariant_store();
    test_does_not_hoist_store_when_read_precedes_it();
    test_hoists_speculatively_safe_stackslot_load();
    test_hoists_invariant_store_past_unknown_later_read();
    test_hoists_global_load_across_dynamic_other_global_store();
    return 0;
}
