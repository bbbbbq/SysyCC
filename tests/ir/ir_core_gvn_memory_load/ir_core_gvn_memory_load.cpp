#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/gvn/core_ir_gvn_pass.hpp"
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

std::size_t count_occurrences(const std::string &haystack,
                              const std::string &needle) {
    if (needle.empty()) {
        return 0;
    }

    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = haystack.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += needle.size();
    }
    return count;
}

} // namespace

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type_with_ptr = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_gvn_memory_load");
    auto *global_x = module->create_global<CoreIrGlobal>("x", i32_type, nullptr,
                                                         true, false);
    auto *global_y = module->create_global<CoreIrGlobal>("y", i32_type, nullptr,
                                                         true, false);
    auto *global_z = module->create_global<CoreIrGlobal>("z", i32_type, nullptr,
                                                         true, false);

    auto *reuse_function = module->create_function<CoreIrFunction>(
        "reuse_live_on_entry", function_type_with_ptr, false);
    auto *reuse_ptr =
        reuse_function->create_parameter<CoreIrParameter>(ptr_i32_type, "ptr");
    auto *reuse_entry =
        reuse_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *reuse_child =
        reuse_function->create_basic_block<CoreIrBasicBlock>("child");
    auto *first_load = reuse_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "live0", reuse_ptr);
    reuse_entry->create_instruction<CoreIrJumpInst>(void_type, reuse_child);
    auto *second_load = reuse_child->create_instruction<CoreIrLoadInst>(
        i32_type, "live1", reuse_ptr);
    auto *sum = reuse_child->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", first_load, second_load);
    reuse_child->create_instruction<CoreIrReturnInst>(void_type, sum);

    auto *forward_function = module->create_function<CoreIrFunction>(
        "forward_from_store", function_type, false);
    auto *forward_entry =
        forward_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *forward_slot = forward_function->create_stack_slot<CoreIrStackSlot>(
        "value", i32_type, 4);
    auto *store_value =
        context->create_constant<CoreIrConstantInt>(i32_type, 7);
    forward_entry->create_instruction<CoreIrStoreInst>(void_type, store_value,
                                                       forward_slot);
    auto *forwarded_load = forward_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "forwarded", forward_slot);
    forward_entry->create_instruction<CoreIrReturnInst>(void_type,
                                                        forwarded_load);

    auto *loop_function = module->create_function<CoreIrFunction>(
        "reuse_across_noalias_loop_phi", function_type, false);
    auto *loop_entry =
        loop_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *loop_header =
        loop_function->create_basic_block<CoreIrBasicBlock>("header");
    auto *loop_body =
        loop_function->create_basic_block<CoreIrBasicBlock>("body");
    auto *loop_exit =
        loop_function->create_basic_block<CoreIrBasicBlock>("exit");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *loop_store_value =
        context->create_constant<CoreIrConstantInt>(i32_type, 7);

    loop_entry->create_instruction<CoreIrJumpInst>(void_type, loop_header);
    auto *loop_iv =
        loop_header->create_instruction<CoreIrPhiInst>(i32_type, "iv");
    auto *x_addr = loop_header->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_i32_type, "x.addr", global_x);
    auto *y_addr = loop_header->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_i32_type, "y.addr", global_y);
    auto *loop_head_load = loop_header->create_instruction<CoreIrLoadInst>(
        i32_type, "head.load", x_addr);
    auto *loop_cmp = loop_header->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedLess, i1_type, "cmp", loop_iv, one);
    loop_header->create_instruction<CoreIrCondJumpInst>(void_type, loop_cmp,
                                                        loop_body, loop_exit);
    loop_body->create_instruction<CoreIrStoreInst>(void_type, loop_store_value,
                                                   y_addr);
    auto *loop_next = loop_body->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "iv.next", loop_iv, one);
    loop_body->create_instruction<CoreIrJumpInst>(void_type, loop_header);
    auto *loop_exit_load = loop_exit->create_instruction<CoreIrLoadInst>(
        i32_type, "exit.load", x_addr);
    auto *loop_sum = loop_exit->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "loop.sum", loop_head_load,
        loop_exit_load);
    loop_exit->create_instruction<CoreIrReturnInst>(void_type, loop_sum);
    loop_iv->add_incoming(loop_entry, zero);
    loop_iv->add_incoming(loop_body, loop_next);

    auto *addr_function = module->create_function<CoreIrFunction>(
        "reuse_global_address_across_domination", function_type, false);
    auto *addr_entry =
        addr_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *addr_child =
        addr_function->create_basic_block<CoreIrBasicBlock>("child");
    auto *global_z_addr0 =
        addr_entry->create_instruction<CoreIrAddressOfGlobalInst>(
            ptr_i32_type, "z.addr.0", global_z);
    addr_entry->create_instruction<CoreIrJumpInst>(void_type, addr_child);
    auto *global_z_addr1 =
        addr_child->create_instruction<CoreIrAddressOfGlobalInst>(
            ptr_i32_type, "z.addr.1", global_z);
    auto *z_load = addr_child->create_instruction<CoreIrLoadInst>(
        i32_type, "z.load", global_z_addr1);
    addr_child->create_instruction<CoreIrReturnInst>(void_type, z_load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrGvnPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    const std::size_t reuse_offset = text.find("func @reuse_live_on_entry");
    const std::size_t forward_offset = text.find("func @forward_from_store");
    const std::size_t loop_offset =
        text.find("func @reuse_across_noalias_loop_phi");
    const std::size_t addr_offset =
        text.find("func @reuse_global_address_across_domination");
    assert(reuse_offset != std::string::npos);
    assert(forward_offset != std::string::npos);
    assert(loop_offset != std::string::npos);
    assert(addr_offset != std::string::npos);
    const std::string reuse_text =
        text.substr(reuse_offset, forward_offset - reuse_offset);
    const std::string forward_text =
        text.substr(forward_offset, loop_offset - forward_offset);
    const std::string loop_text = text.substr(loop_offset, addr_offset - loop_offset);
    const std::string addr_text = text.substr(addr_offset);

    assert(reuse_text.find("%live1 = load i32") == std::string::npos);
    assert(count_occurrences(reuse_text, "load i32") == 1);
    assert(reuse_text.find("%sum = add i32 %live0, %live0") !=
           std::string::npos);

    assert(forward_text.find("%forwarded = load i32") == std::string::npos);
    assert(forward_text.find("store i32 7, %value") != std::string::npos);

    assert(loop_text.find("%exit.load = load i32") == std::string::npos);
    assert(count_occurrences(loop_text, "load i32, %x.addr") == 1);
    assert(loop_text.find("%loop.sum = add i32 %head.load, %head.load") !=
           std::string::npos);

    assert(addr_text.find("%z.addr.1 = addr_of_global") == std::string::npos);
    assert(addr_text.find("load i32, %z.addr.0") != std::string::npos);
    return 0;
}
