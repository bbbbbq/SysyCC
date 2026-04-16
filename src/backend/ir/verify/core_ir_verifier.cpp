#include "backend/ir/verify/core_ir_verifier.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_set>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

namespace sysycc {

namespace {

void add_issue(CoreIrVerifyResult &result, CoreIrVerifyIssueKind kind,
               std::string message, const CoreIrFunction *function = nullptr,
               const CoreIrBasicBlock *block = nullptr,
               const CoreIrInstruction *instruction = nullptr) {
    result.add_issue(CoreIrVerifyIssue{kind, std::move(message), function,
                                       block, instruction});
}

bool contains_use(const CoreIrValue &value, const CoreIrInstruction &user,
                  std::size_t operand_index) {
    for (const CoreIrUse &use : value.get_uses()) {
        if (use.get_user() == &user &&
            use.get_operand_index() == operand_index) {
            return true;
        }
    }
    return false;
}

bool operand_matches_use(const CoreIrInstruction &user, const CoreIrUse &use,
                         const CoreIrValue &value) {
    const auto &operands = user.get_operands();
    return use.get_operand_index() < operands.size() &&
           operands[use.get_operand_index()] == &value;
}

void verify_value_uses(CoreIrVerifyResult &result, const CoreIrValue &value,
                       const CoreIrFunction &function) {
    for (const CoreIrUse &use : value.get_uses()) {
        const CoreIrInstruction *user = use.get_user();
        if (user == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::UseDefMismatch,
                      "value use list contains null user", &function);
            continue;
        }
        if (!operand_matches_use(*user, use, value)) {
            add_issue(result, CoreIrVerifyIssueKind::UseDefMismatch,
                      "value use list and user operand list are out of sync",
                      &function, user->get_parent(), user);
        }
    }
}

void verify_instruction_operands(
    CoreIrVerifyResult &result, const CoreIrInstruction &instruction,
    const CoreIrFunction &function,
    std::unordered_set<const CoreIrValue *> &seen_values) {
    for (std::size_t operand_index = 0;
         operand_index < instruction.get_operands().size(); ++operand_index) {
        CoreIrValue *operand = instruction.get_operands()[operand_index];
        if (operand == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "instruction contains null operand", &function,
                      instruction.get_parent(), &instruction);
            continue;
        }
        seen_values.insert(operand);
        if (!contains_use(*operand, instruction, operand_index)) {
            add_issue(result, CoreIrVerifyIssueKind::UseDefMismatch,
                      "operand is missing reciprocal use entry", &function,
                      instruction.get_parent(), &instruction);
        }
    }
}

void verify_phi(CoreIrVerifyResult &result, const CoreIrPhiInst &phi,
                const CoreIrFunction &function,
                const CoreIrCfgAnalysisResult &cfg_analysis) {
    std::unordered_set<const CoreIrBasicBlock *> incoming_blocks;
    for (std::size_t index = 0; index < phi.get_incoming_count(); ++index) {
        CoreIrBasicBlock *incoming_block = phi.get_incoming_block(index);
        CoreIrValue *incoming_value = phi.get_incoming_value(index);
        if (incoming_block == nullptr || incoming_value == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::PhiIncomingMismatch,
                      "phi incoming edge contains null block or value",
                      &function, phi.get_parent(), &phi);
            continue;
        }
        if (!incoming_blocks.insert(incoming_block).second) {
            add_issue(result, CoreIrVerifyIssueKind::PhiIncomingMismatch,
                      "phi contains duplicate incoming predecessor", &function,
                      phi.get_parent(), &phi);
        }
    }

    const CoreIrBasicBlock *parent_block = phi.get_parent();
    if (parent_block == nullptr) {
        return;
    }
    const auto &predecessors = cfg_analysis.get_predecessors(parent_block);
    if (incoming_blocks.size() != predecessors.size()) {
        add_issue(result, CoreIrVerifyIssueKind::PhiIncomingMismatch,
                  "phi incoming blocks do not match CFG predecessor count",
                  &function, parent_block, &phi);
        return;
    }
    for (CoreIrBasicBlock *predecessor : predecessors) {
        if (incoming_blocks.find(predecessor) == incoming_blocks.end()) {
            add_issue(result, CoreIrVerifyIssueKind::PhiIncomingMismatch,
                      "phi incoming blocks do not match CFG predecessors",
                      &function, parent_block, &phi);
            return;
        }
    }
}

