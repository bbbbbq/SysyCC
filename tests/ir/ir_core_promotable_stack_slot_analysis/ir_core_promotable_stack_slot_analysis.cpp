#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/analysis/promotable_stack_slot_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

using namespace sysycc;

namespace {

const CoreIrPromotionUnitInfo *find_unit(
    const CoreIrPromotableStackSlotAnalysisResult &result,
    CoreIrStackSlot *slot, std::vector<std::uint64_t> path) {
    for (const CoreIrPromotionUnitInfo &unit_info : result.get_unit_infos()) {
        if (unit_info.unit.stack_slot == slot &&
            unit_info.unit.access_path == path) {
            return &unit_info;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *i64_type = context->create_type<CoreIrIntegerType>(64);
    auto *array2_i32 = context->create_type<CoreIrArrayType>(i32_type, 2);
    auto *struct_type = context->create_type<CoreIrStructType>(
        std::vector<const CoreIrType *>{i32_type, array2_i32});
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *ptr_struct_type = context->create_type<CoreIrPointerType>(struct_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_promotable_stack_slot_analysis");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");

    auto *scalar_slot =
        function->create_stack_slot<CoreIrStackSlot>("scalar", i32_type, 4);
    auto *aggregate_slot =
        function->create_stack_slot<CoreIrStackSlot>("aggregate", struct_type, 4);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    entry->create_instruction<CoreIrStoreInst>(void_type, one, scalar_slot);
    entry->create_instruction<CoreIrLoadInst>(i32_type, "scalar.load", scalar_slot);

    auto *aggregate_addr = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_struct_type, "aggregate.addr", aggregate_slot);
    auto *field0_addr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "field0.addr", aggregate_addr,
        std::vector<CoreIrValue *>{zero, zero});
    entry->create_instruction<CoreIrStoreInst>(void_type, two, field0_addr);
    entry->create_instruction<CoreIrLoadInst>(i32_type, "field0.load", field0_addr);

    auto *field1_base = entry->create_instruction<CoreIrGetElementPtrInst>(
        context->create_type<CoreIrPointerType>(array2_i32), "field1.base",
        aggregate_addr, std::vector<CoreIrValue *>{zero, one});
    auto *dynamic_index_slot =
        function->create_stack_slot<CoreIrStackSlot>("idx", i64_type, 8);
    entry->create_instruction<CoreIrStoreInst>(
        void_type, context->create_constant<CoreIrConstantInt>(i64_type, 1),
        dynamic_index_slot);
    auto *dynamic_index =
        entry->create_instruction<CoreIrLoadInst>(i64_type, "idx.load", dynamic_index_slot);
    auto *dynamic_addr = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "field1.dynamic", field1_base,
        std::vector<CoreIrValue *>{zero, dynamic_index});
    entry->create_instruction<CoreIrLoadInst>(i32_type, "field1.load", dynamic_addr);
    entry->create_instruction<CoreIrReturnInst>(void_type, one);

    CoreIrPromotableStackSlotAnalysis analysis;
    const CoreIrPromotableStackSlotAnalysisResult result = analysis.Run(*function);

    const auto *scalar_unit = find_unit(result, scalar_slot, {});
    const auto *field0_unit = find_unit(result, aggregate_slot, {0});
    assert(scalar_unit != nullptr);
    assert(field0_unit != nullptr);
    assert(find_unit(result, aggregate_slot, {1}) == nullptr);
    return 0;
}
