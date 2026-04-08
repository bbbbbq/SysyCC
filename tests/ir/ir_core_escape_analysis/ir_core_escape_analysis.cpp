#include <cassert>
#include <memory>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/escape_analysis.hpp"
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

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *ptr_ptr_i32_type =
        context->create_type<CoreIrPointerType>(ptr_i32_type);
    auto *nocapture_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *capture_type = context->create_type<CoreIrFunctionType>(
        ptr_i32_type, std::vector<const CoreIrType *>{ptr_i32_type}, false);
    auto *main_type = context->create_type<CoreIrFunctionType>(
        ptr_i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("ir_core_escape_analysis");

    auto *nocapture = module->create_function<CoreIrFunction>(
        "nocapture_use", nocapture_type, false);
    auto *nocapture_param =
        nocapture->create_parameter<CoreIrParameter>(ptr_i32_type, "p");
    auto *nocapture_entry =
        nocapture->create_basic_block<CoreIrBasicBlock>("entry");
    auto *nocapture_load = nocapture_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "load", nocapture_param);
    nocapture_entry->create_instruction<CoreIrReturnInst>(void_type,
                                                          nocapture_load);

    auto *capture = module->create_function<CoreIrFunction>(
        "capture_return", capture_type, false);
    auto *capture_param =
        capture->create_parameter<CoreIrParameter>(ptr_i32_type, "p");
    auto *capture_entry =
        capture->create_basic_block<CoreIrBasicBlock>("entry");
    capture_entry->create_instruction<CoreIrReturnInst>(void_type,
                                                        capture_param);

    auto *main_function =
        module->create_function<CoreIrFunction>("main", main_type, false);
    auto *entry = main_function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *slot_a =
        main_function->create_stack_slot<CoreIrStackSlot>("a", i32_type, 4);
    auto *slot_b =
        main_function->create_stack_slot<CoreIrStackSlot>("b", i32_type, 4);
    auto *slot_c =
        main_function->create_stack_slot<CoreIrStackSlot>("c", i32_type, 4);
    auto *slot_d =
        main_function->create_stack_slot<CoreIrStackSlot>("d", i32_type, 4);
    auto *ptr_slot = main_function->create_stack_slot<CoreIrStackSlot>(
        "ptr_slot", ptr_i32_type, 8);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    auto *addr_a = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_a", slot_a);
    auto *addr_b = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_b", slot_b);
    auto *addr_c = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_c", slot_c);
    auto *addr_d = entry->create_instruction<CoreIrAddressOfStackSlotInst>(
        ptr_i32_type, "addr_d", slot_d);
    auto *addr_ptr_slot =
        entry->create_instruction<CoreIrAddressOfStackSlotInst>(
            ptr_ptr_i32_type, "addr_ptr_slot", ptr_slot);
    auto *gep_a = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_i32_type, "gep_a", addr_a, std::vector<CoreIrValue *>{zero});
    entry->create_instruction<CoreIrCompareInst>(CoreIrComparePredicate::Equal,
                                                 i1_type, "cmp", gep_a, addr_a);
    entry->create_instruction<CoreIrCallInst>(
        i32_type, "nocapture", "nocapture_use", nocapture_type,
        std::vector<CoreIrValue *>{addr_a});
    entry->create_instruction<CoreIrStoreInst>(void_type, addr_b,
                                               addr_ptr_slot);
    entry->create_instruction<CoreIrCallInst>(
        ptr_i32_type, "capture", "capture_return", capture_type,
        std::vector<CoreIrValue *>{addr_d});
    entry->create_instruction<CoreIrReturnInst>(void_type, addr_c);

    CoreIrAnalysisManager analysis_manager;
    const CoreIrEscapeAnalysisResult &escape =
        analysis_manager.get_or_compute<CoreIrEscapeAnalysis>(*main_function);

    assert(escape.get_escape_kind_for_value(addr_a) ==
           CoreIrEscapeKind::NoEscape);
    assert(escape.get_escape_kind_for_value(addr_b) ==
           CoreIrEscapeKind::CapturedByStore);
    assert(escape.get_escape_kind_for_value(addr_c) ==
           CoreIrEscapeKind::Returned);
    assert(escape.get_escape_kind_for_value(addr_d) ==
           CoreIrEscapeKind::CapturedByCall);
    return 0;
}
