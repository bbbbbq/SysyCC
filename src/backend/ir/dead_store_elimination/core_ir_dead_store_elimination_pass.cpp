#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"

#include <algorithm>
#include <memory>
#include <unordered_map>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

bool erase_instruction(CoreIrBasicBlock &block, CoreIrInstruction *instruction) {
    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [instruction](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == instruction;
        });
    if (it == instructions.end()) {
        return false;
    }
    (*it)->detach_operands();
    instructions.erase(it);
    return true;
}

std::size_t find_instruction_index(const CoreIrBasicBlock &block,
                                   const CoreIrInstruction *instruction) {
    const auto &instructions = block.get_instructions();
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        if (instructions[index].get() == instruction) {
            return index;
        }
    }
    return instructions.size();
}

bool eliminate_dead_stores(CoreIrBasicBlock &block) {
    bool changed = false;
    std::unordered_map<CoreIrStackSlot *, CoreIrStoreInst *> pending_stores;
    auto &instructions = block.get_instructions();

    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() + index);
            changed = true;
            continue;
        }

        if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction); store != nullptr) {
            if (store->get_stack_slot() == nullptr) {
                pending_stores.clear();
                ++index;
                continue;
            }

            auto it = pending_stores.find(store->get_stack_slot());
            if (it != pending_stores.end() && it->second != nullptr) {
                const std::size_t previous_index =
                    find_instruction_index(block, it->second);
                if (previous_index < instructions.size() &&
                    erase_instruction(block, it->second)) {
                    if (previous_index < index) {
                        --index;
                    }
                    changed = true;
                }
            }
            pending_stores[store->get_stack_slot()] = store;
            ++index;
            continue;
        }

        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction); load != nullptr) {
            if (load->get_stack_slot() != nullptr) {
                pending_stores.erase(load->get_stack_slot());
            } else {
                pending_stores.clear();
            }
            ++index;
            continue;
        }

        if (dynamic_cast<CoreIrCallInst *>(instruction) != nullptr) {
            pending_stores.clear();
        }

        ++index;
    }

    return changed;
}

} // namespace

PassKind CoreIrDeadStoreEliminationPass::Kind() const {
    return PassKind::CoreIrDeadStoreElimination;
}

const char *CoreIrDeadStoreEliminationPass::Name() const {
    return "CoreIrDeadStoreEliminationPass";
}

PassResult CoreIrDeadStoreEliminationPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    for (const auto &function : module->get_functions()) {
        bool function_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            function_changed = eliminate_dead_stores(*block) || function_changed;
        }
        if (function_changed) {
            build_result->invalidate_core_ir_analyses(*function);
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
