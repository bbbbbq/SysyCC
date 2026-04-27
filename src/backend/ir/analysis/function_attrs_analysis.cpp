#include "backend/ir/analysis/function_attrs_analysis.hpp"

#include <unordered_set>

#include "backend/ir/analysis/call_graph_analysis.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"

namespace sysycc {

namespace {

bool value_may_derive_from_parameter(
    CoreIrFunction &function, CoreIrValue *value, CoreIrValue *parameter,
    std::unordered_set<const CoreIrValue *> &visiting_values,
    std::unordered_set<const CoreIrStackSlot *> &visiting_slots);

bool stack_slot_may_hold_parameter(
    CoreIrFunction &function, CoreIrStackSlot *stack_slot,
    CoreIrValue *parameter,
    std::unordered_set<const CoreIrValue *> &visiting_values,
    std::unordered_set<const CoreIrStackSlot *> &visiting_slots) {
    if (stack_slot == nullptr || !visiting_slots.insert(stack_slot).second) {
        return false;
    }

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            auto *store =
                dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
            if (store == nullptr || store->get_stack_slot() != stack_slot) {
                continue;
            }
            if (value_may_derive_from_parameter(function, store->get_value(),
                                                parameter, visiting_values,
                                                visiting_slots)) {
                return true;
            }
        }
    }
    return false;
}

bool value_may_derive_from_parameter(
    CoreIrFunction &function, CoreIrValue *value, CoreIrValue *parameter,
    std::unordered_set<const CoreIrValue *> &visiting_values,
    std::unordered_set<const CoreIrStackSlot *> &visiting_slots) {
    if (value == nullptr) {
        return false;
    }
    if (value == parameter) {
        return true;
    }
    if (!visiting_values.insert(value).second) {
        return false;
    }

    if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(value);
        gep != nullptr) {
        return value_may_derive_from_parameter(function, gep->get_base(),
                                               parameter, visiting_values,
                                               visiting_slots);
    }
    if (auto *cast = dynamic_cast<CoreIrCastInst *>(value); cast != nullptr) {
        return value_may_derive_from_parameter(function, cast->get_operand(),
                                               parameter, visiting_values,
                                               visiting_slots);
    }
    if (auto *phi = dynamic_cast<CoreIrPhiInst *>(value); phi != nullptr) {
        for (std::size_t index = 0; index < phi->get_incoming_count();
             ++index) {
            if (value_may_derive_from_parameter(
                    function, phi->get_incoming_value(index), parameter,
                    visiting_values, visiting_slots)) {
                return true;
            }
        }
        return false;
    }
    if (auto *select = dynamic_cast<CoreIrSelectInst *>(value);
        select != nullptr) {
        return value_may_derive_from_parameter(
                   function, select->get_true_value(), parameter,
                   visiting_values, visiting_slots) ||
               value_may_derive_from_parameter(
                   function, select->get_false_value(), parameter,
                   visiting_values, visiting_slots);
    }
    if (auto *load = dynamic_cast<CoreIrLoadInst *>(value); load != nullptr) {
        if (load->get_stack_slot() != nullptr) {
            return stack_slot_may_hold_parameter(
                function, load->get_stack_slot(), parameter, visiting_values,
                visiting_slots);
        }
        return false;
    }
    return false;
}

bool parameter_is_readonly(CoreIrFunction &function, CoreIrValue *parameter) {
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            auto *store =
                dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
            if (store == nullptr || store->get_stack_slot() != nullptr ||
                store->get_address() == nullptr) {
                continue;
            }
            std::unordered_set<const CoreIrValue *> visiting_values;
            std::unordered_set<const CoreIrStackSlot *> visiting_slots;
            if (value_may_derive_from_parameter(function, store->get_address(),
                                                parameter, visiting_values,
                                                visiting_slots)) {
                return false;
            }
        }
    }
    return true;
}

bool value_tree_is_nocapture(
    CoreIrValue *value, std::unordered_set<const CoreIrValue *> &visiting) {
    if (value == nullptr || !visiting.insert(value).second) {
        return true;
    }

    for (const CoreIrUse &use : value->get_uses()) {
        CoreIrInstruction *user = use.get_user();
        if (user == nullptr) {
            continue;
        }
        if (auto *load = dynamic_cast<CoreIrLoadInst *>(user);
            load != nullptr) {
            if (load->get_address() != value) {
                return false;
            }
            continue;
        }
        if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(user);
            gep != nullptr) {
            if (gep->get_base() != value ||
                !value_tree_is_nocapture(gep, visiting)) {
                return false;
            }
            continue;
        }
        return false;
    }

    return true;
}

