#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/analysis/call_graph_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_call_graph");
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    auto *bar = module->create_function<CoreIrFunction>("bar", function_type, true);
    auto *bar_entry = bar->create_basic_block<CoreIrBasicBlock>("entry");
    bar_entry->create_instruction<CoreIrReturnInst>(void_type, seven);

    auto *foo = module->create_function<CoreIrFunction>("foo", function_type, true);
    auto *foo_entry = foo->create_basic_block<CoreIrBasicBlock>("entry");
    foo_entry->create_instruction<CoreIrCallInst>(
        i32_type, "foo.call", "bar", function_type, std::vector<CoreIrValue *>{});
    foo_entry->create_instruction<CoreIrReturnInst>(void_type, seven);

    auto *rec = module->create_function<CoreIrFunction>("rec", function_type, true);
    auto *rec_entry = rec->create_basic_block<CoreIrBasicBlock>("entry");
    rec_entry->create_instruction<CoreIrCallInst>(
        i32_type, "rec.call", "rec", function_type, std::vector<CoreIrValue *>{});
    rec_entry->create_instruction<CoreIrReturnInst>(void_type, seven);

    auto *main_fn =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
    main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "main.call", "foo", function_type, std::vector<CoreIrValue *>{});
    main_entry->create_instruction<CoreIrReturnInst>(void_type, seven);

    CoreIrCallGraphAnalysis analysis;
    CoreIrCallGraphAnalysisResult result = analysis.Run(*module);

    assert(result.get_callees(main_fn).size() == 1);
    assert(result.get_callees(main_fn).front() == foo);
    assert(result.get_callers(foo).size() == 1);
    assert(result.get_callers(foo).front() == main_fn);
    assert(result.get_callees(foo).size() == 1);
    assert(result.get_callees(foo).front() == bar);
    assert(result.get_callers(bar).size() == 1);
    assert(result.get_callers(bar).front() == foo);
    assert(result.get_callsites_for_callee(rec).size() == 1);
    assert(!result.is_recursive(foo));
    assert(result.is_recursive(rec));
    return 0;
}
