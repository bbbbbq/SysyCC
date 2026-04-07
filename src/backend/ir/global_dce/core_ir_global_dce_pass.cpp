#include "backend/ir/global_dce/core_ir_global_dce_pass.hpp"

#include <string>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

void collect_constant_references(
    const CoreIrConstant *constant, std::vector<CoreIrFunction *> &functions,
    std::vector<CoreIrGlobal *> &globals) {
    if (constant == nullptr) {
        return;
    }
    if (const auto *address =
            dynamic_cast<const CoreIrConstantGlobalAddress *>(constant);
        address != nullptr) {
        if (address->get_function() != nullptr) {
            functions.push_back(address->get_function());
        }
        if (address->get_global() != nullptr) {
            globals.push_back(address->get_global());
        }
        return;
    }
    if (const auto *aggregate =
            dynamic_cast<const CoreIrConstantAggregate *>(constant);
        aggregate != nullptr) {
        for (const CoreIrConstant *element : aggregate->get_elements()) {
            collect_constant_references(element, functions, globals);
        }
        return;
    }
    if (const auto *gep =
            dynamic_cast<const CoreIrConstantGetElementPtr *>(constant);
        gep != nullptr) {
        collect_constant_references(gep->get_base(), functions, globals);
        for (const CoreIrConstant *index : gep->get_indices()) {
            collect_constant_references(index, functions, globals);
        }
    }
}

void collect_function_references(CoreIrFunction &function,
                                 std::vector<CoreIrFunction *> &functions,
                                 std::vector<CoreIrGlobal *> &globals) {
    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block_ptr->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }
            if (auto *call = dynamic_cast<CoreIrCallInst *>(instruction);
                call != nullptr && call->get_is_direct_call()) {
                if (CoreIrModule *module = function.get_parent();
                    module != nullptr) {
                    if (CoreIrFunction *callee =
                            module->find_function(call->get_callee_name());
                        callee != nullptr) {
                        functions.push_back(callee);
                    }
                }
            }
            if (auto *address =
                    dynamic_cast<CoreIrAddressOfFunctionInst *>(instruction);
                address != nullptr && address->get_function() != nullptr) {
                functions.push_back(address->get_function());
            }
            if (auto *address =
                    dynamic_cast<CoreIrAddressOfGlobalInst *>(instruction);
                address != nullptr && address->get_global() != nullptr) {
                globals.push_back(address->get_global());
            }
        }
    }
}

} // namespace

PassKind CoreIrGlobalDcePass::Kind() const { return PassKind::CoreIrGlobalDce; }

const char *CoreIrGlobalDcePass::Name() const { return "CoreIrGlobalDcePass"; }

CoreIrPassMetadata CoreIrGlobalDcePass::Metadata() const noexcept {
    return CoreIrPassMetadata::core_ir_transform();
}

PassResult CoreIrGlobalDcePass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    std::unordered_set<CoreIrFunction *> reachable_functions;
    std::unordered_set<CoreIrGlobal *> reachable_globals;
    std::vector<CoreIrFunction *> function_worklist;
    std::vector<CoreIrGlobal *> global_worklist;

    for (const auto &function_ptr : module->get_functions()) {
        CoreIrFunction *function = function_ptr.get();
        if (function != nullptr &&
            (!function->get_is_internal_linkage() || function->get_name() == "main")) {
            if (reachable_functions.insert(function).second) {
                function_worklist.push_back(function);
            }
        }
    }
    for (const auto &global_ptr : module->get_globals()) {
        CoreIrGlobal *global = global_ptr.get();
        if (global != nullptr && !global->get_is_internal_linkage()) {
            if (reachable_globals.insert(global).second) {
                global_worklist.push_back(global);
            }
        }
    }

    while (!function_worklist.empty() || !global_worklist.empty()) {
        while (!function_worklist.empty()) {
            CoreIrFunction *function = function_worklist.back();
            function_worklist.pop_back();
            std::vector<CoreIrFunction *> referenced_functions;
            std::vector<CoreIrGlobal *> referenced_globals;
            collect_function_references(*function, referenced_functions, referenced_globals);
            for (CoreIrFunction *referenced : referenced_functions) {
                if (referenced != nullptr &&
                    reachable_functions.insert(referenced).second) {
                    function_worklist.push_back(referenced);
                }
            }
            for (CoreIrGlobal *referenced : referenced_globals) {
                if (referenced != nullptr &&
                    reachable_globals.insert(referenced).second) {
                    global_worklist.push_back(referenced);
                }
            }
        }

        while (!global_worklist.empty()) {
            CoreIrGlobal *global = global_worklist.back();
            global_worklist.pop_back();
            std::vector<CoreIrFunction *> referenced_functions;
            std::vector<CoreIrGlobal *> referenced_globals;
            collect_constant_references(global->get_initializer(), referenced_functions,
                                        referenced_globals);
            for (CoreIrFunction *referenced : referenced_functions) {
                if (referenced != nullptr &&
                    reachable_functions.insert(referenced).second) {
                    function_worklist.push_back(referenced);
                }
            }
            for (CoreIrGlobal *referenced : referenced_globals) {
                if (referenced != nullptr &&
                    reachable_globals.insert(referenced).second) {
                    global_worklist.push_back(referenced);
                }
            }
        }
    }

    bool changed = false;
    std::vector<CoreIrFunction *> dead_functions;
    for (const auto &function_ptr : module->get_functions()) {
        CoreIrFunction *function = function_ptr.get();
        if (function != nullptr && function->get_is_internal_linkage() &&
            reachable_functions.find(function) == reachable_functions.end()) {
            dead_functions.push_back(function);
        }
    }
    for (CoreIrFunction *function : dead_functions) {
        changed = module->erase_function(function) || changed;
    }

    std::vector<CoreIrGlobal *> dead_globals;
    for (const auto &global_ptr : module->get_globals()) {
        CoreIrGlobal *global = global_ptr.get();
        if (global != nullptr && global->get_is_internal_linkage() &&
            reachable_globals.find(global) == reachable_globals.end()) {
            dead_globals.push_back(global);
        }
    }
    for (CoreIrGlobal *global : dead_globals) {
        changed = module->erase_global(global) || changed;
    }

    CoreIrPassEffects effects;
    if (!changed) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.module_changed = true;
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
