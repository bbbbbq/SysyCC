#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/core/ir_value.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

enum class CoreIrVerifyIssueKind : unsigned char {
    ModuleOwnership,
    FunctionOwnership,
    BlockOwnership,
    InstructionOwnership,
    TerminatorLayout,
    PhiLayout,
    PhiIncomingMismatch,
    UseDefMismatch,
    InvalidReference,
};

struct CoreIrVerifyIssue {
    CoreIrVerifyIssueKind kind = CoreIrVerifyIssueKind::InvalidReference;
    std::string message;
    const CoreIrFunction *function = nullptr;
    const CoreIrBasicBlock *block = nullptr;
    const CoreIrInstruction *instruction = nullptr;
};

struct CoreIrVerifyResult {
    bool ok = true;
    std::vector<CoreIrVerifyIssue> issues;

    void add_issue(CoreIrVerifyIssue issue) {
        ok = false;
        issues.push_back(std::move(issue));
    }
};

class CoreIrVerifier {
  private:
    static std::string function_prefix(const CoreIrFunction &function) {
        return "func @" + function.get_name() + ": ";
    }

    static std::string block_prefix(const CoreIrFunction &function,
                                    const CoreIrBasicBlock &block) {
        return function_prefix(function) + "block %" + block.get_name() + ": ";
    }

    static std::string instruction_prefix(const CoreIrFunction &function,
                                          const CoreIrBasicBlock &block,
                                          const CoreIrInstruction &instruction) {
        if (!instruction.get_name().empty()) {
            return block_prefix(function, block) + "inst %" +
                   instruction.get_name() + ": ";
        }
        return block_prefix(function, block) + "inst <unnamed>: ";
    }

    static void add_issue(CoreIrVerifyResult &result, CoreIrVerifyIssueKind kind,
                          std::string message,
                          const CoreIrFunction *function = nullptr,
                          const CoreIrBasicBlock *block = nullptr,
                          const CoreIrInstruction *instruction = nullptr) {
        result.add_issue(CoreIrVerifyIssue{kind, std::move(message), function, block,
                                           instruction});
    }

    static bool belongs_to_function(const CoreIrFunction &function,
                                    const CoreIrBasicBlock *block) {
        return block != nullptr && block->get_parent() == &function;
    }

    static void verify_instruction(
        const CoreIrModule *module, const CoreIrFunction &function,
        const CoreIrBasicBlock &block, const CoreIrInstruction &instruction,
        const std::unordered_set<const CoreIrBasicBlock *> &block_set,
        const std::unordered_set<const CoreIrStackSlot *> &stack_slot_set,
        const std::unordered_set<const CoreIrFunction *> &function_set,
        const std::unordered_set<const CoreIrGlobal *> &global_set,
        CoreIrVerifyResult &result) {
        if (instruction.get_type() == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      instruction_prefix(function, block, instruction) +
                          "instruction type is null",
                      &function, &block, &instruction);
        }

