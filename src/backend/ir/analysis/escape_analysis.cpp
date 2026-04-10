#include "backend/ir/analysis/escape_analysis.hpp"

#include <unordered_set>

#include "backend/ir/analysis/function_attrs_analysis.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

bool value_has_pointer_type(const CoreIrValue *value) noexcept {
    return value != nullptr && value->get_type() != nullptr &&
           value->get_type()->get_kind() == CoreIrTypeKind::Pointer;
}

const CoreIrFunctionAttrsSummary *get_direct_callee_summary(
    const CoreIrCallInst &call,
    const CoreIrFunctionAttrsAnalysisResult *function_attrs) {
    if (function_attrs == nullptr || !call.get_is_direct_call()) {
        return nullptr;
    }

    const CoreIrInstruction *instruction = &call;
    const CoreIrBasicBlock *parent_block =
        instruction == nullptr ? nullptr : instruction->get_parent();
    const CoreIrFunction *parent_function =
        parent_block == nullptr ? nullptr : parent_block->get_parent();
    const CoreIrModule *module =
        parent_function == nullptr ? nullptr : parent_function->get_parent();
    if (module == nullptr) {
        return nullptr;
    }

    const CoreIrFunction *callee =
        module->find_function(call.get_callee_name());
    return callee == nullptr ? nullptr : function_attrs->get_summary(callee);
}

CoreIrEscapeKind classify_value_escape(
    const CoreIrValue *value,
    const CoreIrFunctionAttrsAnalysisResult *function_attrs,
    std::unordered_map<const CoreIrValue *, CoreIrEscapeKind> &cache,
    std::unordered_set<const CoreIrValue *> &visiting);

CoreIrEscapeKind classify_call_argument_escape(
    const CoreIrValue *value, const CoreIrCallInst &call,
    std::size_t argument_index,
    const CoreIrFunctionAttrsAnalysisResult *function_attrs,
    std::unordered_map<const CoreIrValue *, CoreIrEscapeKind> &cache,
    std::unordered_set<const CoreIrValue *> &visiting) {
    const CoreIrFunctionAttrsSummary *summary =
        get_direct_callee_summary(call, function_attrs);
    if (summary == nullptr) {
        return CoreIrEscapeKind::CapturedByCall;
    }

    const bool nocapture =
        argument_index < summary->parameter_nocapture.size() &&
        summary->parameter_nocapture[argument_index];
    CoreIrEscapeKind result = nocapture ? CoreIrEscapeKind::NoEscape
                                        : CoreIrEscapeKind::CapturedByCall;

    if (summary->returned_parameter_index.has_value() &&
        *summary->returned_parameter_index == argument_index &&
        value_has_pointer_type(&call)) {
        result = merge_core_ir_escape_kind(
            result,
            classify_value_escape(&call, function_attrs, cache, visiting));
    }
    return result;
}

