#include <cassert>
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

void test_does_not_hoist_unknown_location_load() {
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

void test_does_not_hoist_single_use_addrof_chain() {
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

    assert(addr->get_parent() == header);
    assert(load->get_parent() == header);
}

} // namespace

int main() {
    test_hoists_invariant_binary_but_not_phi_or_variant_user();
    test_hoists_load_without_loop_write();
    test_does_not_hoist_must_alias_load();
    test_does_not_hoist_may_alias_load();
    test_does_not_hoist_unknown_location_load();
    test_does_not_hoist_non_must_execute_body_instruction();
    test_hoists_from_inner_loop_to_outer_preheader();
    test_does_not_hoist_single_use_addrof_chain();
    return 0;
}
