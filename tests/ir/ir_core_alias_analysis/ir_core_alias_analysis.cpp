#include <cassert>
#include <memory>

#include "backend/ir/analysis/alias_analysis.hpp"
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

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type, ptr_i32_type},
        false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_alias_analysis");
    auto *global = module->create_global<CoreIrGlobal>("g", i32_type, nullptr,
                                                       false, false);
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *parameter0 =
        function->create_parameter<CoreIrParameter>(ptr_i32_type, "arg0");
    auto *parameter1 =
        function->create_parameter<CoreIrParameter>(ptr_i32_type, "arg1");
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot_a =
        function->create_stack_slot<CoreIrStackSlot>("a", i32_type, 4);
    auto *slot_b =
        function->create_stack_slot<CoreIrStackSlot>("b", i32_type, 4);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);

    auto *addr_a = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_a", slot_a);
    auto *addr_b = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_b", slot_b);
    auto *addr_g = entry->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_i32_type, "addr_g", global);
    auto *gep_a0 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep_a0", addr_a, std::vector<CoreIrValue *>{zero});
    auto *gep_a1 = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep_a1", addr_a, std::vector<CoreIrValue *>{one});
    auto *arg_gep = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "arg_gep", parameter0, std::vector<CoreIrValue *>{one});
    auto *dyn_index =
        entry->create_instruction<CoreIrLoadInst>(i32_type, "dyn", parameter0);
    auto *unknown_gep = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "unknown_gep", parameter0,
        std::vector<CoreIrValue *>{dyn_index});
    entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    CoreIrAliasAnalysis analysis;
    const CoreIrAliasAnalysisResult result = analysis.Run(*function);

    assert(result.alias_values(addr_a, addr_b) == CoreIrAliasKind::NoAlias);
    assert(result.alias_values(addr_a, addr_g) == CoreIrAliasKind::NoAlias);
    assert(result.alias_values(addr_a, parameter0) == CoreIrAliasKind::NoAlias);
    assert(result.alias_values(addr_a, gep_a0) == CoreIrAliasKind::MustAlias);
    assert(result.alias_values(addr_a, gep_a1) == CoreIrAliasKind::MayAlias);
    assert(result.alias_values(parameter0, parameter1) ==
           CoreIrAliasKind::MayAlias);
    assert(result.alias_values(parameter0, addr_g) ==
           CoreIrAliasKind::MayAlias);
    assert(result.alias_values(arg_gep, unknown_gep) ==
           CoreIrAliasKind::MayAlias);
    return 0;
}
