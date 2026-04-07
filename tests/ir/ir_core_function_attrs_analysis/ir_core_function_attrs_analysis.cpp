#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/analysis/call_graph_analysis.hpp"
#include "backend/ir/analysis/function_attrs_analysis.hpp"
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
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *fn_i32 = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *fn_ptr = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_function_attrs");

    auto *pure = module->create_function<CoreIrFunction>("pure", fn_i32, true);
    auto *pure_entry = pure->create_basic_block<CoreIrBasicBlock>("entry");
    auto *five = context->create_constant<CoreIrConstantInt>(i32_type, 5);
    pure_entry->create_instruction<CoreIrReturnInst>(void_type, five);

    auto *reader = module->create_function<CoreIrFunction>("reader", fn_ptr, true);
    auto *reader_param = reader->create_parameter<CoreIrParameter>(ptr_i32_type, "p");
    auto *reader_entry = reader->create_basic_block<CoreIrBasicBlock>("entry");
    auto *load = reader_entry->create_instruction<CoreIrLoadInst>(i32_type, "load", reader_param);
    reader_entry->create_instruction<CoreIrReturnInst>(void_type, load);

    auto *recur = module->create_function<CoreIrFunction>("recur", fn_i32, true);
    auto *recur_entry = recur->create_basic_block<CoreIrBasicBlock>("entry");
    recur_entry->create_instruction<CoreIrCallInst>(i32_type, "self", "recur", fn_i32,
                                                    std::vector<CoreIrValue *>{});
    recur_entry->create_instruction<CoreIrReturnInst>(void_type, five);

    CoreIrCallGraphAnalysis call_graph_analysis;
    CoreIrCallGraphAnalysisResult call_graph = call_graph_analysis.Run(*module);
    CoreIrFunctionAttrsAnalysis attrs_analysis;
    CoreIrFunctionAttrsAnalysisResult attrs =
        attrs_analysis.Run(*module, call_graph);

    const CoreIrFunctionAttrsSummary *pure_attrs = attrs.get_summary(pure);
    assert(pure_attrs != nullptr);
    assert(pure_attrs->memory_behavior == CoreIrMemoryBehavior::None);
    assert(pure_attrs->is_norecurse);
    assert(pure_attrs->constant_return == five);

    const CoreIrFunctionAttrsSummary *reader_attrs = attrs.get_summary(reader);
    assert(reader_attrs != nullptr);
    assert(reader_attrs->memory_behavior == CoreIrMemoryBehavior::Read);
    assert(reader_attrs->parameter_nocapture.size() == 1);
    assert(reader_attrs->parameter_nocapture[0]);
    assert(reader_attrs->constant_return == nullptr);

    const CoreIrFunctionAttrsSummary *recur_attrs = attrs.get_summary(recur);
    assert(recur_attrs != nullptr);
    assert(!recur_attrs->is_norecurse);
    return 0;
}
