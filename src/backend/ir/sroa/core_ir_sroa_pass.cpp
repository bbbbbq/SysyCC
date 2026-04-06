#include "backend/ir/sroa/core_ir_sroa_pass.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/promotable_stack_slot_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::erase_instruction;
using sysycc::detail::replace_instruction;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

bool erase_dead_address_chain(CoreIrBasicBlock &block, CoreIrValue *value) {
    bool changed = false;
    CoreIrInstruction *instruction = dynamic_cast<CoreIrInstruction *>(value);
    while (instruction != nullptr &&
           get_core_ir_instruction_effect(*instruction).is_pure_value &&
           instruction->get_uses().empty()) {
        CoreIrValue *next = nullptr;
        if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(instruction);
            gep != nullptr) {
            next = gep->get_base();
        }
        if (!erase_instruction(block, instruction)) {
            break;
        }
        changed = true;
        instruction = dynamic_cast<CoreIrInstruction *>(next);
    }
    return changed;
}

std::string build_sroa_slot_name(const CoreIrPromotionUnit &unit) {
    std::string name = unit.stack_slot == nullptr ? "sroa" : unit.stack_slot->get_name();
    for (std::uint64_t index : unit.access_path) {
        name += ".";
        name += std::to_string(index);
    }
    return name;
}

void remove_unused_stack_slots(CoreIrFunction &function) {
    auto &stack_slots = function.get_stack_slots();
    stack_slots.erase(
        std::remove_if(stack_slots.begin(), stack_slots.end(),
                       [&function](const std::unique_ptr<CoreIrStackSlot> &slot) {
                           if (slot == nullptr) {
                               return true;
                           }
                           for (const auto &block : function.get_basic_blocks()) {
                               if (block == nullptr) {
                                   continue;
                               }
                               for (const auto &instruction : block->get_instructions()) {
                                   if (instruction == nullptr) {
                                       continue;
                                   }
                                   if (auto *address =
                                           dynamic_cast<CoreIrAddressOfStackSlotInst *>(
                                               instruction.get());
                                       address != nullptr &&
                                       address->get_stack_slot() == slot.get()) {
                                       return false;
                                   }
                                   if (auto *load = dynamic_cast<CoreIrLoadInst *>(
                                           instruction.get());
                                       load != nullptr &&
                                       load->get_stack_slot() == slot.get()) {
                                       return false;
                                   }
                                   if (auto *store = dynamic_cast<CoreIrStoreInst *>(
                                           instruction.get());
                                       store != nullptr &&
                                       store->get_stack_slot() == slot.get()) {
                                       return false;
                                   }
                               }
                           }
                           return true;
                       }),
        stack_slots.end());
}

} // namespace

PassKind CoreIrSroaPass::Kind() const { return PassKind::CoreIrSroa; }

const char *CoreIrSroaPass::Name() const { return "CoreIrSroaPass"; }

PassResult CoreIrSroaPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir sroa dependencies");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrPromotableStackSlotAnalysisResult &promotable_units =
            analysis_manager->get_or_compute<CoreIrPromotableStackSlotAnalysis>(
                *function);

        std::unordered_map<const CoreIrPromotionUnitInfo *, CoreIrStackSlot *> unit_slots;
        for (const CoreIrPromotionUnitInfo &unit_info :
             promotable_units.get_unit_infos()) {
            if (unit_info.unit.kind != CoreIrPromotionUnitKind::AccessPath ||
                unit_info.unit.stack_slot == nullptr ||
                unit_info.unit.value_type == nullptr) {
                continue;
            }
            CoreIrStackSlot *new_slot =
                function->create_stack_slot<CoreIrStackSlot>(
                    build_sroa_slot_name(unit_info.unit), unit_info.unit.value_type,
                    unit_info.unit.stack_slot->get_alignment());
            unit_slots.emplace(&unit_info, new_slot);
        }
        if (unit_slots.empty()) {
            continue;
        }

        bool function_changed = false;
        for (const auto &unit_entry : unit_slots) {
            const CoreIrPromotionUnitInfo &unit_info = *unit_entry.first;
            CoreIrStackSlot *new_slot = unit_entry.second;

            for (CoreIrLoadInst *load : unit_info.loads) {
                if (load == nullptr || load->get_parent() == nullptr) {
                    continue;
                }
                CoreIrBasicBlock &block = *load->get_parent();
                CoreIrValue *address = load->get_address();
                auto replacement = std::make_unique<CoreIrLoadInst>(
                    load->get_type(), load->get_name(), new_slot);
                replacement->set_source_span(load->get_source_span());
                if (replace_instruction(block, load, std::move(replacement)) != nullptr) {
                    function_changed = true;
                    erase_dead_address_chain(block, address);
                }
            }

            for (CoreIrStoreInst *store : unit_info.stores) {
                if (store == nullptr || store->get_parent() == nullptr) {
                    continue;
                }
                CoreIrBasicBlock &block = *store->get_parent();
                CoreIrValue *address = store->get_address();
                auto replacement = std::make_unique<CoreIrStoreInst>(
                    store->get_type(), store->get_value(), new_slot);
                replacement->set_source_span(store->get_source_span());
                if (replace_instruction(block, store, std::move(replacement)) != nullptr) {
                    function_changed = true;
                    erase_dead_address_chain(block, address);
                }
            }
        }

        if (function_changed) {
            remove_unused_stack_slots(*function);
            effects.changed_functions.insert(function.get());
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }

    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    effects.preserved_analyses.preserve_cfg_family();
    effects.preserved_analyses.preserve_loop_family();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc

