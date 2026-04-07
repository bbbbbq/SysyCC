#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/gvn/core_ir_gvn_pass.hpp"
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
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type_with_ptr = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_gvn_memory_load");

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
    auto *forward_slot =
        forward_function->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    forward_entry->create_instruction<CoreIrStoreInst>(void_type, seven, forward_slot);
    auto *forwarded_load = forward_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "forwarded", forward_slot);
    forward_entry->create_instruction<CoreIrReturnInst>(void_type, forwarded_load);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrGvnPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    const std::size_t reuse_offset = text.find("func @reuse_live_on_entry");
    const std::size_t forward_offset = text.find("func @forward_from_store");
    assert(reuse_offset != std::string::npos);
    assert(forward_offset != std::string::npos);
    const std::string reuse_text =
        text.substr(reuse_offset, forward_offset - reuse_offset);
    const std::string forward_text = text.substr(forward_offset);

    assert(reuse_text.find("%live1 = load i32") == std::string::npos);
    assert(count_occurrences(reuse_text, "load i32") == 1);
    assert(reuse_text.find("%sum = add i32 %live0, %live0") != std::string::npos);

    assert(forward_text.find("%forwarded = load i32") == std::string::npos);
    assert(forward_text.find("store i32 7, %value") != std::string::npos);
    return 0;
}
