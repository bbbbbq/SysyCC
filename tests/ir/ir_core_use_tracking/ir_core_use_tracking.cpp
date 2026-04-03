#include <cassert>
#include <string>
#include <vector>

#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"

using namespace sysycc;

int main() {
    CoreIrContext context;

    const auto *void_type = context.create_type<CoreIrVoidType>();
    const auto *i1_type = context.create_type<CoreIrIntegerType>(1);
    const auto *control_flow_type = context.create_type<CoreIrFunctionType>(
        void_type, std::vector<const CoreIrType *>{i1_type}, false);

    auto *module = context.create_module<CoreIrModule>("control_flow");
    auto *function = module->create_function<CoreIrFunction>(
        "branching", control_flow_type, true);
    auto *condition =
        function->create_parameter<CoreIrParameter>(i1_type, "cond");

    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *then_block = function->create_basic_block<CoreIrBasicBlock>("then");
    auto *else_block = function->create_basic_block<CoreIrBasicBlock>("else");
    auto *merge_block = function->create_basic_block<CoreIrBasicBlock>("merge");

    entry->create_instruction<CoreIrCondJumpInst>(void_type, condition, then_block,
                                                  else_block);
    then_block->create_instruction<CoreIrJumpInst>(void_type, merge_block);
    else_block->create_instruction<CoreIrJumpInst>(void_type, merge_block);
    merge_block->create_instruction<CoreIrReturnInst>(void_type);

    assert(condition->get_uses().size() == 1);
    assert(entry->get_has_terminator());
    assert(then_block->get_has_terminator());
    assert(else_block->get_has_terminator());
    assert(merge_block->get_has_terminator());

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("func @branching(i1 %cond) -> void internal {\n") !=
           std::string::npos);
    assert(text.find("  br i1 %cond, label %then, label %else\n") !=
           std::string::npos);
    assert(text.find("  jmp %merge\n") != std::string::npos);
    assert(text.find("  ret void\n") != std::string::npos);
    return 0;
}
