#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/lcssa/core_ir_lcssa_pass.hpp"
#include "backend/ir/loop_memory_promotion/core_ir_loop_memory_promotion_pass.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"
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

namespace {

void assert_module_verifies(const CoreIrModule &module) {
    CoreIrVerifier verifier;
    assert(verifier.verify_module(module).ok);
}

bool block_has_phi(const CoreIrBasicBlock &block) {
    for (const auto &instruction_ptr : block.get_instructions()) {
        if (instruction_ptr == nullptr) {
            continue;
        }
        return dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get()) != nullptr;
    }
    return false;
}

} // namespace

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
    assert_module_verifies(*module);

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
    assert_module_verifies(*module2);

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
    assert_module_verifies(*module3);

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
    assert_module_verifies(*module4);

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
        assert_module_verifies(*module5);

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
        assert_module_verifies(*module6);

        const std::string text6 = printer.print_module(*module6);
        assert(text6.find("%sum.first.load = load i32, %sum") ==
               std::string::npos);
        assert(text6.find("%sum.second.load = load i32, %sum") ==
               std::string::npos);
        assert(text6.find("store i32 %sum.first.next, %sum") ==
               std::string::npos);
        assert(text6.find("store i32 %sum.second.next, %sum") ==
               std::string::npos);
        assert(text6.find("%sum.loop.") != std::string::npos);
    }
    {
        auto context7 = std::make_unique<CoreIrContext>();
        auto *void_type7 = context7->create_type<CoreIrVoidType>();
        auto *i1_type7 = context7->create_type<CoreIrIntegerType>(1);
        auto *i32_type7 = context7->create_type<CoreIrIntegerType>(32);
        auto *i32_type7_alt = context7->create_type<CoreIrIntegerType>(32);
        auto *function_type7 = context7->create_type<CoreIrFunctionType>(
            i32_type7, std::vector<const CoreIrType *>{}, false);
        auto *module7 = context7->create_module<CoreIrModule>(
            "ir_core_loop_memory_promotion_whole_slot_conditional_reset");
        auto *function7 =
            module7->create_function<CoreIrFunction>("main", function_type7, false);
        auto *entry7 = function7->create_basic_block<CoreIrBasicBlock>("entry");
        auto *header7 = function7->create_basic_block<CoreIrBasicBlock>("header");
        auto *body7 = function7->create_basic_block<CoreIrBasicBlock>("body");
        auto *reset7 = function7->create_basic_block<CoreIrBasicBlock>("reset");
        auto *latch7 = function7->create_basic_block<CoreIrBasicBlock>("latch");
        auto *exit7 = function7->create_basic_block<CoreIrBasicBlock>("exit");
        auto *tank_slot7 =
            function7->create_stack_slot<CoreIrStackSlot>("tank", i32_type7, 4);
        auto *zero7 = context7->create_constant<CoreIrConstantInt>(i32_type7, 0);
        auto *zero7_alt =
            context7->create_constant<CoreIrConstantInt>(i32_type7_alt, 0);
        auto *one7 = context7->create_constant<CoreIrConstantInt>(i32_type7, 1);
        auto *two7 = context7->create_constant<CoreIrConstantInt>(i32_type7, 2);

        entry7->create_instruction<CoreIrStoreInst>(void_type7, zero7, tank_slot7);
        entry7->create_instruction<CoreIrJumpInst>(void_type7, header7);
        auto *iv7 = header7->create_instruction<CoreIrPhiInst>(i32_type7, "iv");
        auto *cmp7 = header7->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::SignedLess, i1_type7, "cmp", iv7, two7);
        header7->create_instruction<CoreIrCondJumpInst>(void_type7, cmp7, body7, exit7);

        auto *tank_load7 =
            body7->create_instruction<CoreIrLoadInst>(i32_type7, "tank.load", tank_slot7);
        auto *tank_next7 = body7->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type7, "tank.next", tank_load7, one7);
        body7->create_instruction<CoreIrStoreInst>(void_type7, tank_next7, tank_slot7);
        auto *reset_cmp7 = body7->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::Equal, i1_type7, "reset.cmp", iv7, zero7);
        body7->create_instruction<CoreIrCondJumpInst>(void_type7, reset_cmp7, reset7,
                                                      latch7);

        reset7->create_instruction<CoreIrStoreInst>(void_type7, zero7_alt, tank_slot7);
        reset7->create_instruction<CoreIrJumpInst>(void_type7, latch7);

        auto *next_iv7 = latch7->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type7, "iv.next", iv7, one7);
        latch7->create_instruction<CoreIrJumpInst>(void_type7, header7);

        auto *tank_exit7 =
            exit7->create_instruction<CoreIrLoadInst>(i32_type7, "tank.exit", tank_slot7);
        exit7->create_instruction<CoreIrReturnInst>(void_type7, tank_exit7);
        iv7->add_incoming(entry7, zero7);
        iv7->add_incoming(latch7, next_iv7);

        CompilerContext compiler_context7;
        compiler_context7.set_core_ir_build_result(
            std::make_unique<CoreIrBuildResult>(std::move(context7), module7));
        CoreIrLcssaPass lcssa7;
        assert(lcssa7.Run(compiler_context7).ok);
        CoreIrLoopMemoryPromotionPass pass7;
        assert(pass7.Run(compiler_context7).ok);
        assert_module_verifies(*module7);

        const std::string text7 = printer.print_module(*module7);
        assert(text7.find("%tank.load = load i32, %tank") == std::string::npos);
        assert(text7.find("store i32 %tank.next, %tank") == std::string::npos);
        assert(text7.find("%tank.exit = load i32, %tank") == std::string::npos);
        assert(text7.find("%tank.loop.") != std::string::npos);
    }
    {
        auto context8 = std::make_unique<CoreIrContext>();
        auto *void_type8 = context8->create_type<CoreIrVoidType>();
        auto *i1_type8 = context8->create_type<CoreIrIntegerType>(1);
        auto *i32_type8 = context8->create_type<CoreIrIntegerType>(32);
        auto *array2_i32_8 = context8->create_type<CoreIrArrayType>(i32_type8, 2);
        auto *ptr_array2_i32_8 =
            context8->create_type<CoreIrPointerType>(array2_i32_8);
        auto *ptr_i32_type8 = context8->create_type<CoreIrPointerType>(i32_type8);
        auto *function_type8 = context8->create_type<CoreIrFunctionType>(
            i32_type8, std::vector<const CoreIrType *>{}, false);
        auto *module8 = context8->create_module<CoreIrModule>(
            "ir_core_loop_memory_promotion_access_path_exit_store");
        auto *function8 =
            module8->create_function<CoreIrFunction>("main", function_type8, false);
        auto *entry8 = function8->create_basic_block<CoreIrBasicBlock>("entry");
        auto *header8 = function8->create_basic_block<CoreIrBasicBlock>("header");
        auto *body8 = function8->create_basic_block<CoreIrBasicBlock>("body");
        auto *exit8 = function8->create_basic_block<CoreIrBasicBlock>("exit");
        auto *state8 =
            function8->create_stack_slot<CoreIrStackSlot>("state", array2_i32_8, 4);
        auto *zero8 = context8->create_constant<CoreIrConstantInt>(i32_type8, 0);
        auto *one8 = context8->create_constant<CoreIrConstantInt>(i32_type8, 1);
        auto *three8 = context8->create_constant<CoreIrConstantInt>(i32_type8, 3);
        auto *five8 = context8->create_constant<CoreIrConstantInt>(i32_type8, 5);

        auto *entry_state_addr8 =
            entry8->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_8, "state.addr", state8);
        auto *entry_field0_addr8 = entry8->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type8, "field0.addr", entry_state_addr8,
            std::vector<CoreIrValue *>{zero8, zero8});
        entry8->create_instruction<CoreIrStoreInst>(void_type8, zero8,
                                                    entry_field0_addr8);
        entry8->create_instruction<CoreIrJumpInst>(void_type8, header8);
        auto *iv8 = header8->create_instruction<CoreIrPhiInst>(i32_type8, "iv");
        auto *cmp8 = header8->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::SignedLess, i1_type8, "cmp", iv8, three8);
        header8->create_instruction<CoreIrCondJumpInst>(void_type8, cmp8, body8, exit8);

        auto *loop_state_addr8 =
            body8->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_8, "state.addr.loop", state8);
        auto *loop_field0_addr8 = body8->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type8, "field0.addr.loop", loop_state_addr8,
            std::vector<CoreIrValue *>{zero8, zero8});
        auto *field0_load8 = body8->create_instruction<CoreIrLoadInst>(
            i32_type8, "field0.load", loop_field0_addr8);
        auto *field0_next8 = body8->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type8, "field0.next", field0_load8, one8);
        body8->create_instruction<CoreIrStoreInst>(void_type8, field0_next8,
                                                   loop_field0_addr8);
        auto *next_iv8 = body8->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type8, "iv.next", iv8, one8);
        body8->create_instruction<CoreIrJumpInst>(void_type8, header8);

        auto *exit_state_addr8 =
            exit8->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_8, "state.addr.exit", state8);
        auto *exit_field0_addr8 = exit8->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type8, "field0.addr.exit", exit_state_addr8,
            std::vector<CoreIrValue *>{zero8, zero8});
        auto *exit_load8 = exit8->create_instruction<CoreIrLoadInst>(
            i32_type8, "field0.exit", exit_field0_addr8);
        auto *exit_value8 = exit8->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type8, "field0.after", exit_load8, five8);
        exit8->create_instruction<CoreIrStoreInst>(void_type8, exit_value8,
                                                   exit_field0_addr8);
        exit8->create_instruction<CoreIrReturnInst>(void_type8, exit_value8);
        iv8->add_incoming(entry8, zero8);
        iv8->add_incoming(body8, next_iv8);

        CompilerContext compiler_context8;
        compiler_context8.set_core_ir_build_result(
            std::make_unique<CoreIrBuildResult>(std::move(context8), module8));
        CoreIrLcssaPass lcssa8;
        assert(lcssa8.Run(compiler_context8).ok);
        CoreIrLoopMemoryPromotionPass pass8;
        assert(pass8.Run(compiler_context8).ok);
        assert_module_verifies(*module8);

        const std::string text8 = printer.print_module(*module8);
        assert(text8.find("%field0.load = load i32") == std::string::npos);
        assert(text8.find("store i32 %field0.next") == std::string::npos);
        assert(text8.find("%field0.exit = load i32") == std::string::npos);
        assert(text8.find("%state.loop.") != std::string::npos);
        assert(text8.find("store i32 %field0.after") != std::string::npos);
    }
    {
        auto context9 = std::make_unique<CoreIrContext>();
        auto *void_type9 = context9->create_type<CoreIrVoidType>();
        auto *i1_type9 = context9->create_type<CoreIrIntegerType>(1);
        auto *i32_type9 = context9->create_type<CoreIrIntegerType>(32);
        auto *array2_i32_9 = context9->create_type<CoreIrArrayType>(i32_type9, 2);
        auto *ptr_array2_i32_9 =
            context9->create_type<CoreIrPointerType>(array2_i32_9);
        auto *ptr_i32_type9 = context9->create_type<CoreIrPointerType>(i32_type9);
        auto *function_type9 = context9->create_type<CoreIrFunctionType>(
            i32_type9, std::vector<const CoreIrType *>{}, false);
        auto *module9 = context9->create_module<CoreIrModule>(
            "ir_core_loop_memory_promotion_access_path_whole_store");
        auto *function9 =
            module9->create_function<CoreIrFunction>("main", function_type9, false);
        auto *entry9 = function9->create_basic_block<CoreIrBasicBlock>("entry");
        auto *header9 = function9->create_basic_block<CoreIrBasicBlock>("header");
        auto *body9 = function9->create_basic_block<CoreIrBasicBlock>("body");
        auto *exit9 = function9->create_basic_block<CoreIrBasicBlock>("exit");
        auto *state9 =
            function9->create_stack_slot<CoreIrStackSlot>("state", array2_i32_9, 4);
        auto *zero9 = context9->create_constant<CoreIrConstantInt>(i32_type9, 0);
        auto *one9 = context9->create_constant<CoreIrConstantInt>(i32_type9, 1);
        auto *three9 = context9->create_constant<CoreIrConstantInt>(i32_type9, 3);
        auto *state_zero9 =
            context9->create_constant<CoreIrConstantZeroInitializer>(array2_i32_9);

        auto *entry_state_addr9 =
            entry9->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_9, "state.addr", state9);
        auto *entry_field0_addr9 = entry9->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type9, "field0.addr", entry_state_addr9,
            std::vector<CoreIrValue *>{zero9, zero9});
        entry9->create_instruction<CoreIrStoreInst>(void_type9, one9, entry_field0_addr9);
        entry9->create_instruction<CoreIrJumpInst>(void_type9, header9);
        auto *iv9 = header9->create_instruction<CoreIrPhiInst>(i32_type9, "iv");
        auto *cmp9 = header9->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::SignedLess, i1_type9, "cmp", iv9, three9);
        header9->create_instruction<CoreIrCondJumpInst>(void_type9, cmp9, body9, exit9);

        body9->create_instruction<CoreIrStoreInst>(void_type9, state_zero9, state9);
        auto *loop_state_addr9 =
            body9->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_9, "state.addr.loop", state9);
        auto *loop_field0_addr9 = body9->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type9, "field0.addr.loop", loop_state_addr9,
            std::vector<CoreIrValue *>{zero9, zero9});
        auto *field0_load9 = body9->create_instruction<CoreIrLoadInst>(
            i32_type9, "field0.load", loop_field0_addr9);
        auto *field0_next9 = body9->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type9, "field0.next", field0_load9, one9);
        body9->create_instruction<CoreIrStoreInst>(void_type9, field0_next9,
                                                   loop_field0_addr9);
        auto *next_iv9 = body9->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type9, "iv.next", iv9, one9);
        body9->create_instruction<CoreIrJumpInst>(void_type9, header9);

        auto *exit_state_addr9 =
            exit9->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_9, "state.addr.exit", state9);
        auto *exit_field0_addr9 = exit9->create_instruction<CoreIrGetElementPtrInst>(
            ptr_i32_type9, "field0.addr.exit", exit_state_addr9,
            std::vector<CoreIrValue *>{zero9, zero9});
        auto *exit_load9 = exit9->create_instruction<CoreIrLoadInst>(
            i32_type9, "field0.exit", exit_field0_addr9);
        exit9->create_instruction<CoreIrReturnInst>(void_type9, exit_load9);
        iv9->add_incoming(entry9, zero9);
        iv9->add_incoming(body9, next_iv9);

        CompilerContext compiler_context9;
        compiler_context9.set_core_ir_build_result(
            std::make_unique<CoreIrBuildResult>(std::move(context9), module9));
        CoreIrLcssaPass lcssa9;
        assert(lcssa9.Run(compiler_context9).ok);
        CoreIrLoopMemoryPromotionPass pass9;
        assert(pass9.Run(compiler_context9).ok);
        assert_module_verifies(*module9);

        const std::string text9 = printer.print_module(*module9);
        assert(text9.find("%field0.load = load i32") == std::string::npos);
        assert(text9.find("store i32 %field0.next") == std::string::npos);
        assert(text9.find("%field0.exit = load i32") == std::string::npos);
        assert(text9.find("store [2 x i32] zeroinitializer, %state") !=
               std::string::npos);
        assert(text9.find("%state.loop.") != std::string::npos);
    }
    {
        auto context10 = std::make_unique<CoreIrContext>();
        auto *void_type10 = context10->create_type<CoreIrVoidType>();
        auto *i1_type10 = context10->create_type<CoreIrIntegerType>(1);
        auto *i32_type10 = context10->create_type<CoreIrIntegerType>(32);
        auto *array2_i32_10 = context10->create_type<CoreIrArrayType>(i32_type10, 2);
        auto *ptr_array2_i32_10 =
            context10->create_type<CoreIrPointerType>(array2_i32_10);
        auto *ptr_i32_type10 = context10->create_type<CoreIrPointerType>(i32_type10);
        auto *function_type10 = context10->create_type<CoreIrFunctionType>(
            i32_type10, std::vector<const CoreIrType *>{}, false);
        auto *module10 = context10->create_module<CoreIrModule>(
            "ir_core_loop_memory_promotion_access_path_join_phi");
        auto *function10 =
            module10->create_function<CoreIrFunction>("main", function_type10, false);
        auto *entry10 = function10->create_basic_block<CoreIrBasicBlock>("entry");
        auto *header10 = function10->create_basic_block<CoreIrBasicBlock>("header");
        auto *body10 = function10->create_basic_block<CoreIrBasicBlock>("body");
        auto *reset10 = function10->create_basic_block<CoreIrBasicBlock>("reset");
        auto *join10 = function10->create_basic_block<CoreIrBasicBlock>("join");
        auto *exit10 = function10->create_basic_block<CoreIrBasicBlock>("exit");
        auto *state10 =
            function10->create_stack_slot<CoreIrStackSlot>("state", array2_i32_10, 4);
        auto *zero10 = context10->create_constant<CoreIrConstantInt>(i32_type10, 0);
        auto *one10 = context10->create_constant<CoreIrConstantInt>(i32_type10, 1);
        auto *two10 = context10->create_constant<CoreIrConstantInt>(i32_type10, 2);
        auto *five10 = context10->create_constant<CoreIrConstantInt>(i32_type10, 5);
        auto *state_zero10 =
            context10->create_constant<CoreIrConstantZeroInitializer>(array2_i32_10);

        auto *entry_state_addr10 =
            entry10->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_10, "state.addr", state10);
        auto *entry_field0_addr10 =
            entry10->create_instruction<CoreIrGetElementPtrInst>(
                ptr_i32_type10, "field0.addr", entry_state_addr10,
                std::vector<CoreIrValue *>{zero10, zero10});
        entry10->create_instruction<CoreIrStoreInst>(void_type10, five10,
                                                     entry_field0_addr10);
        entry10->create_instruction<CoreIrJumpInst>(void_type10, header10);
        auto *iv10 = header10->create_instruction<CoreIrPhiInst>(i32_type10, "iv");
        auto *cmp10 = header10->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::SignedLess, i1_type10, "cmp", iv10, two10);
        header10->create_instruction<CoreIrCondJumpInst>(void_type10, cmp10, body10,
                                                         exit10);

        auto *reset_cmp10 = body10->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::Equal, i1_type10, "reset.cmp", iv10, zero10);
        body10->create_instruction<CoreIrCondJumpInst>(void_type10, reset_cmp10, reset10,
                                                       join10);

        reset10->create_instruction<CoreIrStoreInst>(void_type10, state_zero10, state10);
        reset10->create_instruction<CoreIrJumpInst>(void_type10, join10);

        auto *join_state_addr10 =
            join10->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_10, "state.addr.join", state10);
        auto *join_field0_addr10 =
            join10->create_instruction<CoreIrGetElementPtrInst>(
                ptr_i32_type10, "field0.addr.join", join_state_addr10,
                std::vector<CoreIrValue *>{zero10, zero10});
        auto *field0_load10 = join10->create_instruction<CoreIrLoadInst>(
            i32_type10, "field0.load", join_field0_addr10);
        auto *field0_next10 = join10->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type10, "field0.next", field0_load10, one10);
        join10->create_instruction<CoreIrStoreInst>(void_type10, field0_next10,
                                                    join_field0_addr10);
        auto *next_iv10 = join10->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type10, "iv.next", iv10, one10);
        join10->create_instruction<CoreIrJumpInst>(void_type10, header10);

        auto *exit_state_addr10 =
            exit10->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_array2_i32_10, "state.addr.exit", state10);
        auto *exit_field0_addr10 =
            exit10->create_instruction<CoreIrGetElementPtrInst>(
                ptr_i32_type10, "field0.addr.exit", exit_state_addr10,
                std::vector<CoreIrValue *>{zero10, zero10});
        auto *exit_load10 = exit10->create_instruction<CoreIrLoadInst>(
            i32_type10, "field0.exit", exit_field0_addr10);
        exit10->create_instruction<CoreIrReturnInst>(void_type10, exit_load10);
        iv10->add_incoming(entry10, zero10);
        iv10->add_incoming(join10, next_iv10);

        CompilerContext compiler_context10;
        compiler_context10.set_core_ir_build_result(
            std::make_unique<CoreIrBuildResult>(std::move(context10), module10));
        CoreIrLcssaPass lcssa10;
        assert(lcssa10.Run(compiler_context10).ok);
        CoreIrLoopMemoryPromotionPass pass10;
        assert(pass10.Run(compiler_context10).ok);
        assert_module_verifies(*module10);

        const std::string text10 = printer.print_module(*module10);
        assert(text10.find("%field0.load = load i32") == std::string::npos);
        assert(text10.find("%field0.exit = load i32") == std::string::npos);
        assert(text10.find("store [2 x i32] zeroinitializer, %state") !=
               std::string::npos);
        assert(block_has_phi(*join10));
    }
    {
        auto context11 = std::make_unique<CoreIrContext>();
        auto *void_type11 = context11->create_type<CoreIrVoidType>();
        auto *i1_type11 = context11->create_type<CoreIrIntegerType>(1);
        auto *i8_type11 = context11->create_type<CoreIrIntegerType>(8);
        auto *i32_type11 = context11->create_type<CoreIrIntegerType>(32);
        auto *ptr_i8_type11 = context11->create_type<CoreIrPointerType>(i8_type11);
        auto *struct_type11 = context11->create_type<CoreIrStructType>(
            std::vector<const CoreIrType *>{ptr_i8_type11, i32_type11});
        auto *ptr_struct_type11 =
            context11->create_type<CoreIrPointerType>(struct_type11);
        auto *function_type11 = context11->create_type<CoreIrFunctionType>(
            i32_type11, std::vector<const CoreIrType *>{}, false);
        auto *sink_type11 = context11->create_type<CoreIrFunctionType>(
            void_type11, std::vector<const CoreIrType *>{ptr_struct_type11}, false);
        auto *module11 = context11->create_module<CoreIrModule>(
            "ir_core_loop_memory_promotion_null_unit_seed");
        auto *function11 =
            module11->create_function<CoreIrFunction>("main", function_type11, false);
        auto *entry11 = function11->create_basic_block<CoreIrBasicBlock>("entry");
        auto *header11 = function11->create_basic_block<CoreIrBasicBlock>("header");
        auto *body11 = function11->create_basic_block<CoreIrBasicBlock>("body");
        auto *exit11 = function11->create_basic_block<CoreIrBasicBlock>("exit");
        auto *state11 =
            function11->create_stack_slot<CoreIrStackSlot>("state", struct_type11, 8);
        auto *unrelated11 =
            function11->create_stack_slot<CoreIrStackSlot>("unrelated", i32_type11, 4);
        auto *zero11 = context11->create_constant<CoreIrConstantInt>(i32_type11, 0);
        auto *one11 = context11->create_constant<CoreIrConstantInt>(i32_type11, 1);
        auto *two11 = context11->create_constant<CoreIrConstantInt>(i32_type11, 2);
        auto *seven11 = context11->create_constant<CoreIrConstantInt>(i32_type11, 7);
        auto *null11 =
            context11->create_constant<CoreIrConstantNull>(ptr_i8_type11);

        auto *entry_state_addr11 =
            entry11->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_struct_type11, "state.addr", state11);
        auto *entry_ptr_addr11 =
            entry11->create_instruction<CoreIrGetElementPtrInst>(
                context11->create_type<CoreIrPointerType>(ptr_i8_type11),
                "state.ptr.addr", entry_state_addr11,
                std::vector<CoreIrValue *>{zero11, zero11});
        entry11->create_instruction<CoreIrStoreInst>(void_type11, null11,
                                                     entry_ptr_addr11);
        entry11->create_instruction<CoreIrStoreInst>(void_type11, seven11,
                                                     unrelated11);
        auto *unrelated_load11 = entry11->create_instruction<CoreIrLoadInst>(
            i32_type11, "unrelated.load", unrelated11);
        entry11->create_instruction<CoreIrJumpInst>(void_type11, header11);

        auto *iv11 = header11->create_instruction<CoreIrPhiInst>(i32_type11, "iv");
        auto *cmp11 = header11->create_instruction<CoreIrCompareInst>(
            CoreIrComparePredicate::SignedLess, i1_type11, "cmp", iv11, two11);
        header11->create_instruction<CoreIrCondJumpInst>(void_type11, cmp11, body11,
                                                         exit11);

        auto *loop_state_addr11 =
            body11->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_struct_type11, "state.addr.loop", state11);
        auto *loop_ptr_addr11 = body11->create_instruction<CoreIrGetElementPtrInst>(
            context11->create_type<CoreIrPointerType>(ptr_i8_type11),
            "state.ptr.addr.loop", loop_state_addr11,
            std::vector<CoreIrValue *>{zero11, zero11});
        auto *ptr_load11 = body11->create_instruction<CoreIrLoadInst>(
            ptr_i8_type11, "state.ptr.load", loop_ptr_addr11);
        body11->create_instruction<CoreIrStoreInst>(void_type11, ptr_load11,
                                                    loop_ptr_addr11);
        auto *next_iv11 = body11->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type11, "iv.next", iv11, one11);
        body11->create_instruction<CoreIrJumpInst>(void_type11, header11);

        auto *exit_state_addr11 =
            exit11->create_instruction<CoreIrAddressOfStackSlotInst>(
                ptr_struct_type11, "state.addr.exit", state11);
        exit11->create_instruction<CoreIrCallInst>(
            void_type11, "", "sink", sink_type11,
            std::vector<CoreIrValue *>{exit_state_addr11});
        exit11->create_instruction<CoreIrReturnInst>(void_type11,
                                                     unrelated_load11);
        iv11->add_incoming(entry11, zero11);
        iv11->add_incoming(body11, next_iv11);

        CompilerContext compiler_context11;
        compiler_context11.set_core_ir_build_result(
            std::make_unique<CoreIrBuildResult>(std::move(context11), module11));
        CoreIrLcssaPass lcssa11;
        assert(lcssa11.Run(compiler_context11).ok);
        CoreIrLoopMemoryPromotionPass pass11;
        assert(pass11.Run(compiler_context11).ok);
        assert_module_verifies(*module11);

        const std::string text11 = printer.print_module(*module11);
        assert(text11.find("%state.ptr.load = load ptr") == std::string::npos);
        assert(text11.find("%state.loop.") != std::string::npos);
    }
    return 0;
}
