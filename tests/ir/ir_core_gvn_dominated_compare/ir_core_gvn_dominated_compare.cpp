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
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i1_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_gvn_dominated_compare");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *child = function->create_basic_block<CoreIrBasicBlock>("child");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);

    auto *cmp0 = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cmp0", one, one);
    entry->create_instruction<CoreIrJumpInst>(void_type, child);
    auto *cmp1 = child->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cmp1", one, one);
    child->create_instruction<CoreIrReturnInst>(void_type, cmp1);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrGvnPass pass;
    assert(pass.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%cmp1 = icmp") == std::string::npos);
    assert(text.find("ret i1 %cmp0") != std::string::npos);
    return 0;
}
