#include "backend/ir/analysis/call_graph_analysis.hpp"

#include <algorithm>

#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"

namespace sysycc {

namespace {

template <typename T>
void append_unique(std::vector<T *> &items, T *item) {
    if (item == nullptr ||
        std::find(items.begin(), items.end(), item) != items.end()) {
        return;
    }
    items.push_back(item);
}

bool function_reaches_itself(
    const CoreIrFunction *origin, const CoreIrFunction *current,
    const std::unordered_map<const CoreIrFunction *, std::vector<CoreIrFunction *>>
        &callees,
    std::unordered_set<const CoreIrFunction *> &visiting) {
    if (current == nullptr || !visiting.insert(current).second) {
        return false;
    }

    auto it = callees.find(current);
    if (it == callees.end()) {
        return false;
    }
    for (CoreIrFunction *callee : it->second) {
        if (callee == origin) {
            return true;
        }
        if (function_reaches_itself(origin, callee, callees, visiting)) {
            return true;
        }
    }
    return false;
}

} // namespace

CoreIrCallGraphAnalysisResult::CoreIrCallGraphAnalysisResult(
    const CoreIrModule *module,
    std::unordered_map<const CoreIrFunction *, std::vector<CoreIrFunction *>>
        callees,
    std::unordered_map<const CoreIrFunction *, std::vector<CoreIrFunction *>>
        callers,
    std::unordered_map<const CoreIrFunction *, std::vector<CoreIrCallInst *>>
        callsites_by_callee,
    std::unordered_set<const CoreIrFunction *> recursive_functions) noexcept
    : module_(module), callees_(std::move(callees)), callers_(std::move(callers)),
      callsites_by_callee_(std::move(callsites_by_callee)),
      recursive_functions_(std::move(recursive_functions)) {}

const std::vector<CoreIrFunction *> &
CoreIrCallGraphAnalysisResult::get_callees(
    const CoreIrFunction *function) const noexcept {
    static const std::vector<CoreIrFunction *> empty;
    auto it = callees_.find(function);
    return it == callees_.end() ? empty : it->second;
}

const std::vector<CoreIrFunction *> &
CoreIrCallGraphAnalysisResult::get_callers(
    const CoreIrFunction *function) const noexcept {
    static const std::vector<CoreIrFunction *> empty;
    auto it = callers_.find(function);
    return it == callers_.end() ? empty : it->second;
}

const std::vector<CoreIrCallInst *> &
CoreIrCallGraphAnalysisResult::get_callsites_for_callee(
    const CoreIrFunction *function) const noexcept {
    static const std::vector<CoreIrCallInst *> empty;
    auto it = callsites_by_callee_.find(function);
    return it == callsites_by_callee_.end() ? empty : it->second;
}

bool CoreIrCallGraphAnalysisResult::is_recursive(
    const CoreIrFunction *function) const noexcept {
    return function != nullptr &&
           recursive_functions_.find(function) != recursive_functions_.end();
}

CoreIrCallGraphAnalysisResult CoreIrCallGraphAnalysis::Run(
    CoreIrModule &module) const {
    std::unordered_map<const CoreIrFunction *, std::vector<CoreIrFunction *>> callees;
    std::unordered_map<const CoreIrFunction *, std::vector<CoreIrFunction *>> callers;
    std::unordered_map<const CoreIrFunction *, std::vector<CoreIrCallInst *>>
        callsites_by_callee;

    for (const auto &function_ptr : module.get_functions()) {
        CoreIrFunction *caller = function_ptr.get();
        if (caller == nullptr) {
            continue;
        }
        callees.emplace(caller, std::vector<CoreIrFunction *>{});
        callers.emplace(caller, std::vector<CoreIrFunction *>{});

        for (const auto &block_ptr : caller->get_basic_blocks()) {
            if (block_ptr == nullptr) {
                continue;
            }
            for (const auto &instruction_ptr : block_ptr->get_instructions()) {
                auto *call = dynamic_cast<CoreIrCallInst *>(instruction_ptr.get());
                if (call == nullptr || !call->get_is_direct_call()) {
                    continue;
                }
                CoreIrFunction *callee = module.find_function(call->get_callee_name());
                if (callee == nullptr) {
                    continue;
                }
                append_unique(callees[caller], callee);
                append_unique(callers[callee], caller);
                callsites_by_callee[callee].push_back(call);
            }
        }
    }

    std::unordered_set<const CoreIrFunction *> recursive_functions;
    for (const auto &function_ptr : module.get_functions()) {
        const CoreIrFunction *function = function_ptr.get();
        if (function == nullptr) {
            continue;
        }
        std::unordered_set<const CoreIrFunction *> visiting;
        if (function_reaches_itself(function, function, callees, visiting)) {
            recursive_functions.insert(function);
        }
    }

    return CoreIrCallGraphAnalysisResult(&module, std::move(callees),
                                         std::move(callers),
                                         std::move(callsites_by_callee),
                                         std::move(recursive_functions));
}

} // namespace sysycc