bool value_tree_is_readonly(CoreIrValue *value,
                            std::unordered_set<const CoreIrValue *> &visiting) {
    if (value == nullptr || !visiting.insert(value).second) {
        return true;
    }

    for (const CoreIrUse &use : value->get_uses()) {
        CoreIrInstruction *user = use.get_user();
        if (user == nullptr) {
            continue;
        }
        if (auto *load = dynamic_cast<CoreIrLoadInst *>(user);
            load != nullptr) {
            if (load->get_address() != value) {
                return false;
            }
            continue;
        }
        if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(user);
            gep != nullptr) {
            if (gep->get_base() != value ||
                !value_tree_is_readonly(gep, visiting)) {
                return false;
            }
            continue;
        }
        if (auto *cast = dynamic_cast<CoreIrCastInst *>(user);
            cast != nullptr) {
            if (cast->get_operand() != value ||
                !value_tree_is_readonly(cast, visiting)) {
                return false;
            }
            continue;
        }
        if (auto *phi = dynamic_cast<CoreIrPhiInst *>(user); phi != nullptr) {
            if (!value_tree_is_readonly(phi, visiting)) {
                return false;
            }
            continue;
        }
        if (auto *select = dynamic_cast<CoreIrSelectInst *>(user);
            select != nullptr) {
            if (!value_tree_is_readonly(select, visiting)) {
                return false;
            }
            continue;
        }
        if (auto *store = dynamic_cast<CoreIrStoreInst *>(user);
            store != nullptr) {
            if (store->get_address() == value) {
                return false;
            }
            if (store->get_value() == value) {
                continue;
            }
            return false;
        }
        if (dynamic_cast<CoreIrCompareInst *>(user) != nullptr ||
            dynamic_cast<CoreIrCondJumpInst *>(user) != nullptr ||
            dynamic_cast<CoreIrJumpInst *>(user) != nullptr ||
            dynamic_cast<CoreIrReturnInst *>(user) != nullptr ||
            dynamic_cast<CoreIrBinaryInst *>(user) != nullptr ||
            dynamic_cast<CoreIrUnaryInst *>(user) != nullptr) {
            continue;
        }
        return false;
    }

    return true;
}

CoreIrFunctionAttrsSummary
summarize_function_attrs(CoreIrFunction &function,
                         const CoreIrCallGraphAnalysisResult &call_graph) {
    CoreIrFunctionAttrsSummary summary;
    summary.memory_behavior =
        summarize_core_ir_function_effect(function).memory_behavior;
    summary.is_norecurse = !call_graph.is_recursive(&function);
    summary.parameter_nocapture.assign(function.get_parameters().size(), false);
    summary.parameter_readonly.assign(function.get_parameters().size(), false);

    for (std::size_t index = 0; index < function.get_parameters().size();
         ++index) {
        CoreIrParameter *parameter = function.get_parameters()[index].get();
        if (parameter == nullptr || parameter->get_type() == nullptr ||
            parameter->get_type()->get_kind() != CoreIrTypeKind::Pointer) {
            continue;
        }
        std::unordered_set<const CoreIrValue *> visiting;
        summary.parameter_nocapture[index] =
            value_tree_is_nocapture(parameter, visiting);
        std::unordered_set<const CoreIrValue *> readonly_visiting;
        summary.parameter_readonly[index] =
            parameter_is_readonly(function, parameter) &&
            value_tree_is_readonly(parameter, readonly_visiting);
    }

    if (function.get_basic_blocks().size() != 1 ||
        function.get_basic_blocks().front() == nullptr) {
        return summary;
    }

    const CoreIrBasicBlock &block = *function.get_basic_blocks().front();
    if (block.get_instructions().empty()) {
        return summary;
    }
    auto *ret =
        dynamic_cast<CoreIrReturnInst *>(block.get_instructions().back().get());
    if (ret == nullptr || ret->get_return_value() == nullptr) {
        return summary;
    }
    if (auto *constant =
            dynamic_cast<const CoreIrConstant *>(ret->get_return_value());
        constant != nullptr) {
        summary.constant_return = constant;
        return summary;
    }

    for (std::size_t index = 0; index < function.get_parameters().size();
         ++index) {
        if (function.get_parameters()[index].get() == ret->get_return_value()) {
            summary.returned_parameter_index = index;
            break;
        }
    }
    return summary;
}

} // namespace

CoreIrFunctionAttrsAnalysisResult::CoreIrFunctionAttrsAnalysisResult(
    const CoreIrModule *module,
    std::unordered_map<const CoreIrFunction *, CoreIrFunctionAttrsSummary>
        summaries) noexcept
    : module_(module), summaries_(std::move(summaries)) {}

const CoreIrFunctionAttrsSummary *
CoreIrFunctionAttrsAnalysisResult::get_summary(
    const CoreIrFunction *function) const noexcept {
    auto it = summaries_.find(function);
    return it == summaries_.end() ? nullptr : &it->second;
}

CoreIrFunctionAttrsAnalysisResult CoreIrFunctionAttrsAnalysis::Run(
    CoreIrModule &module,
    const CoreIrCallGraphAnalysisResult &call_graph) const {
    std::unordered_map<const CoreIrFunction *, CoreIrFunctionAttrsSummary>
        summaries;
    for (const auto &function_ptr : module.get_functions()) {
        CoreIrFunction *function = function_ptr.get();
        if (function != nullptr) {
            summaries.emplace(function,
                              summarize_function_attrs(*function, call_graph));
        }
    }
    return CoreIrFunctionAttrsAnalysisResult(&module, std::move(summaries));
}

} // namespace sysycc