void verify_instruction_references(CoreIrVerifyResult &result,
                                   const CoreIrInstruction &instruction,
                                   const CoreIrFunction &function,
                                   const CoreIrModule *module,
                                   const CoreIrContext *context) {
    if (instruction.get_type() == nullptr ||
        instruction.get_type()->get_parent_context() != context) {
        add_issue(
            result, CoreIrVerifyIssueKind::InvalidReference,
            "instruction type does not belong to the current Core IR context",
            &function, instruction.get_parent(), &instruction);
    }

    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Phi:
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Select:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::ExtractElement:
    case CoreIrOpcode::InsertElement:
    case CoreIrOpcode::ShuffleVector:
    case CoreIrOpcode::VectorReduceAdd:
    case CoreIrOpcode::GetElementPtr:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Return:
        return;
    case CoreIrOpcode::AddressOfFunction: {
        const auto &address =
            static_cast<const CoreIrAddressOfFunctionInst &>(instruction);
        if (address.get_function() == nullptr ||
            address.get_function()->get_parent() != module) {
            add_issue(
                result, CoreIrVerifyIssueKind::InvalidReference,
                "addr_of_function references a function outside the module",
                &function, instruction.get_parent(), &instruction);
        }
        return;
    }
    case CoreIrOpcode::AddressOfGlobal: {
        const auto &address =
            static_cast<const CoreIrAddressOfGlobalInst &>(instruction);
        if (address.get_global() == nullptr ||
            address.get_global()->get_parent() != module) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "addr_of_global references a global outside the module",
                      &function, instruction.get_parent(), &instruction);
        }
        return;
    }
    case CoreIrOpcode::AddressOfStackSlot: {
        const auto &address =
            static_cast<const CoreIrAddressOfStackSlotInst &>(instruction);
        if (address.get_stack_slot() == nullptr ||
            address.get_stack_slot()->get_parent() != &function) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "addr_of_stackslot references a stack slot outside the "
                      "function",
                      &function, instruction.get_parent(), &instruction);
        }
        return;
    }
    case CoreIrOpcode::Call: {
        const auto &call = static_cast<const CoreIrCallInst &>(instruction);
        if (call.get_callee_type() == nullptr ||
            call.get_callee_type()->get_parent_context() != context) {
            add_issue(
                result, CoreIrVerifyIssueKind::InvalidReference,
                "call references a callee type outside the current context",
                &function, instruction.get_parent(), &instruction);
        }
        return;
    }
    case CoreIrOpcode::Jump: {
        const auto &jump = static_cast<const CoreIrJumpInst &>(instruction);
        if (jump.get_target_block() == nullptr ||
            jump.get_target_block()->get_parent() != &function) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "jump target does not belong to the current function",
                      &function, instruction.get_parent(), &instruction);
        }
        return;
    }
    case CoreIrOpcode::CondJump: {
        const auto &cond_jump =
            static_cast<const CoreIrCondJumpInst &>(instruction);
        if (cond_jump.get_true_block() == nullptr ||
            cond_jump.get_true_block()->get_parent() != &function ||
            cond_jump.get_false_block() == nullptr ||
            cond_jump.get_false_block()->get_parent() != &function) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "conditional jump target does not belong to the current "
                      "function",
                      &function, instruction.get_parent(), &instruction);
        }
        return;
    }
    case CoreIrOpcode::IndirectJump: {
        const auto &jump =
            static_cast<const CoreIrIndirectJumpInst &>(instruction);
        if (jump.get_address() == nullptr ||
            jump.get_address()->get_type() == nullptr ||
            jump.get_address()->get_type()->get_kind() != CoreIrTypeKind::Pointer) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "indirect jump address must be a pointer value",
                      &function, instruction.get_parent(), &instruction);
        }
        if (jump.get_target_blocks().empty()) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "indirect jump must list at least one possible target",
                      &function, instruction.get_parent(), &instruction);
        }
        for (CoreIrBasicBlock *target : jump.get_target_blocks()) {
            if (target == nullptr || target->get_parent() != &function) {
                add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                          "indirect jump target does not belong to the current function",
                          &function, instruction.get_parent(), &instruction);
                break;
            }
        }
        return;
    }
    }
}

} // namespace

CoreIrVerifyResult
CoreIrVerifier::verify_module(const CoreIrModule &module) const {
    CoreIrVerifyResult result;
    const CoreIrContext *context = module.get_parent_context();
    if (context == nullptr) {
        add_issue(result, CoreIrVerifyIssueKind::ModuleOwnership,
                  "module is missing parent Core IR context");
    }

    for (const auto &global : module.get_globals()) {
        if (global == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::ModuleOwnership,
                      "module contains null global");
            continue;
        }
        if (global->get_parent() != &module) {
            add_issue(result, CoreIrVerifyIssueKind::ModuleOwnership,
                      "global parent pointer does not match module");
        }
        if (global->get_type() == nullptr ||
            global->get_type()->get_parent_context() != context) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "global type does not belong to the module context");
        }
        if (global->get_initializer() != nullptr &&
            global->get_initializer()->get_parent_context() != context) {
            add_issue(
                result, CoreIrVerifyIssueKind::InvalidReference,
                "global initializer does not belong to the module context");
        }
    }

    for (const auto &function : module.get_functions()) {
        if (function == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::ModuleOwnership,
                      "module contains null function");
            continue;
        }
        if (function->get_parent() != &module) {
            add_issue(result, CoreIrVerifyIssueKind::ModuleOwnership,
                      "function parent pointer does not match module",
                      function.get());
        }
        if (function->get_function_type() == nullptr ||
            function->get_function_type()->get_parent_context() != context) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "function type does not belong to the module context",
                      function.get());
        }
        CoreIrVerifyResult function_result = verify_function(*function);
        if (!function_result.ok) {
            result.ok = false;
            result.issues.insert(result.issues.end(),
                                 function_result.issues.begin(),
                                 function_result.issues.end());
        }
    }

    return result;
}

