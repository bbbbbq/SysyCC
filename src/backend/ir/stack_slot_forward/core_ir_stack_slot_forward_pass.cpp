#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"

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

bool forward_stack_slot_values(CoreIrBasicBlock &block) {
    bool changed = false;
    std::unordered_map<CoreIrStackSlot *, CoreIrValue *> known_slot_values;
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
            if (store->get_stack_slot() != nullptr) {
                known_slot_values[store->get_stack_slot()] = store->get_value();
            } else {
                known_slot_values.clear();
            }
            ++index;
            continue;
        }

        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction); load != nullptr) {
            if (load->get_stack_slot() != nullptr) {
                auto it = known_slot_values.find(load->get_stack_slot());
                if (it != known_slot_values.end() && it->second != nullptr) {
                    load->replace_all_uses_with(it->second);
                    erase_instruction(block, load);
                    changed = true;
                    continue;
                }
            }
            ++index;
            continue;
        }

        if (dynamic_cast<CoreIrCallInst *>(instruction) != nullptr) {
            known_slot_values.clear();
            ++index;
            continue;
        }

        ++index;
    }

    return changed;
}

} // namespace

PassKind CoreIrStackSlotForwardPass::Kind() const {
    return PassKind::CoreIrStackSlotForward;
}

const char *CoreIrStackSlotForwardPass::Name() const {
    return "CoreIrStackSlotForwardPass";
}

PassResult CoreIrStackSlotForwardPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    for (const auto &function : module->get_functions()) {
        bool function_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            function_changed = forward_stack_slot_values(*block) || function_changed;
        }
        if (function_changed) {
            build_result->invalidate_core_ir_analyses(*function);
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
