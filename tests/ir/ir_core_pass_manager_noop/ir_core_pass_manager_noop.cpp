#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/core/ir_basic_block.hpp"
#include "backend/ir/core/ir_constant.hpp"
#include "backend/ir/core/ir_context.hpp"
#include "backend/ir/core/ir_function.hpp"
#include "backend/ir/core/ir_instruction.hpp"
#include "backend/ir/core/ir_module.hpp"
#include "backend/ir/core/ir_type.hpp"
#include "backend/ir/pass/core_ir_pass.hpp"
#include "backend/ir/printer/core_ir_raw_printer.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

using namespace sysycc;

int main() {
    CoreIrContext context;
    auto *void_type = context.create_type<CoreIrVoidType>();
    auto *i32_type = context.create_type<CoreIrIntegerType>(32);
    auto *function_type =
        context.create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{}, false);
    CoreIrModule *module =
        context.create_module<CoreIrModule>("ir_core_pass_manager_noop");
    CoreIrFunction *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    CoreIrBasicBlock *entry =
        function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context.create_constant<CoreIrConstantInt>(i32_type, 0);
    entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    CoreIrRawPrinter printer;
    const std::string before = printer.print_module(*module);

    DiagnosticEngine diagnostic_engine;
    CoreIrPassManager pass_manager;
    pass_manager.AddPass(std::make_unique<CoreIrNoOpPass>());
    assert(pass_manager.Run(*module, diagnostic_engine));
    assert(!diagnostic_engine.has_error());

    const std::string after = printer.print_module(*module);
    assert(before == after);
    return 0;
}
