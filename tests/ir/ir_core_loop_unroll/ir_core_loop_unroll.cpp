#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "backend/ir/loop_unroll/core_ir_loop_unroll_pass.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "backend/ir/simple_loop_unswitch/core_ir_simple_loop_unswitch_pass.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "compiler/pass/pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

std::size_t count_substring(const std::string &text, const std::string &needle) {
    if (needle.empty()) {
        return 0;
    }
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = text.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

} // namespace

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_loop_unroll");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *three = context->create_constant<CoreIrConstantInt>(i32_type, 3);

    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, three);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *body_value = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "body.v", iv, one);
    auto *next = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "iv.next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    exit->create_instruction<CoreIrReturnInst>(void_type, zero);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrLoopUnrollPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("header:") != std::string::npos);
    assert(text.find("body:") != std::string::npos);
    assert(text.find("%body.v") != std::string::npos);

    auto *entry_jump =
        dynamic_cast<CoreIrJumpInst *>(entry->get_instructions().back().get());
    assert(entry_jump != nullptr);
    assert(entry_jump->get_target_block() == exit);

    auto context2 = std::make_unique<CoreIrContext>();
    auto *void_type2 = context2->create_type<CoreIrVoidType>();
    auto *i1_type2 = context2->create_type<CoreIrIntegerType>(1);
    auto *i32_type2 = context2->create_type<CoreIrIntegerType>(32);
    auto *array4_i32 = context2->create_type<CoreIrArrayType>(i32_type2, 4);
    auto *ptr_array4_i32 = context2->create_type<CoreIrPointerType>(array4_i32);
    auto *ptr_i32_type2 = context2->create_type<CoreIrPointerType>(i32_type2);
    auto *function_type2 = context2->create_type<CoreIrFunctionType>(
        i32_type2, std::vector<const CoreIrType *>{}, false);
    auto *module2 =
        context2->create_module<CoreIrModule>("ir_core_loop_unroll_memory");
    auto *function2 =
        module2->create_function<CoreIrFunction>("main", function_type2, false);
    auto *entry2 = function2->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header2 = function2->create_basic_block<CoreIrBasicBlock>("header");
    auto *body2 = function2->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit2 = function2->create_basic_block<CoreIrBasicBlock>("exit");
    auto *sum_slot = function2->create_stack_slot<CoreIrStackSlot>("sum", i32_type2, 4);
    auto *arr_slot =
        function2->create_stack_slot<CoreIrStackSlot>("arr", array4_i32, 4);
    auto *zero2 = context2->create_constant<CoreIrConstantInt>(i32_type2, 0);
    auto *one2 = context2->create_constant<CoreIrConstantInt>(i32_type2, 1);
    auto *two2 = context2->create_constant<CoreIrConstantInt>(i32_type2, 2);
    auto *three2 = context2->create_constant<CoreIrConstantInt>(i32_type2, 3);
    auto *four2 = context2->create_constant<CoreIrConstantInt>(i32_type2, 4);

    auto *arr_addr = entry2->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array4_i32, "arr.addr", arr_slot);
    auto *arr0_addr = entry2->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type2, "arr0.addr", arr_addr,
        std::vector<CoreIrValue *>{zero2, zero2});
    auto *arr1_addr = entry2->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type2, "arr1.addr", arr_addr,
        std::vector<CoreIrValue *>{zero2, one2});
    auto *arr2_addr = entry2->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type2, "arr2.addr", arr_addr,
        std::vector<CoreIrValue *>{zero2, two2});
    auto *arr3_addr = entry2->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type2, "arr3.addr", arr_addr,
        std::vector<CoreIrValue *>{zero2, three2});
    entry2->create_instruction<CoreIrStoreInst>(void_type2, one2, arr0_addr);
    entry2->create_instruction<CoreIrStoreInst>(void_type2, two2, arr1_addr);
    entry2->create_instruction<CoreIrStoreInst>(void_type2, three2, arr2_addr);
    entry2->create_instruction<CoreIrStoreInst>(void_type2, four2, arr3_addr);
    entry2->create_instruction<CoreIrStoreInst>(void_type2, zero2, sum_slot);
    entry2->create_instruction<CoreIrJumpInst>(void_type2, header2);
    auto *iv2 = header2->create_instruction<CoreIrPhiInst>(i32_type2, "iv");
    auto *cmp2 = header2->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type2, "cmp", iv2, four2);
    header2->create_instruction<CoreIrCondJumpInst>(void_type2, cmp2, body2, exit2);
    auto *sum_load = body2->create_instruction<CoreIrLoadInst>(
        i32_type2, "sum.load", sum_slot);
    auto *loop_arr_addr = body2->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array4_i32, "loop.arr.addr", arr_slot);
    auto *elem_addr = body2->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type2, "elem.addr", loop_arr_addr,
        std::vector<CoreIrValue *>{zero2, iv2});
    auto *elem_load = body2->create_instruction<CoreIrLoadInst>(
        i32_type2, "elem.load", elem_addr);
    auto *next_sum = body2->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type2, "sum.next", sum_load, elem_load);
    body2->create_instruction<CoreIrStoreInst>(void_type2, next_sum, sum_slot);
    auto *next_iv2 = body2->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type2, "iv.next", iv2, one2);
    body2->create_instruction<CoreIrJumpInst>(void_type2, header2);
    auto *exit_sum =
        exit2->create_instruction<CoreIrLoadInst>(i32_type2, "sum.exit", sum_slot);
    exit2->create_instruction<CoreIrReturnInst>(void_type2, exit_sum);
    iv2->add_incoming(entry2, zero2);
    iv2->add_incoming(body2, next_iv2);

    CompilerContext compiler_context2;
    compiler_context2.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context2), module2));

    CoreIrLoopUnrollPass memory_unroll_pass;
    assert(memory_unroll_pass.Run(compiler_context2).ok);

    const std::string memory_text = printer.print_module(*module2);
    auto *entry_jump2 =
        dynamic_cast<CoreIrJumpInst *>(entry2->get_instructions().back().get());
    assert(entry_jump2 != nullptr);
    assert(entry_jump2->get_target_block() == exit2);
    assert(count_substring(memory_text, "elem.addr") >= 5);
    assert(count_substring(memory_text, "sum.next") >= 5);

    auto context3 = std::make_unique<CoreIrContext>();
    auto *void_type3 = context3->create_type<CoreIrVoidType>();
    auto *i1_type3 = context3->create_type<CoreIrIntegerType>(1);
    auto *i32_type3 = context3->create_type<CoreIrIntegerType>(32);
    auto *function_type3 = context3->create_type<CoreIrFunctionType>(
        i32_type3, std::vector<const CoreIrType *>{}, false);
    auto *module3 =
        context3->create_module<CoreIrModule>("ir_core_loop_unroll_multi_phi");
    auto *function3 =
        module3->create_function<CoreIrFunction>("main", function_type3, false);
    auto *entry3 = function3->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header3 = function3->create_basic_block<CoreIrBasicBlock>("header");
    auto *body3 = function3->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit3 = function3->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero3 = context3->create_constant<CoreIrConstantInt>(i32_type3, 0);
    auto *one3 = context3->create_constant<CoreIrConstantInt>(i32_type3, 1);
    auto *four3 = context3->create_constant<CoreIrConstantInt>(i32_type3, 4);

    entry3->create_instruction<CoreIrJumpInst>(void_type3, header3);
    auto *iv3 = header3->create_instruction<CoreIrPhiInst>(i32_type3, "iv");
    auto *sum3 = header3->create_instruction<CoreIrPhiInst>(i32_type3, "sum");
    auto *cmp3 = header3->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type3, "cmp", iv3, four3);
    header3->create_instruction<CoreIrCondJumpInst>(void_type3, cmp3, body3, exit3);
    auto *sum_next3 = body3->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type3, "sum.next", sum3, iv3);
    auto *iv_next3 = body3->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type3, "iv.next", iv3, one3);
    body3->create_instruction<CoreIrJumpInst>(void_type3, header3);
    auto *ret3 = exit3->create_instruction<CoreIrReturnInst>(void_type3, sum3);
    iv3->add_incoming(entry3, zero3);
    iv3->add_incoming(body3, iv_next3);
    sum3->add_incoming(entry3, zero3);
    sum3->add_incoming(body3, sum_next3);

    CompilerContext compiler_context3;
    compiler_context3.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context3), module3));

    CoreIrLoopUnrollPass multi_phi_unroll_pass;
    assert(multi_phi_unroll_pass.Run(compiler_context3).ok);

    auto *entry_jump3 =
        dynamic_cast<CoreIrJumpInst *>(entry3->get_instructions().back().get());
    assert(entry_jump3 != nullptr);
    assert(entry_jump3->get_target_block() == exit3);
    assert(ret3->get_return_value() != sum3);

    auto context4 = std::make_unique<CoreIrContext>();
    auto *void_type4 = context4->create_type<CoreIrVoidType>();
    auto *i1_type4 = context4->create_type<CoreIrIntegerType>(1);
    auto *i32_type4 = context4->create_type<CoreIrIntegerType>(32);
    auto *array100_i32 = context4->create_type<CoreIrArrayType>(i32_type4, 100);
    auto *ptr_array100_i32 = context4->create_type<CoreIrPointerType>(array100_i32);
    auto *ptr_i32_type4 = context4->create_type<CoreIrPointerType>(i32_type4);
    auto *function_type4 = context4->create_type<CoreIrFunctionType>(
        i32_type4, std::vector<const CoreIrType *>{}, false);
    auto *module4 =
        context4->create_module<CoreIrModule>("ir_core_loop_unroll_nested_memory");
    auto *function4 =
        module4->create_function<CoreIrFunction>("main", function_type4, false);
    auto *entry4 = function4->create_basic_block<CoreIrBasicBlock>("entry");
    auto *outer_header4 =
        function4->create_basic_block<CoreIrBasicBlock>("outer.header");
    auto *outer_body4 =
        function4->create_basic_block<CoreIrBasicBlock>("outer.body");
    auto *inner_header4 =
        function4->create_basic_block<CoreIrBasicBlock>("inner.header");
    auto *inner_body4 =
        function4->create_basic_block<CoreIrBasicBlock>("inner.body");
    auto *inner_exit4 =
        function4->create_basic_block<CoreIrBasicBlock>("inner.exit");
    auto *outer_exit4 =
        function4->create_basic_block<CoreIrBasicBlock>("outer.exit");
    auto *sum_slot4 =
        function4->create_stack_slot<CoreIrStackSlot>("sum", i32_type4, 4);
    auto *idx_slot4 =
        function4->create_stack_slot<CoreIrStackSlot>("idx", i32_type4, 4);
    auto *arr_slot4 =
        function4->create_stack_slot<CoreIrStackSlot>("arr", array100_i32, 4);
    auto *zero4 = context4->create_constant<CoreIrConstantInt>(i32_type4, 0);
    auto *one4 = context4->create_constant<CoreIrConstantInt>(i32_type4, 1);
    auto *hundred4 = context4->create_constant<CoreIrConstantInt>(i32_type4, 100);

    auto *arr_addr4 = entry4->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array100_i32, "arr.addr", arr_slot4);
    for (std::uint64_t index = 0; index < 100; ++index) {
        auto *index_value =
            context4->create_constant<CoreIrConstantInt>(i32_type4, index);
        auto *element_addr = entry4->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type4, "arr.init." + std::to_string(index), arr_addr4,
            std::vector<CoreIrValue *>{zero4, index_value});
        entry4->create_instruction<CoreIrStoreInst>(void_type4, index_value,
                                                    element_addr);
    }
    entry4->create_instruction<CoreIrStoreInst>(void_type4, zero4, sum_slot4);
    entry4->create_instruction<CoreIrStoreInst>(void_type4, zero4, idx_slot4);
    entry4->create_instruction<CoreIrJumpInst>(void_type4, outer_header4);
    auto *outer_iv4 =
        outer_header4->create_instruction<CoreIrPhiInst>(i32_type4, "outer.iv");
    auto *outer_cmp4 = outer_header4->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type4, "outer.cmp", outer_iv4, one4);
    outer_header4->create_instruction<CoreIrCondJumpInst>(void_type4, outer_cmp4,
                                                          outer_body4, outer_exit4);
    outer_body4->create_instruction<CoreIrStoreInst>(void_type4, zero4, idx_slot4);
    outer_body4->create_instruction<CoreIrJumpInst>(void_type4, inner_header4);
    auto *inner_iv4 =
        inner_header4->create_instruction<CoreIrPhiInst>(i32_type4, "inner.iv");
    auto *inner_cmp4 = inner_header4->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type4, "inner.cmp", inner_iv4,
        hundred4);
    inner_header4->create_instruction<CoreIrCondJumpInst>(void_type4, inner_cmp4,
                                                          inner_body4, inner_exit4);
    auto *sum_load4 = inner_body4->create_instruction<CoreIrLoadInst>(
        i32_type4, "sum.load", sum_slot4);
    auto *loop_arr_addr4 = inner_body4->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array100_i32, "loop.arr.addr", arr_slot4);
    auto *elem_addr4 = inner_body4->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type4, "elem.addr", loop_arr_addr4,
        std::vector<CoreIrValue *>{zero4, inner_iv4});
    auto *elem_load4 = inner_body4->create_instruction<CoreIrLoadInst>(
        i32_type4, "elem.load", elem_addr4);
    auto *sum_next4 = inner_body4->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type4, "sum.next", sum_load4, elem_load4);
    inner_body4->create_instruction<CoreIrStoreInst>(void_type4, sum_next4, sum_slot4);
    auto *inner_next4 = inner_body4->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type4, "inner.next", inner_iv4, one4);
    inner_body4->create_instruction<CoreIrJumpInst>(void_type4, inner_header4);
    inner_exit4->create_instruction<CoreIrStoreInst>(void_type4, inner_iv4, idx_slot4);
    auto *outer_next4 = inner_exit4->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type4, "outer.next", outer_iv4, one4);
    inner_exit4->create_instruction<CoreIrJumpInst>(void_type4, outer_header4);
    auto *final_sum4 =
        outer_exit4->create_instruction<CoreIrLoadInst>(i32_type4, "sum.exit", sum_slot4);
    outer_exit4->create_instruction<CoreIrReturnInst>(void_type4, final_sum4);
    outer_iv4->add_incoming(entry4, zero4);
    outer_iv4->add_incoming(inner_exit4, outer_next4);
    inner_iv4->add_incoming(outer_body4, zero4);
    inner_iv4->add_incoming(inner_body4, inner_next4);

    CompilerContext compiler_context4;
    compiler_context4.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context4), module4));

    CoreIrLoopUnrollPass nested_memory_unroll_pass;
    assert(nested_memory_unroll_pass.Run(compiler_context4).ok);

    auto *outer_body_jump4 =
        dynamic_cast<CoreIrJumpInst *>(outer_body4->get_instructions().back().get());
    assert(outer_body_jump4 != nullptr);
    assert(outer_body_jump4->get_target_block() == inner_exit4);

    auto context5 = std::make_unique<CoreIrContext>();
    auto *void_type5 = context5->create_type<CoreIrVoidType>();
    auto *i1_type5 = context5->create_type<CoreIrIntegerType>(1);
    auto *i32_type5 = context5->create_type<CoreIrIntegerType>(32);
    auto *array4_i32_5 = context5->create_type<CoreIrArrayType>(i32_type5, 4);
    auto *ptr_array4_i32_5 = context5->create_type<CoreIrPointerType>(array4_i32_5);
    auto *ptr_i32_type5 = context5->create_type<CoreIrPointerType>(i32_type5);
    auto *function_type5 = context5->create_type<CoreIrFunctionType>(
        i32_type5, std::vector<const CoreIrType *>{}, false);
    auto *module5 = context5->create_module<CoreIrModule>(
        "ir_core_loop_unroll_after_unswitch");
    auto *function5 =
        module5->create_function<CoreIrFunction>("main", function_type5, false);
    auto *flag_slot5 =
        function5->create_stack_slot<CoreIrStackSlot>("flag", i32_type5, 4);
    auto *arr_slot5 =
        function5->create_stack_slot<CoreIrStackSlot>("arr", array4_i32_5, 4);
    auto *entry5 = function5->create_basic_block<CoreIrBasicBlock>("entry");
    auto *outer_header5 =
        function5->create_basic_block<CoreIrBasicBlock>("outer.header");
    auto *outer_body5 =
        function5->create_basic_block<CoreIrBasicBlock>("outer.body");
    auto *then5 = function5->create_basic_block<CoreIrBasicBlock>("then");
    auto *inner_header5 =
        function5->create_basic_block<CoreIrBasicBlock>("inner.header");
    auto *inner_body5 =
        function5->create_basic_block<CoreIrBasicBlock>("inner.body");
    auto *inner_exit5 =
        function5->create_basic_block<CoreIrBasicBlock>("inner.exit");
    auto *outer_latch5 =
        function5->create_basic_block<CoreIrBasicBlock>("outer.latch");
    auto *outer_exit5 =
        function5->create_basic_block<CoreIrBasicBlock>("outer.exit");
    auto *zero5 = context5->create_constant<CoreIrConstantInt>(i32_type5, 0);
    auto *one5 = context5->create_constant<CoreIrConstantInt>(i32_type5, 1);
    auto *four5 = context5->create_constant<CoreIrConstantInt>(i32_type5, 4);

    auto *arr_addr5 = entry5->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array4_i32_5, "arr.addr", arr_slot5);
    for (std::uint64_t index = 0; index < 4; ++index) {
        auto *index_value =
            context5->create_constant<CoreIrConstantInt>(i32_type5, index + 1);
        auto *element_addr = entry5->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type5, "arr.init." + std::to_string(index), arr_addr5,
            std::vector<CoreIrValue *>{zero5,
                                       context5->create_constant<CoreIrConstantInt>(
                                           i32_type5, index)});
        entry5->create_instruction<CoreIrStoreInst>(void_type5, index_value,
                                                    element_addr);
    }
    entry5->create_instruction<CoreIrStoreInst>(void_type5, one5, flag_slot5);
    entry5->create_instruction<CoreIrJumpInst>(void_type5, outer_header5);
    auto *outer_iv5 =
        outer_header5->create_instruction<CoreIrPhiInst>(i32_type5, "outer.iv");
    auto *outer_cmp5 = outer_header5->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type5, "outer.cmp", outer_iv5, one5);
    outer_header5->create_instruction<CoreIrCondJumpInst>(void_type5, outer_cmp5,
                                                          outer_body5, outer_exit5);
    auto *flag_load5 =
        outer_body5->create_instruction<CoreIrLoadInst>(i32_type5, "flag.load", flag_slot5);
    auto *flag_cmp5 = outer_body5->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::NotEqual, i1_type5, "flag.cmp", flag_load5, zero5);
    outer_body5->create_instruction<CoreIrCondJumpInst>(void_type5, flag_cmp5, then5,
                                                        outer_latch5);
    then5->create_instruction<CoreIrJumpInst>(void_type5, inner_header5);
    auto *inner_iv5 =
        inner_header5->create_instruction<CoreIrPhiInst>(i32_type5, "inner.iv");
    auto *inner_sum5 =
        inner_header5->create_instruction<CoreIrPhiInst>(i32_type5, "inner.sum");
    auto *inner_cmp5 = inner_header5->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type5, "inner.cmp", inner_iv5, four5);
    inner_header5->create_instruction<CoreIrCondJumpInst>(void_type5, inner_cmp5,
                                                          inner_body5, inner_exit5);
    auto *elem_addr5 = inner_body5->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type5, "elem.addr", arr_addr5,
        std::vector<CoreIrValue *>{zero5, inner_iv5});
    auto *elem_load5 = inner_body5->create_instruction<CoreIrLoadInst>(
        i32_type5, "elem.load", elem_addr5);
    auto *inner_sum_next5 = inner_body5->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type5, "inner.sum.next", inner_sum5, elem_load5);
    auto *inner_iv_next5 = inner_body5->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type5, "inner.iv.next", inner_iv5, one5);
    inner_body5->create_instruction<CoreIrJumpInst>(void_type5, inner_header5);
    inner_exit5->create_instruction<CoreIrJumpInst>(void_type5, outer_latch5);
    auto *outer_iv_next5 = outer_latch5->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type5, "outer.iv.next", outer_iv5, one5);
    outer_latch5->create_instruction<CoreIrJumpInst>(void_type5, outer_header5);
    outer_exit5->create_instruction<CoreIrReturnInst>(void_type5, zero5);
    outer_iv5->add_incoming(entry5, zero5);
    outer_iv5->add_incoming(outer_latch5, outer_iv_next5);
    inner_iv5->add_incoming(then5, zero5);
    inner_iv5->add_incoming(inner_body5, inner_iv_next5);
    inner_sum5->add_incoming(then5, zero5);
    inner_sum5->add_incoming(inner_body5, inner_sum_next5);

    CompilerContext compiler_context5;
    compiler_context5.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context5), module5));
    PassManager pass_manager5;
    pass_manager5.AddPass(std::make_unique<CoreIrSimpleLoopUnswitchPass>());
    pass_manager5.AddPass(std::make_unique<CoreIrSimplifyCfgPass>());
    pass_manager5.AddPass(std::make_unique<CoreIrLoopSimplifyPass>());
    pass_manager5.AddPass(std::make_unique<CoreIrLoopUnrollPass>());
    assert(pass_manager5.Run(compiler_context5).ok);

    const std::string text5 = printer.print_module(*module5);
    assert(text5.find("outer.body.unsw.true:") != std::string::npos);
    return 0;
}