CoreIrEscapeKind classify_use_escape(
    const CoreIrValue *value, const CoreIrUse &use,
    const CoreIrFunctionAttrsAnalysisResult *function_attrs,
    std::unordered_map<const CoreIrValue *, CoreIrEscapeKind> &cache,
    std::unordered_set<const CoreIrValue *> &visiting) {
    CoreIrInstruction *user = use.get_user();
    if (value == nullptr || user == nullptr) {
        return CoreIrEscapeKind::NoEscape;
    }

    if (auto *load = dynamic_cast<CoreIrLoadInst *>(user); load != nullptr) {
        return load->get_address() == value ? CoreIrEscapeKind::NoEscape
                                            : CoreIrEscapeKind::Unknown;
    }

    if (auto *store = dynamic_cast<CoreIrStoreInst *>(user); store != nullptr) {
        if (store->get_value() == value) {
            return CoreIrEscapeKind::CapturedByStore;
        }
        return store->get_address() == value ||
                       store->get_stack_slot() != nullptr
                   ? CoreIrEscapeKind::NoEscape
                   : CoreIrEscapeKind::Unknown;
    }

    if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(user);
        gep != nullptr) {
        return gep->get_base() == value
                   ? classify_value_escape(gep, function_attrs, cache, visiting)
                   : CoreIrEscapeKind::Unknown;
    }

    if (auto *phi = dynamic_cast<CoreIrPhiInst *>(user); phi != nullptr) {
        return classify_value_escape(phi, function_attrs, cache, visiting);
    }

    if (auto *cast = dynamic_cast<CoreIrCastInst *>(user); cast != nullptr) {
        if (cast->get_operand() != value) {
            return CoreIrEscapeKind::Unknown;
        }
        return value_has_pointer_type(cast)
                   ? classify_value_escape(cast, function_attrs, cache,
                                           visiting)
                   : CoreIrEscapeKind::Unknown;
    }

    if (auto *call = dynamic_cast<CoreIrCallInst *>(user); call != nullptr) {
        const std::size_t begin = call->get_argument_begin_index();
        if (use.get_operand_index() < begin) {
            return CoreIrEscapeKind::CapturedByCall;
        }
        return classify_call_argument_escape(value, *call,
                                             use.get_operand_index() - begin,
                                             function_attrs, cache, visiting);
    }

    if (auto *ret = dynamic_cast<CoreIrReturnInst *>(user); ret != nullptr) {
        return ret->get_return_value() == value ? CoreIrEscapeKind::Returned
                                                : CoreIrEscapeKind::Unknown;
    }

    if (dynamic_cast<CoreIrCompareInst *>(user) != nullptr ||
        dynamic_cast<CoreIrCondJumpInst *>(user) != nullptr) {
        return CoreIrEscapeKind::NoEscape;
    }

    if (dynamic_cast<CoreIrBinaryInst *>(user) != nullptr ||
        dynamic_cast<CoreIrUnaryInst *>(user) != nullptr ||
        dynamic_cast<CoreIrJumpInst *>(user) != nullptr) {
        return CoreIrEscapeKind::NoEscape;
    }

    return CoreIrEscapeKind::Unknown;
}

CoreIrEscapeKind classify_value_escape(
    const CoreIrValue *value,
    const CoreIrFunctionAttrsAnalysisResult *function_attrs,
    std::unordered_map<const CoreIrValue *, CoreIrEscapeKind> &cache,
    std::unordered_set<const CoreIrValue *> &visiting) {
    if (value == nullptr || !value_has_pointer_type(value)) {
        return CoreIrEscapeKind::NoEscape;
    }

    auto cache_it = cache.find(value);
    if (cache_it != cache.end()) {
        return cache_it->second;
    }
    if (!visiting.insert(value).second) {
        return CoreIrEscapeKind::NoEscape;
    }

    CoreIrEscapeKind result = CoreIrEscapeKind::NoEscape;
    for (const CoreIrUse &use : value->get_uses()) {
        result = merge_core_ir_escape_kind(
            result,
            classify_use_escape(value, use, function_attrs, cache, visiting));
    }

    visiting.erase(value);
    cache.emplace(value, result);
    return result;
}

} // namespace

CoreIrEscapeKind merge_core_ir_escape_kind(CoreIrEscapeKind lhs,
                                           CoreIrEscapeKind rhs) noexcept {
    return static_cast<unsigned char>(lhs) >= static_cast<unsigned char>(rhs)
               ? lhs
               : rhs;
}

CoreIrEscapeAnalysisResult::CoreIrEscapeAnalysisResult(
    const CoreIrFunction *function,
    std::unordered_map<const CoreIrValue *, CoreIrEscapeKind>
        value_escape_kinds,
    std::unordered_map<const CoreIrStackSlot *, CoreIrEscapeKind>
        stack_slot_escape_kinds,
    std::unordered_map<const CoreIrParameter *, CoreIrEscapeKind>
        parameter_escape_kinds,
    std::unordered_map<const CoreIrGlobal *, CoreIrEscapeKind>
        global_escape_kinds) noexcept
    : function_(function), value_escape_kinds_(std::move(value_escape_kinds)),
      stack_slot_escape_kinds_(std::move(stack_slot_escape_kinds)),
      parameter_escape_kinds_(std::move(parameter_escape_kinds)),
      global_escape_kinds_(std::move(global_escape_kinds)) {}

CoreIrEscapeKind CoreIrEscapeAnalysisResult::get_escape_kind_for_value(
    const CoreIrValue *value) const noexcept {
    auto value_it = value_escape_kinds_.find(value);
    if (value_it != value_escape_kinds_.end()) {
        return value_it->second;
    }

    const CoreIrMemoryLocation location = describe_memory_location(value);
    return get_escape_kind_for_location(location);
}