CoreIrVerifyResult CoreIrVerifier::verify_function(
    const CoreIrFunction &function,
    const CoreIrCfgAnalysisResult *cfg_analysis) const {
    CoreIrVerifyResult result;
    const CoreIrModule *module = function.get_parent();
    const CoreIrContext *context =
        module == nullptr ? nullptr : module->get_parent_context();

    CoreIrCfgAnalysis owned_cfg_analysis_runner;
    CoreIrCfgAnalysisResult owned_cfg_analysis;
    if (cfg_analysis == nullptr) {
        owned_cfg_analysis = owned_cfg_analysis_runner.Run(function);
        cfg_analysis = &owned_cfg_analysis;
    }

    std::unordered_set<const CoreIrValue *> seen_values;
    for (const auto &parameter : function.get_parameters()) {
        if (parameter == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                      "function contains null parameter", &function);
            continue;
        }
        if (parameter->get_parent() != &function) {
            add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                      "parameter parent pointer does not match function",
                      &function);
        }
        seen_values.insert(parameter.get());
        verify_value_uses(result, *parameter, function);
    }

    for (const auto &stack_slot : function.get_stack_slots()) {
        if (stack_slot == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                      "function contains null stack slot", &function);
            continue;
        }
        if (stack_slot->get_parent() != &function) {
            add_issue(result, CoreIrVerifyIssueKind::FunctionOwnership,
                      "stack slot parent pointer does not match function",
                      &function);
        }
        if (stack_slot->get_allocated_type() == nullptr ||
            stack_slot->get_allocated_type()->get_parent_context() != context) {
            add_issue(result, CoreIrVerifyIssueKind::InvalidReference,
                      "stack slot allocated type does not belong to the "
                      "function context",
                      &function);
        }
    }

    if (function.get_basic_blocks().empty()) {
        return result;
    }

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            add_issue(result, CoreIrVerifyIssueKind::BlockOwnership,
                      "function contains null basic block", &function);
            continue;
        }
        if (block->get_parent() != &function) {
            add_issue(result, CoreIrVerifyIssueKind::BlockOwnership,
                      "basic block parent pointer does not match function",
                      &function, block.get());
        }
        if (block->get_instructions().empty()) {
            add_issue(result, CoreIrVerifyIssueKind::TerminatorLayout,
                      "basic block is empty", &function, block.get());
            continue;
        }

        bool seen_non_phi = false;
        for (std::size_t index = 0; index < block->get_instructions().size();
             ++index) {
            const auto &instruction_ptr = block->get_instructions()[index];
            if (instruction_ptr == nullptr) {
                add_issue(result, CoreIrVerifyIssueKind::InstructionOwnership,
                          "basic block contains null instruction", &function,
                          block.get());
                continue;
            }

            const CoreIrInstruction &instruction = *instruction_ptr;
            seen_values.insert(&instruction);
            verify_value_uses(result, instruction, function);
            verify_instruction_operands(result, instruction, function,
                                        seen_values);
            verify_instruction_references(result, instruction, function, module,
                                          context);

            if (instruction.get_parent() != block.get()) {
                add_issue(
                    result, CoreIrVerifyIssueKind::InstructionOwnership,
                    "instruction parent pointer does not match basic block",
                    &function, block.get(), &instruction);
            }

            const bool is_phi = instruction.get_opcode() == CoreIrOpcode::Phi;
            if (is_phi && seen_non_phi) {
                add_issue(
                    result, CoreIrVerifyIssueKind::PhiLayout,
                    "phi instruction is not placed at the start of the block",
                    &function, block.get(), &instruction);
            }
            if (!is_phi) {
                seen_non_phi = true;
            } else {
                verify_phi(result,
                           static_cast<const CoreIrPhiInst &>(instruction),
                           function, *cfg_analysis);
            }

            const bool is_last = index + 1 == block->get_instructions().size();
            if (instruction.get_is_terminator() != is_last) {
                add_issue(
                    result, CoreIrVerifyIssueKind::TerminatorLayout,
                    is_last ? "basic block does not end with a terminator"
                            : "terminator appears before the end of the block",
                    &function, block.get(), &instruction);
            }
        }
    }

    return result;
}

bool emit_core_ir_verify_result(CompilerContext &context,
                                const CoreIrVerifyResult &verify_result,
                                const char *pass_name) {
    if (verify_result.ok) {
        return true;
    }

    if (CoreIrBuildResult *build_result = context.get_core_ir_build_result();
        build_result != nullptr && build_result->get_module() != nullptr) {
        std::filesystem::create_directories("build/intermediate_results");
        const std::filesystem::path input_path(context.get_input_file());
        const std::filesystem::path output_file =
            std::filesystem::path("build/intermediate_results") /
            (input_path.stem().string() + ".verify-fail.core-ir.txt");
        std::ofstream ofs(output_file);
        if (ofs.is_open()) {
            CoreIrRawPrinter printer;
            ofs << printer.print_module(*build_result->get_module());
        }
    }

    for (const CoreIrVerifyIssue &issue : verify_result.issues) {
        std::string detail;
        if (issue.function != nullptr) {
            detail += " function=" + issue.function->get_name();
        }
        if (issue.block != nullptr) {
            detail += " block=" + issue.block->get_name();
        }
        if (issue.instruction != nullptr) {
            if (!issue.instruction->get_name().empty()) {
                detail += " inst=" + issue.instruction->get_name();
            } else {
                detail += " inst=<unnamed>";
            }
        }
        context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                  std::string(pass_name) +
                                                      ": " + issue.message +
                                                      detail);
    }
    return false;
}

} // namespace sysycc
