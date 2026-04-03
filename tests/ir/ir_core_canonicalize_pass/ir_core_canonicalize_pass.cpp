#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type =
        context->create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{}, false);
    CoreIrModule *module =
        context->create_module<CoreIrModule>("ir_core_canonicalize_pass");
    CoreIrFunction *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    CoreIrBasicBlock *entry =
        function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    CoreIrRawPrinter printer;
    const std::string before = printer.print_module(*module);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    CoreIrCanonicalizePass canonicalize_pass;
    assert(canonicalize_pass.Run(compiler_context).ok);
    assert(!compiler_context.get_diagnostic_engine().has_error());

    const std::string after = printer.print_module(*module);
    assert(before == after);
    return 0;
}