CoreIrEscapeKind CoreIrEscapeAnalysisResult::get_escape_kind_for_location(
    const CoreIrMemoryLocation &location) const noexcept {
    switch (location.kind) {
    case CoreIrMemoryLocationKind::StackSlot: {
        auto it = stack_slot_escape_kinds_.find(location.stack_slot);
        return it == stack_slot_escape_kinds_.end() ? CoreIrEscapeKind::Unknown
                                                    : it->second;
    }
    case CoreIrMemoryLocationKind::ArgumentDerived: {
        auto it = parameter_escape_kinds_.find(location.parameter);
        return it == parameter_escape_kinds_.end() ? CoreIrEscapeKind::Unknown
                                                   : it->second;
    }
    case CoreIrMemoryLocationKind::Global: {
        auto it = global_escape_kinds_.find(location.global);
        return it == global_escape_kinds_.end() ? CoreIrEscapeKind::Unknown
                                                : it->second;
    }
    case CoreIrMemoryLocationKind::Unknown:
        return CoreIrEscapeKind::Unknown;
    }
    return CoreIrEscapeKind::Unknown;
}

bool CoreIrEscapeAnalysisResult::is_non_escaping_location(
    const CoreIrMemoryLocation &location) const noexcept {
    return location.kind == CoreIrMemoryLocationKind::StackSlot &&
           get_escape_kind_for_location(location) == CoreIrEscapeKind::NoEscape;
}

CoreIrEscapeAnalysisResult CoreIrEscapeAnalysis::Run(
    const CoreIrFunction &function,
    const CoreIrFunctionAttrsAnalysisResult *function_attrs) const {
    std::unordered_map<const CoreIrValue *, CoreIrEscapeKind>
        value_escape_kinds;
    std::unordered_map<const CoreIrStackSlot *, CoreIrEscapeKind>
        stack_slot_escape_kinds;
    std::unordered_map<const CoreIrParameter *, CoreIrEscapeKind>
        parameter_escape_kinds;
    std::unordered_map<const CoreIrGlobal *, CoreIrEscapeKind>
        global_escape_kinds;
    std::unordered_set<const CoreIrValue *> visiting;

    for (const auto &parameter_ptr : function.get_parameters()) {
        const CoreIrParameter *parameter = parameter_ptr.get();
        if (parameter == nullptr || !value_has_pointer_type(parameter)) {
            continue;
        }
        const CoreIrEscapeKind escape_kind = classify_value_escape(
            parameter, function_attrs, value_escape_kinds, visiting);
        parameter_escape_kinds.emplace(parameter, escape_kind);
    }

    for (const auto &stack_slot_ptr : function.get_stack_slots()) {
        const CoreIrStackSlot *stack_slot = stack_slot_ptr.get();
        if (stack_slot != nullptr) {
            stack_slot_escape_kinds.emplace(stack_slot,
                                            CoreIrEscapeKind::NoEscape);
        }
    }

    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block_ptr->get_instructions()) {
            const CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr ||
                !value_has_pointer_type(instruction)) {
                continue;
            }

            const CoreIrEscapeKind escape_kind = classify_value_escape(
                instruction, function_attrs, value_escape_kinds, visiting);
            const CoreIrMemoryLocation location =
                describe_memory_location(instruction);
            switch (location.kind) {
            case CoreIrMemoryLocationKind::StackSlot:
                stack_slot_escape_kinds[location.stack_slot] =
                    merge_core_ir_escape_kind(
                        stack_slot_escape_kinds[location.stack_slot],
                        escape_kind);
                break;
            case CoreIrMemoryLocationKind::ArgumentDerived:
                parameter_escape_kinds[location.parameter] =
                    merge_core_ir_escape_kind(
                        parameter_escape_kinds[location.parameter],
                        escape_kind);
                break;
            case CoreIrMemoryLocationKind::Global:
                global_escape_kinds[location.global] =
                    merge_core_ir_escape_kind(
                        global_escape_kinds[location.global], escape_kind);
                break;
            case CoreIrMemoryLocationKind::Unknown:
                break;
            }
        }
    }

    return CoreIrEscapeAnalysisResult(&function, std::move(value_escape_kinds),
                                      std::move(stack_slot_escape_kinds),
                                      std::move(parameter_escape_kinds),
                                      std::move(global_escape_kinds));
}

} // namespace sysycc
