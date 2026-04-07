#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/lcssa/core_ir_lcssa_pass.hpp"
#include "backend/ir/loop_memory_promotion/core_ir_loop_memory_promotion_pass.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
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

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_loop_memory_promotion");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header = function->create_basic_block<CoreIrBasicBlock>("header");
    auto *body = function->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit = function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot = function->create_stack_slot<CoreIrStackSlot>("acc", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *ten = context->create_constant<CoreIrConstantInt>(i32_type, 10);

    entry->create_instruction<CoreIrStoreInst>(void_type, zero, slot);
    entry->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *iv = header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *cmp = header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", iv, ten);
    header->create_instruction<CoreIrCondJumpInst>(void_type, cmp, body, exit);
    auto *load = body->create_instruction<CoreIrLoadInst>(i32_type, "acc.load", slot);
    auto *next_acc = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "acc.next", load, one);
    body->create_instruction<CoreIrStoreInst>(void_type, next_acc, slot);
    auto *next_iv = body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "iv.next", iv, one);
    body->create_instruction<CoreIrJumpInst>(void_type, header);
    auto *exit_load =
        exit->create_instruction<CoreIrLoadInst>(i32_type, "acc.exit", slot);
    exit->create_instruction<CoreIrReturnInst>(void_type, exit_load);
    iv->add_incoming(entry, zero);
    iv->add_incoming(body, next_iv);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrLcssaPass lcssa;
    assert(lcssa.Run(compiler_context).ok);
    CoreIrLoopMemoryPromotionPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%acc.load = load i32, stackslot %acc") == std::string::npos);
    assert(text.find("store i32 %acc.next, stackslot %acc") == std::string::npos);
    assert(text.find("%acc.loop.") != std::string::npos);
    assert(text.find("%acc.exit = load i32, stackslot %acc") == std::string::npos);
    assert(text.find("store i32 %acc.exit, stackslot %acc") == std::string::npos);
    assert(text.find("store i32 ") != std::string::npos);

    auto context2 = std::make_unique<CoreIrContext>();
    auto *void_type2 = context2->create_type<CoreIrVoidType>();
    auto *i1_type2 = context2->create_type<CoreIrIntegerType>(1);
    auto *i32_type2 = context2->create_type<CoreIrIntegerType>(32);
    auto *array2_i32 = context2->create_type<CoreIrArrayType>(i32_type2, 2);
    auto *struct_type = context2->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type2, array2_i32});
    auto *ptr_struct_type = context2->create_type<CoreIrPointerType>(struct_type);
    auto *ptr_i32_type = context2->create_type<CoreIrPointerType>(i32_type2);
    auto *function_type2 = context2->create_type<CoreIrFunctionType>(
        i32_type2, std::vector<const CoreIrType *>{}, false);
    auto *module2 = context2->create_module<CoreIrModule>(
        "ir_core_loop_memory_promotion_access_path");
    auto *function2 =
        module2->create_function<CoreIrFunction>("main", function_type2, false);
    auto *entry2 = function2->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header2 = function2->create_basic_block<CoreIrBasicBlock>("header");
    auto *body2 = function2->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit2 = function2->create_basic_block<CoreIrBasicBlock>("exit");
    auto *slot2 =
        function2->create_stack_slot<CoreIrStackSlot>("state", struct_type, 4);
    auto *zero2 = context2->create_constant<CoreIrConstantInt>(i32_type2, 0);
    auto *one2 = context2->create_constant<CoreIrConstantInt>(i32_type2, 1);
    auto *ten2 = context2->create_constant<CoreIrConstantInt>(i32_type2, 10);

    auto *base_addr = entry2->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_struct_type, "state.addr", slot2);
    auto *field0_addr = entry2->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "field0.addr", base_addr,
        std::vector<CoreIrValue *>{zero2, zero2});
    entry2->create_instruction<CoreIrStoreInst>(void_type2, zero2, field0_addr);
    entry2->create_instruction<CoreIrJumpInst>(void_type2, header2);
    auto *iv2 = header2->create_instruction<CoreIrPhiInst>(i32_type2, "iv");
    auto *cmp2 = header2->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type2, "cmp", iv2, ten2);
    header2->create_instruction<CoreIrCondJumpInst>(void_type2, cmp2, body2, exit2);
    auto *loop_base = body2->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_struct_type, "state.addr.loop", slot2);
    auto *loop_field0_addr = body2->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "field0.addr.loop", loop_base,
        std::vector<CoreIrValue *>{zero2, zero2});
    auto *field0_load = body2->create_instruction<CoreIrLoadInst>(
        i32_type2, "field0.load", loop_field0_addr);
    auto *next_field0 = body2->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type2, "field0.next", field0_load, one2);
    body2->create_instruction<CoreIrStoreInst>(void_type2, next_field0, loop_field0_addr);
    auto *next_iv2 = body2->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type2, "iv.next", iv2, one2);
    body2->create_instruction<CoreIrJumpInst>(void_type2, header2);
    auto *exit_base = exit2->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_struct_type, "state.addr.exit", slot2);
    auto *exit_field0_addr = exit2->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "field0.addr.exit", exit_base,
        std::vector<CoreIrValue *>{zero2, zero2});
    auto *exit_load2 = exit2->create_instruction<CoreIrLoadInst>(
        i32_type2, "field0.exit", exit_field0_addr);
    exit2->create_instruction<CoreIrReturnInst>(void_type2, exit_load2);
    iv2->add_incoming(entry2, zero2);
    iv2->add_incoming(body2, next_iv2);

    CompilerContext compiler_context2;
    compiler_context2.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context2), module2));
    CoreIrLcssaPass lcssa2;
    assert(lcssa2.Run(compiler_context2).ok);
    CoreIrLoopMemoryPromotionPass pass2;
    assert(pass2.Run(compiler_context2).ok);

    const std::string text2 = printer.print_module(*module2);
    assert(text2.find("%field0.load = load i32") == std::string::npos);
    assert(text2.find("store i32 %field0.next") == std::string::npos);
    assert(text2.find("%field0.exit = load i32") == std::string::npos);
    assert(text2.find("%state.loop.") != std::string::npos);

    auto context3 = std::make_unique<CoreIrContext>();
    auto *void_type3 = context3->create_type<CoreIrVoidType>();
    auto *i1_type3 = context3->create_type<CoreIrIntegerType>(1);
    auto *i32_type3 = context3->create_type<CoreIrIntegerType>(32);
    auto *array2_i32_3 = context3->create_type<CoreIrArrayType>(i32_type3, 2);
    auto *ptr_array2_i32_3 = context3->create_type<CoreIrPointerType>(array2_i32_3);
    auto *ptr_i32_type3 = context3->create_type<CoreIrPointerType>(i32_type3);
    auto *function_type3 = context3->create_type<CoreIrFunctionType>(
        i32_type3, std::vector<const CoreIrType *>{}, false);
    auto *module3 = context3->create_module<CoreIrModule>(
        "ir_core_loop_memory_promotion_local_exact");
    auto *function3 =
        module3->create_function<CoreIrFunction>("main", function_type3, false);
    auto *entry3 = function3->create_basic_block<CoreIrBasicBlock>("entry");
    auto *header3 = function3->create_basic_block<CoreIrBasicBlock>("header");
    auto *body3 = function3->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit3 = function3->create_basic_block<CoreIrBasicBlock>("exit");
    auto *state3 =
        function3->create_stack_slot<CoreIrStackSlot>("state", array2_i32_3, 4);
    auto *index_slot3 =
        function3->create_stack_slot<CoreIrStackSlot>("index", i32_type3, 4);
    auto *zero3 = context3->create_constant<CoreIrConstantInt>(i32_type3, 0);
    auto *one3 = context3->create_constant<CoreIrConstantInt>(i32_type3, 1);
    auto *two3 = context3->create_constant<CoreIrConstantInt>(i32_type3, 2);
    auto *four3 = context3->create_constant<CoreIrConstantInt>(i32_type3, 4);

    auto *entry_state_addr = entry3->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array2_i32_3, "state.addr", state3);
    auto *entry_field0_addr = entry3->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type3, "field0.addr", entry_state_addr,
        std::vector<CoreIrValue *>{zero3, zero3});
    entry3->create_instruction<CoreIrStoreInst>(void_type3, zero3, entry_field0_addr);
    entry3->create_instruction<CoreIrStoreInst>(void_type3, one3, index_slot3);
    entry3->create_instruction<CoreIrJumpInst>(void_type3, header3);
    auto *iv3 = header3->create_instruction<CoreIrPhiInst>(i32_type3, "iv");
    auto *cmp3 = header3->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type3, "cmp", iv3, four3);
    header3->create_instruction<CoreIrCondJumpInst>(void_type3, cmp3, body3, exit3);
    auto *loop_state_addr = body3->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array2_i32_3, "state.addr.loop", state3);
    auto *loop_field0_addr3 = body3->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type3, "field0.addr.loop", loop_state_addr,
        std::vector<CoreIrValue *>{zero3, zero3});
    auto *field0_load3 = body3->create_instruction<CoreIrLoadInst>(
        i32_type3, "field0.load", loop_field0_addr3);
    auto *next_field0_3 = body3->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type3, "field0.next", field0_load3, one3);
    body3->create_instruction<CoreIrStoreInst>(void_type3, next_field0_3,
                                               loop_field0_addr3);
    auto *next_iv3 = body3->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type3, "iv.next", iv3, one3);
    body3->create_instruction<CoreIrJumpInst>(void_type3, header3);
    auto *dynamic_index = exit3->create_instruction<CoreIrLoadInst>(
        i32_type3, "index.load", index_slot3);
    auto *exit_state_addr = exit3->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_array2_i32_3, "state.addr.exit", state3);
    auto *dynamic_addr = exit3->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type3, "state.dynamic.addr", exit_state_addr,
        std::vector<CoreIrValue *>{zero3, dynamic_index});
    auto *exit_load3 = exit3->create_instruction<CoreIrLoadInst>(
        i32_type3, "state.dynamic.load", dynamic_addr);
    exit3->create_instruction<CoreIrReturnInst>(void_type3, exit_load3);
    iv3->add_incoming(entry3, zero3);
    iv3->add_incoming(body3, next_iv3);

    CompilerContext compiler_context3;
    compiler_context3.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context3), module3));
    CoreIrLcssaPass lcssa3;
    assert(lcssa3.Run(compiler_context3).ok);
    CoreIrLoopMemoryPromotionPass pass3;
    assert(pass3.Run(compiler_context3).ok);

    const std::string text3 = printer.print_module(*module3);
    assert(text3.find("%field0.load = load i32") == std::string::npos);
    assert(text3.find("store i32 %field0.next") == std::string::npos);
    assert(text3.find("state.dynamic.load = load i32") != std::string::npos);
    assert(text3.find("%state.loop.") != std::string::npos);

    auto context4 = std::make_unique<CoreIrContext>();
    auto *void_type4 = context4->create_type<CoreIrVoidType>();
    auto *i1_type4 = context4->create_type<CoreIrIntegerType>(1);
    auto *i32_type4 = context4->create_type<CoreIrIntegerType>(32);
    auto *function_type4 = context4->create_type<CoreIrFunctionType>(
        i32_type4, std::vector<const CoreIrType *>{}, false);
    auto *module4 = context4->create_module<CoreIrModule>(
        "ir_core_loop_memory_promotion_preheader_seed");
    auto *function4 =
        module4->create_function<CoreIrFunction>("main", function_type4, false);
    auto *entry4 = function4->create_basic_block<CoreIrBasicBlock>("entry");
    auto *preheader4 =
        function4->create_basic_block<CoreIrBasicBlock>("preheader");
    auto *header4 = function4->create_basic_block<CoreIrBasicBlock>("header");
    auto *body4 = function4->create_basic_block<CoreIrBasicBlock>("body");
    auto *exit4 = function4->create_basic_block<CoreIrBasicBlock>("exit");
    auto *sum_slot4 =
        function4->create_stack_slot<CoreIrStackSlot>("sum", i32_type4, 4);
    auto *zero4 = context4->create_constant<CoreIrConstantInt>(i32_type4, 0);
    auto *one4 = context4->create_constant<CoreIrConstantInt>(i32_type4, 1);
    auto *two4 = context4->create_constant<CoreIrConstantInt>(i32_type4, 2);

    entry4->create_instruction<CoreIrStoreInst>(void_type4, zero4, sum_slot4);
    entry4->create_instruction<CoreIrJumpInst>(void_type4, preheader4);
    preheader4->create_instruction<CoreIrJumpInst>(void_type4, header4);
    auto *iv4 = header4->create_instruction<CoreIrPhiInst>(i32_type4, "iv");
    auto *cmp4 = header4->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type4, "cmp", iv4, two4);
    header4->create_instruction<CoreIrCondJumpInst>(void_type4, cmp4, body4, exit4);
    auto *sum_load4 = body4->create_instruction<CoreIrLoadInst>(
        i32_type4, "sum.inner.load", sum_slot4);
    auto *sum_next4 = body4->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type4, "sum.inner.next", sum_load4, one4);
    body4->create_instruction<CoreIrStoreInst>(void_type4, sum_next4, sum_slot4);
    auto *next_iv4 = body4->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type4, "iv.next", iv4, one4);
    body4->create_instruction<CoreIrJumpInst>(void_type4, header4);
    auto *sum_exit4 =
        exit4->create_instruction<CoreIrLoadInst>(i32_type4, "sum.exit", sum_slot4);
    exit4->create_instruction<CoreIrReturnInst>(void_type4, sum_exit4);
    iv4->add_incoming(preheader4, zero4);
    iv4->add_incoming(body4, next_iv4);

    CompilerContext compiler_context4;
    compiler_context4.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context4), module4));
    CoreIrLcssaPass lcssa4;
    assert(lcssa4.Run(compiler_context4).ok);
    CoreIrLoopMemoryPromotionPass pass4;
    assert(pass4.Run(compiler_context4).ok);

    const std::string text4 = printer.print_module(*module4);
    assert(text4.find("%sum.inner.load = load i32, stackslot %sum") == std::string::npos);
    assert(text4.find("store i32 %sum.inner.next, stackslot %sum") == std::string::npos);
    assert(text4.find("%sum.loop.") != std::string::npos);
    {
        auto context5 = std::make_unique<CoreIrContext>();
        auto *void_type5 = context5->create_type<CoreIrVoidType>();
        auto *i1_type5 = context5->create_type<CoreIrIntegerType>(1);
        auto *i32_type5 = context5->create_type<CoreIrIntegerType>(32);
        auto *array2_i32_5 = context5->create_type<CoreIrArrayType>(i32_type5, 2);
        auto *ptr_array2_i32_5 =
            context5->create_type<CoreIrPointerType>(array2_i32_5);
        auto *ptr_i32_type5 = context5->create_type<CoreIrPointerType>(i32_type5);
        auto *function_type5 = context5->create_type<CoreIrFunctionType>(
            i32_type5, std::vector<const CoreIrType *>{}, false);
        auto *module5 = context5->create_module<CoreIrModule>(
            "ir_core_loop_memory_promotion_nested_access_path");
        auto *function5 =
            module5->create_function<CoreIrFunction>("main", function_type5, false);
        auto *entry5 = function5->create_basic_block<CoreIrBasicBlock>("entry");
        auto *outer_header5 =
            function5->create_basic_block<CoreIrBasicBlock>("outer.header");
        auto *outer_body5 =
            function5->create_basic_block<CoreIrBasicBlock>("outer.body");
        auto *inner_preheader5 =
            function5->create_basic_block<CoreIrBasicBlock>("inner.preheader");
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
        auto *state5 =
            function5->create_stack_slot<CoreIrStackSlot>("state", array2_i32_5, 4);
        auto *zero5 = context5->create_constant<CoreIrConstantInt>(i32_type5, 0);
        auto *one5 = context5->create_constant<CoreIrConstantInt>(i32_type5, 1);
        auto *two5 = context5->create_constant<CoreIrConstantInt>(i32_type5, 2);

        auto *entry_state_addr5 =
            entry5->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_5, "state.addr", state5);
        auto *entry_field0_addr5 = entry5->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type5, "state.field0.addr", entry_state_addr5,
            std::vector<CoreIrValue *>{zero5, zero5});
        entry5->create_instruction<CoreIrStoreInst>(void_type5, zero5,
                                                    entry_field0_addr5);
        entry5->create_instruction<CoreIrJumpInst>(void_type5, outer_header5);
        auto *outer_iv =
            outer_header5->create_instruction<CoreIrPhiInst>(i32_type5, "outer.iv");
        auto *outer_cmp = outer_header5->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::SignedLess, i1_type5, "outer.cmp", outer_iv,
            one5);
        outer_header5->create_instruction<CoreIrCondJumpInst>(
            void_type5, outer_cmp, outer_body5, outer_exit5);
        outer_body5->create_instruction<CoreIrJumpInst>(void_type5, inner_preheader5);
        inner_preheader5->create_instruction<CoreIrJumpInst>(void_type5, inner_header5);
        auto *inner_iv =
            inner_header5->create_instruction<CoreIrPhiInst>(i32_type5, "inner.iv");
        auto *inner_cmp = inner_header5->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::SignedLess, i1_type5, "inner.cmp", inner_iv,
            two5);
        inner_header5->create_instruction<CoreIrCondJumpInst>(
            void_type5, inner_cmp, inner_body5, inner_exit5);
        auto *loop_state_addr5 =
            inner_body5->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_5, "state.addr.loop", state5);
        auto *loop_field0_addr5 =
            inner_body5->create_instruction<CoreIrGetElementPtrInst>(
                ptr_i32_type5, "state.field0.addr.loop", loop_state_addr5,
                std::vector<CoreIrValue *>{zero5, zero5});
        auto *field0_load5 = inner_body5->create_instruction<CoreIrLoadInst>(
            i32_type5, "field0.inner.load", loop_field0_addr5);
        auto *field0_next5 = inner_body5->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type5, "field0.inner.next", field0_load5,
            one5);
        inner_body5->create_instruction<CoreIrStoreInst>(void_type5, field0_next5,
                                                         loop_field0_addr5);
        auto *inner_next = inner_body5->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type5, "inner.next", inner_iv, one5);
        inner_body5->create_instruction<CoreIrJumpInst>(void_type5, inner_header5);
        inner_exit5->create_instruction<CoreIrJumpInst>(void_type5, outer_latch5);
        auto *outer_next = outer_latch5->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type5, "outer.next", outer_iv, one5);
        outer_latch5->create_instruction<CoreIrJumpInst>(void_type5, outer_header5);
        auto *exit_state_addr5 =
            outer_exit5->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_5, "state.addr.exit", state5);
        auto *exit_field0_addr5 =
            outer_exit5->create_instruction<CoreIrGetElementPtrInst>(
                ptr_i32_type5, "state.field0.addr.exit", exit_state_addr5,
                std::vector<CoreIrValue *>{zero5, zero5});
        auto *exit_load5 = outer_exit5->create_instruction<CoreIrLoadInst>(
            i32_type5, "field0.exit", exit_field0_addr5);
        outer_exit5->create_instruction<CoreIrReturnInst>(void_type5, exit_load5);
        outer_iv->add_incoming(entry5, zero5);
        outer_iv->add_incoming(outer_latch5, outer_next);
        inner_iv->add_incoming(inner_preheader5, zero5);
        inner_iv->add_incoming(inner_body5, inner_next);

        CompilerContext compiler_context5;
        compiler_context5.set_core_ir_build_result(
            std::make_unique<CoreIrBuildResult>(std::move(context5), module5));
        CoreIrLcssaPass lcssa5;
        assert(lcssa5.Run(compiler_context5).ok);
        CoreIrLoopMemoryPromotionPass pass5;
        assert(pass5.Run(compiler_context5).ok);

        const std::string text5 = printer.print_module(*module5);
        assert(text5.find("%field0.inner.load = load i32") == std::string::npos);
        assert(text5.find("store i32 %field0.inner.next") == std::string::npos);
        assert(text5.find("%field0.exit = load i32") == std::string::npos);
        assert(text5.find("%state.loop.") != std::string::npos);
    }
    {
        auto context6 = std::make_unique<CoreIrContext>();
        auto *void_type6 = context6->create_type<CoreIrVoidType>();
        auto *i1_type6 = context6->create_type<CoreIrIntegerType>(1);
        auto *i32_type6 = context6->create_type<CoreIrIntegerType>(32);
        auto *function_type6 = context6->create_type<CoreIrFunctionType>(
            i32_type6, std::vector<const CoreIrType *>{}, false);
        auto *module6 = context6->create_module<CoreIrModule>(
            "ir_core_loop_memory_promotion_whole_slot_multi_def");
        auto *function6 =
            module6->create_function<CoreIrFunction>("main", function_type6, false);
        auto *entry6 = function6->create_basic_block<CoreIrBasicBlock>("entry");
        auto *header6 = function6->create_basic_block<CoreIrBasicBlock>("header");
        auto *first6 = function6->create_basic_block<CoreIrBasicBlock>("first");
        auto *second6 = function6->create_basic_block<CoreIrBasicBlock>("second");
        auto *exit6 = function6->create_basic_block<CoreIrBasicBlock>("exit");
        auto *sum_slot6 =
            function6->create_stack_slot<CoreIrStackSlot>("sum", i32_type6, 4);
        auto *zero6 = context6->create_constant<CoreIrConstantInt>(i32_type6, 0);
        auto *one6 = context6->create_constant<CoreIrConstantInt>(i32_type6, 1);
        auto *two6 = context6->create_constant<CoreIrConstantInt>(i32_type6, 2);
        auto *three6 =
            context6->create_constant<CoreIrConstantInt>(i32_type6, 3);

        entry6->create_instruction<CoreIrStoreInst>(void_type6, zero6, sum_slot6);
        entry6->create_instruction<CoreIrJumpInst>(void_type6, header6);
        auto *iv6 = header6->create_instruction<CoreIrPhiInst>(i32_type6, "iv");
        auto *cmp6 = header6->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::SignedLess, i1_type6, "cmp", iv6, three6);
        header6->create_instruction<CoreIrCondJumpInst>(void_type6, cmp6, first6,
                                                        exit6);

        auto *sum_first_load = first6->create_instruction<CoreIrLoadInst>(
            i32_type6, "sum.first.load", sum_slot6);
        auto *sum_after_first = first6->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type6, "sum.first.next",
            sum_first_load, one6);
        first6->create_instruction<CoreIrStoreInst>(void_type6, sum_after_first,
                                                    sum_slot6);
        first6->create_instruction<CoreIrJumpInst>(void_type6, second6);

        auto *sum_second_load = second6->create_instruction<CoreIrLoadInst>(
            i32_type6, "sum.second.load", sum_slot6);
        auto *sum_after_second = second6->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type6, "sum.second.next",
            sum_second_load, two6);
        second6->create_instruction<CoreIrStoreInst>(void_type6, sum_after_second,
                                                     sum_slot6);
        auto *next_iv6 = second6->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type6, "iv.next", iv6, one6);
        second6->create_instruction<CoreIrJumpInst>(void_type6, header6);

        auto *sum_exit6 =
            exit6->create_instruction<CoreIrLoadInst>(i32_type6, "sum.exit", sum_slot6);
        exit6->create_instruction<CoreIrReturnInst>(void_type6, sum_exit6);
        iv6->add_incoming(entry6, zero6);
        iv6->add_incoming(second6, next_iv6);

        CompilerContext compiler_context6;
        compiler_context6.set_core_ir_build_result(
            std::make_unique<CoreIrBuildResult>(std::move(context6), module6));
        CoreIrLcssaPass lcssa6;
        assert(lcssa6.Run(compiler_context6).ok);
        CoreIrLoopMemoryPromotionPass pass6;
        assert(pass6.Run(compiler_context6).ok);

        const std::string text6 = printer.print_module(*module6);
        assert(text6.find("%sum.first.load = load i32, %sum") !=
               std::string::npos);
        assert(text6.find("%sum.second.load = load i32, %sum") !=
               std::string::npos);
        assert(text6.find("store i32 %sum.first.next, %sum") !=
               std::string::npos);
        assert(text6.find("store i32 %sum.second.next, %sum") !=
               std::string::npos);
        assert(text6.find("%sum.loop.") == std::string::npos);
    }
    return 0;
}
