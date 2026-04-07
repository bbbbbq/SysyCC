#include "backend/ir/analysis/function_attrs_analysis.hpp"

#include <unordered_set>

#include "backend/ir/analysis/call_graph_analysis.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"

namespace sysycc {

namespace {

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
        if (auto *load = dynamic_cast<CoreIrLoadInst *>(user); load != nullptr) {
            if (load->get_address() != value) {
                return false;
            }
            continue;
        }
        if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(user); gep != nullptr) {
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

CoreIrFunctionAttrsSummary summarize_function_attrs(
    CoreIrFunction &function, const CoreIrCallGraphAnalysisResult &call_graph) {
    CoreIrFunctionAttrsSummary summary;
    summary.memory_behavior =
        summarize_core_ir_function_effect(function).memory_behavior;
    summary.is_norecurse = !call_graph.is_recursive(&function);
    summary.parameter_nocapture.assign(function.get_parameters().size(), false);

    for (std::size_t index = 0; index < function.get_parameters().size(); ++index) {
        CoreIrParameter *parameter = function.get_parameters()[index].get();
        if (parameter == nullptr || parameter->get_type() == nullptr ||
            parameter->get_type()->get_kind() != CoreIrTypeKind::Pointer) {
            continue;
        }
        std::unordered_set<const CoreIrValue *> visiting;
        summary.parameter_nocapture[index] =
            value_tree_is_nocapture(parameter, visiting);
    }

    if (function.get_basic_blocks().size() != 1 ||
        function.get_basic_blocks().front() == nullptr) {
        return summary;
    }

    const CoreIrBasicBlock &block = *function.get_basic_blocks().front();
    if (block.get_instructions().empty()) {
        return summary;
    }
    auto *ret = dynamic_cast<CoreIrReturnInst *>(block.get_instructions().back().get());
    if (ret == nullptr || ret->get_return_value() == nullptr) {
        return summary;
    }
    if (auto *constant =
            dynamic_cast<const CoreIrConstant *>(ret->get_return_value());
        constant != nullptr) {
        summary.constant_return = constant;
        return summary;
    }

    for (std::size_t index = 0; index < function.get_parameters().size(); ++index) {
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
    CoreIrModule &module, const CoreIrCallGraphAnalysisResult &call_graph) const {
    std::unordered_map<const CoreIrFunction *, CoreIrFunctionAttrsSummary> summaries;
    for (const auto &function_ptr : module.get_functions()) {
        CoreIrFunction *function = function_ptr.get();
        if (function != nullptr) {
            summaries.emplace(function, summarize_function_attrs(*function, call_graph));
        }
    }
    return CoreIrFunctionAttrsAnalysisResult(&module, std::move(summaries));
}

} // namespace sysycc