        const auto &operands = instruction.get_operands();
        for (std::size_t index = 0; index < operands.size(); ++index) {
            if (operands[index] == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "operand " + std::to_string(index) + " is null",
                          &function, &block, &instruction);
            }
        }

        if (const auto *phi = dynamic_cast<const CoreIrPhiInst *>(&instruction);
            phi != nullptr) {
            std::unordered_set<const CoreIrBasicBlock *> incoming_blocks;
            for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
                const CoreIrBasicBlock *incoming_block =
                    phi->get_incoming_block(index);
                if (!belongs_to_function(function, incoming_block)) {
                    add_issue(result, CoreIrVerifyIssueKind::PhiIncomingMismatch,
                              instruction_prefix(function, block, instruction) +
                                  "phi incoming block is outside function",
                              &function, &block, &instruction);
                }
                if (!incoming_blocks.insert(incoming_block).second) {
                    add_issue(result, CoreIrVerifyIssueKind::PhiIncomingMismatch,
                              instruction_prefix(function, block, instruction) +
                                  "phi has duplicate incoming block",
                              &function, &block, &instruction);
                }
                if (phi->get_incoming_value(index) == nullptr) {
                    add_issue(result, CoreIrVerifyIssueKind::PhiIncomingMismatch,
                              instruction_prefix(function, block, instruction) +
                                  "phi incoming value is null",
                              &function, &block, &instruction);
                }
            }
            return;
        }

        if (const auto *address_of_function =
                dynamic_cast<const CoreIrAddressOfFunctionInst *>(&instruction);
            address_of_function != nullptr) {
            if (module != nullptr &&
                function_set.find(address_of_function->get_function()) ==
                    function_set.end()) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "address_of_function target is outside module",
                          &function, &block, &instruction);
            }
            return;
        }

        if (const auto *address_of_global =
                dynamic_cast<const CoreIrAddressOfGlobalInst *>(&instruction);
            address_of_global != nullptr) {
            if (module != nullptr &&
                global_set.find(address_of_global->get_global()) == global_set.end()) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "address_of_global target is outside module",
                          &function, &block, &instruction);
            }
            return;
        }

        if (const auto *address_of_stack_slot =
                dynamic_cast<const CoreIrAddressOfStackSlotInst *>(&instruction);
            address_of_stack_slot != nullptr) {
            if (stack_slot_set.find(address_of_stack_slot->get_stack_slot()) ==
                stack_slot_set.end()) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "address_of_stackslot target is outside function",
                          &function, &block, &instruction);
            }
            return;
        }

        if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
            load != nullptr) {
            if (load->get_stack_slot() != nullptr &&
                stack_slot_set.find(load->get_stack_slot()) == stack_slot_set.end()) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "load stack slot is outside function",
                          &function, &block, &instruction);
            }
            if (load->get_stack_slot() == nullptr && load->get_address() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "address-based load has null address",
                          &function, &block, &instruction);
            }
            return;
        }

        if (const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
            store != nullptr) {
            if (store->get_value() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "store value is null",
                          &function, &block, &instruction);
            }
            if (store->get_stack_slot() != nullptr &&
                stack_slot_set.find(store->get_stack_slot()) == stack_slot_set.end()) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "store stack slot is outside function",
                          &function, &block, &instruction);
            }
            if (store->get_stack_slot() == nullptr && store->get_address() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "address-based store has null address",
                          &function, &block, &instruction);
            }
            return;
        }

        if (const auto *call = dynamic_cast<const CoreIrCallInst *>(&instruction);
            call != nullptr) {
            if (call->get_callee_type() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "call callee type is null",
                          &function, &block, &instruction);
            }
            if (call->get_is_direct_call()) {
                if (call->get_callee_name().empty()) {
                    add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                              instruction_prefix(function, block, instruction) +
                                  "direct call callee name is empty",
                              &function, &block, &instruction);
                }
            } else if (call->get_callee_value() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "indirect call callee value is null",
                          &function, &block, &instruction);
            }
            return;
        }

        if (const auto *jump = dynamic_cast<const CoreIrJumpInst *>(&instruction);
            jump != nullptr) {
            if (block_set.find(jump->get_target_block()) == block_set.end()) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "jump target is outside function",
                          &function, &block, &instruction);
            }
            return;
        }

        if (const auto *cond_jump = dynamic_cast<const CoreIrCondJumpInst *>(&instruction);
            cond_jump != nullptr) {
            if (cond_jump->get_condition() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "condjump condition is null",
                          &function, &block, &instruction);
            }
            if (block_set.find(cond_jump->get_true_block()) == block_set.end()) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "condjump true target is outside function",
                          &function, &block, &instruction);
            }
            if (block_set.find(cond_jump->get_false_block()) == block_set.end()) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "condjump false target is outside function",
                          &function, &block, &instruction);
            }
            return;
        }

        if (const auto *return_inst = dynamic_cast<const CoreIrReturnInst *>(&instruction);
            return_inst != nullptr && function.get_function_type() != nullptr) {
            const CoreIrType *return_type =
                function.get_function_type()->get_return_type();
            const bool returns_void =
                return_type != nullptr &&
                return_type->get_kind() == CoreIrTypeKind::Void;
            if (returns_void && return_inst->get_return_value() != nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "void return carries a value",
                          &function, &block, &instruction);
            }
            if (!returns_void && return_inst->get_return_value() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          instruction_prefix(function, block, instruction) +
                              "non-void return has no value",
                          &function, &block, &instruction);
            }
        }
    }

    static void verify_use_lists(
        const CoreIrFunction &function,
        const std::unordered_set<const CoreIrValue *> &tracked_values,
        const std::unordered_map<const CoreIrValue *,
                                 std::vector<std::pair<const CoreIrInstruction *, std::size_t>>>
            &expected_uses,
        CoreIrVerifyResult &result) {
        for (const CoreIrValue *value : tracked_values) {
            if (value == nullptr) {
                continue;
            }

            const auto expected_it = expected_uses.find(value);
            const auto &actual_uses = value->get_uses();
            if (expected_it == expected_uses.end()) {
                if (!actual_uses.empty()) {
                    add_issue(result, CoreIrVerifyIssueKind::UseDefMismatch,
                              function_prefix(function) + "value %" +
                                  value->get_name() +
                                  " has unexpected use-list entries",
                              &function);
                }
                continue;
            }

            for (const auto &expected_use : expected_it->second) {
                const CoreIrInstruction *user = expected_use.first;
                const std::size_t operand_index = expected_use.second;
                const bool found = std::any_of(
                    actual_uses.begin(), actual_uses.end(),
                    [user, operand_index](const CoreIrUse &use) {
                        return use.get_user() == user &&
                               use.get_operand_index() == operand_index;
                    });
                if (!found) {
                    add_issue(result, CoreIrVerifyIssueKind::UseDefMismatch,
                              function_prefix(function) + "value %" +
                                  value->get_name() +
                                  " is missing reverse use metadata",
                              &function);
                }
            }

            for (const CoreIrUse &use : actual_uses) {
                const CoreIrInstruction *user = use.get_user();
                if (user == nullptr) {
                    add_issue(result, CoreIrVerifyIssueKind::UseDefMismatch,
                              function_prefix(function) + "value %" +
                                  value->get_name() + " has null user in use list",
                              &function);
                    continue;
                }
                if (user->get_parent() == nullptr ||
                    user->get_parent()->get_parent() != &function) {
                    add_issue(result, CoreIrVerifyIssueKind::UseDefMismatch,
                              function_prefix(function) + "value %" +
                                  value->get_name() +
                                  " is referenced by an instruction outside its function",
                              &function);
                    continue;
                }
                const auto &operands = user->get_operands();
                if (use.get_operand_index() >= operands.size() ||
                    operands[use.get_operand_index()] != value) {
                    add_issue(result, CoreIrVerifyIssueKind::UseDefMismatch,
                              function_prefix(function) + "value %" +
                                  value->get_name() +
                                  " has stale use-list metadata",
                              &function);
                }
            }
        }
    }

  public:
    CoreIrVerifyResult verify_module(const CoreIrModule &module) const {
        CoreIrVerifyResult result;
        std::unordered_set<const CoreIrFunction *> function_set;
        std::unordered_set<const CoreIrGlobal *> global_set;

        for (const auto &global : module.get_globals()) {
            if (global == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::ModuleOwnership,
                          "module @" + module.get_name() + ": null global entry");
                continue;
            }
            global_set.insert(global.get());
            if (global->get_type() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::ModuleOwnership,
                          "module @" + module.get_name() + ": global @" +
                              global->get_name() + " has null type");
            }
        }

        for (const auto &function : module.get_functions()) {
            if (function == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::ModuleOwnership,
                          "module @" + module.get_name() + ": null function entry");
                continue;
            }
            function_set.insert(function.get());
            CoreIrVerifyResult function_result = verify_function(*function);
            result.issues.insert(result.issues.end(), function_result.issues.begin(),
                                 function_result.issues.end());
            result.ok = result.ok && function_result.ok;
        }

        for (const auto &function : module.get_functions()) {
            if (function == nullptr) {
                continue;
            }
            std::unordered_set<const CoreIrBasicBlock *> block_set;
            std::unordered_set<const CoreIrStackSlot *> stack_slot_set;
            for (const auto &block : function->get_basic_blocks()) {
                if (block != nullptr) {
                    block_set.insert(block.get());
                }
            }
            for (const auto &stack_slot : function->get_stack_slots()) {
                if (stack_slot != nullptr) {
                    stack_slot_set.insert(stack_slot.get());
                }
            }
            for (const auto &block : function->get_basic_blocks()) {
                if (block == nullptr) {
                    continue;
                }
                for (const auto &instruction : block->get_instructions()) {
                    if (instruction == nullptr) {
                        continue;
                    }
                    verify_instruction(&module, *function, *block, *instruction,
                                       block_set, stack_slot_set, function_set,
                                       global_set, result);
                }
            }
        }

        return result;
    }

    CoreIrVerifyResult verify_function(
        const CoreIrFunction &function,
        const CoreIrCfgAnalysisResult *cfg_analysis = nullptr) const {
        CoreIrVerifyResult result;
        std::unordered_set<const CoreIrBasicBlock *> block_set;
        std::unordered_set<const CoreIrStackSlot *> stack_slot_set;
        std::unordered_set<const CoreIrFunction *> empty_function_set;
        std::unordered_set<const CoreIrGlobal *> empty_global_set;
        std::unordered_set<const CoreIrValue *> tracked_values;
        std::unordered_map<const CoreIrValue *,
                           std::vector<std::pair<const CoreIrInstruction *, std::size_t>>>
            expected_uses;

        if (function.get_function_type() == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                      function_prefix(function) + "function type is null",
                      &function);
        } else if (!function.get_basic_blocks().empty() &&
                   function.get_function_type()->get_parameter_types().size() !=
                       function.get_parameters().size()) {
            add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                      function_prefix(function) +
                          "parameter count does not match function type",
                      &function);
        }

        for (const auto &parameter : function.get_parameters()) {
            if (parameter == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                          function_prefix(function) + "null parameter entry",
                          &function);
                continue;
            }
            if (parameter->get_type() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                          function_prefix(function) + "parameter %" +
                              parameter->get_name() + " has null type",
                          &function);
            }
            tracked_values.insert(parameter.get());
        }

        for (const auto &stack_slot : function.get_stack_slots()) {
            if (stack_slot == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                          function_prefix(function) + "null stack slot entry",
                          &function);
                continue;
            }
            if (stack_slot->get_allocated_type() == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                          function_prefix(function) + "stack slot %" +
                              stack_slot->get_name() + " has null allocated type",
                          &function);
            }
            stack_slot_set.insert(stack_slot.get());
        }

        for (const auto &block : function.get_basic_blocks()) {
            if (block == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                          function_prefix(function) + "null basic block entry",
                          &function);
                continue;
            }
            if (!block_set.insert(block.get()).second) {
                add_issue(result, CoreIrVerifyIssueKind::BlockOwnership,
                          function_prefix(function) + "duplicate basic block pointer",
                          &function, block.get());
            }
            if (block->get_parent() != &function) {
                add_issue(result, CoreIrVerifyIssueKind::BlockOwnership,
                          block_prefix(function, *block) +
                              "basic block parent mismatch",
                          &function, block.get());
            }
        }

        std::vector<const CoreIrPhiInst *> phis;
        for (const auto &block : function.get_basic_blocks()) {
            if (block == nullptr) {
                continue;
            }
            if (block->get_instructions().empty()) {
                add_issue(result, CoreIrVerifyIssueKind::TerminatorLayout,
                          block_prefix(function, *block) + "basic block is empty",
                          &function, block.get());
                continue;
            }

            std::size_t terminator_count = 0;
            bool seen_non_phi = false;
            for (std::size_t index = 0; index < block->get_instructions().size();
                 ++index) {
                const auto &instruction_ptr = block->get_instructions()[index];
                if (instruction_ptr == nullptr) {
                    add_issue(result, CoreIrVerifyIssueKind::InstructionOwnership,
                              block_prefix(function, *block) +
                                  "null instruction entry",
                              &function, block.get());
                    continue;
                }

                const CoreIrInstruction &instruction = *instruction_ptr;
                tracked_values.insert(instruction_ptr.get());
                if (instruction.get_parent() != block.get()) {
                    add_issue(result, CoreIrVerifyIssueKind::InstructionOwnership,
                              instruction_prefix(function, *block, instruction) +
                                  "instruction parent mismatch",
                              &function, block.get(), instruction_ptr.get());
                }
                if (instruction.get_is_terminator()) {
                    ++terminator_count;
                    if (index + 1 != block->get_instructions().size()) {
                        add_issue(result, CoreIrVerifyIssueKind::TerminatorLayout,
                                  instruction_prefix(function, *block, instruction) +
                                      "terminator is not the last instruction",
                                  &function, block.get(), instruction_ptr.get());
                    }
                }

                if (instruction.get_opcode() == CoreIrOpcode::Phi) {
                    if (seen_non_phi) {
                        add_issue(result, CoreIrVerifyIssueKind::PhiLayout,
                                  instruction_prefix(function, *block, instruction) +
                                      "phi appears after a non-phi instruction",
                                  &function, block.get(), instruction_ptr.get());
                    }
                    phis.push_back(static_cast<const CoreIrPhiInst *>(
                        instruction_ptr.get()));
                } else {
                    seen_non_phi = true;
                }

                verify_instruction(nullptr, function, *block, instruction, block_set,
                                   stack_slot_set, empty_function_set,
                                   empty_global_set, result);

                const auto &operands = instruction.get_operands();
                for (std::size_t operand_index = 0; operand_index < operands.size();
                     ++operand_index) {
                    const CoreIrValue *operand = operands[operand_index];
                    if (operand == nullptr) {
                        continue;
                    }
                    tracked_values.insert(operand);
                    expected_uses[operand].push_back(
                        {instruction_ptr.get(), operand_index});
                }
            }

            if (terminator_count == 0) {
                add_issue(result, CoreIrVerifyIssueKind::TerminatorLayout,
                          block_prefix(function, *block) +
                              "basic block has no terminator",
                          &function, block.get());
            } else if (terminator_count > 1) {
                add_issue(result, CoreIrVerifyIssueKind::TerminatorLayout,
                          block_prefix(function, *block) +
                              "basic block has multiple terminators",
                          &function, block.get());
            }
        }

        CoreIrCfgAnalysis cfg_runner;
        const CoreIrCfgAnalysisResult computed_cfg =
            cfg_analysis == nullptr ? cfg_runner.Run(function) : *cfg_analysis;
        const CoreIrCfgAnalysisResult &cfg_result =
            cfg_analysis == nullptr ? computed_cfg : *cfg_analysis;
        if (!function.get_basic_blocks().empty() &&
            cfg_result.get_entry_block() != function.get_basic_blocks().front().get()) {
            add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                      function_prefix(function) +
                          "entry block does not match the first basic block",
                      &function);
        }

        for (const CoreIrPhiInst *phi : phis) {
            const CoreIrBasicBlock *block = phi->get_parent();
            if (block == nullptr) {
                continue;
            }
            std::unordered_set<const CoreIrBasicBlock *> incoming_blocks;
            for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
                incoming_blocks.insert(phi->get_incoming_block(index));
            }
            std::unordered_set<const CoreIrBasicBlock *> predecessor_blocks;
            for (const CoreIrBasicBlock *predecessor :
                 cfg_result.get_predecessors(block)) {
                predecessor_blocks.insert(predecessor);
            }
            if (incoming_blocks != predecessor_blocks) {
                add_issue(result, CoreIrVerifyIssueKind::PhiIncomingMismatch,
                          block_prefix(function, *block) +
                              "phi incoming set does not match CFG predecessors",
                          &function, block, phi);
            }
        }

        verify_use_lists(function, tracked_values, expected_uses, result);
        return result;
    }
};

inline bool emit_core_ir_verify_result(CompilerContext &context,
                                       const CoreIrVerifyResult &verify_result,
                                       const char *pass_name) {
    if (verify_result.ok) {
        return true;
    }

    for (const CoreIrVerifyIssue &issue : verify_result.issues) {
        context.get_diagnostic_engine().add_error(
            DiagnosticStage::Compiler,
            std::string(pass_name) + ": " + issue.message);
    }
    return false;
}

} // namespace sysycc
