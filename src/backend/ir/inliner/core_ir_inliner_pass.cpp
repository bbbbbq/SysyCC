#include "backend/ir/inliner/core_ir_inliner_pass.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/detail/core_ir_clone_utils.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::erase_instruction;
using sysycc::detail::clone_instruction_remapped;

constexpr std::size_t kInlineBudget = 160;
constexpr std::size_t kAlwaysInlineBudget = 192;
constexpr std::size_t kPointerLoopInlineBudget = 16;
constexpr std::size_t kLoopifiedScalarHotLoopInlineBudget = 32;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

bool instruction_is_inline_supported(const CoreIrInstruction &instruction) {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Phi:
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Select:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::GetElementPtr:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::IndirectJump:
    case CoreIrOpcode::Return:
        return true;
    default:
        return false;
    }
}

std::size_t count_inline_cost(const CoreIrFunction &callee) {
    std::size_t cost = 0;
    for (const auto &block_ptr : callee.get_basic_blocks()) {
        if (block_ptr == nullptr) {
            continue;
        }
        cost += block_ptr->get_instructions().size();
    }
    return cost;
}

std::vector<CoreIrBasicBlock *> collect_terminator_successors(
    CoreIrInstruction *terminator);

bool callee_has_pointer_parameter(const CoreIrFunction &callee) {
    for (const auto &parameter_ptr : callee.get_parameters()) {
        const CoreIrParameter *parameter = parameter_ptr.get();
        if (parameter != nullptr &&
            dynamic_cast<const CoreIrPointerType *>(parameter->get_type()) !=
                nullptr) {
            return true;
        }
    }
    return false;
}

bool callee_has_cfg_backedge(const CoreIrFunction &callee) {
    std::unordered_map<const CoreIrBasicBlock *, std::size_t> block_order;
    for (std::size_t index = 0; index < callee.get_basic_blocks().size(); ++index) {
        const CoreIrBasicBlock *block = callee.get_basic_blocks()[index].get();
        if (block != nullptr) {
            block_order.emplace(block, index);
        }
    }

    for (std::size_t index = 0; index < callee.get_basic_blocks().size(); ++index) {
        const CoreIrBasicBlock *block = callee.get_basic_blocks()[index].get();
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        CoreIrInstruction *terminator = block->get_instructions().back().get();
        for (CoreIrBasicBlock *successor :
             collect_terminator_successors(terminator)) {
            auto it = block_order.find(successor);
            if (it != block_order.end() && it->second <= index) {
                return true;
            }
        }
    }
    return false;
}

bool block_is_inside_loop(
    const CoreIrBasicBlock *block,
    const CoreIrLoopInfoAnalysisResult &loop_info) {
    if (block == nullptr) {
        return false;
    }

    for (const auto &loop_ptr : loop_info.get_loops()) {
        const CoreIrLoopInfo *loop = loop_ptr.get();
        if (loop != nullptr &&
            loop->get_blocks().find(const_cast<CoreIrBasicBlock *>(block)) !=
                loop->get_blocks().end()) {
            return true;
        }
    }
    return false;
}

std::vector<CoreIrBasicBlock *> collect_terminator_successors(
    CoreIrInstruction *terminator) {
    std::vector<CoreIrBasicBlock *> successors;
    auto append_unique = [&successors](CoreIrBasicBlock *successor) {
        if (successor == nullptr ||
            std::find(successors.begin(), successors.end(), successor) !=
                successors.end()) {
            return;
        }
        successors.push_back(successor);
    };

    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator); jump != nullptr) {
        append_unique(jump->get_target_block());
    } else if (auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
               cond_jump != nullptr) {
        append_unique(cond_jump->get_true_block());
        append_unique(cond_jump->get_false_block());
    }

    return successors;
}

bool rewrite_phi_predecessor(CoreIrBasicBlock *successor,
                             CoreIrBasicBlock *old_predecessor,
                             CoreIrBasicBlock *new_predecessor) {
    if (successor == nullptr || old_predecessor == nullptr ||
        new_predecessor == nullptr || old_predecessor == new_predecessor) {
        return true;
    }

    for (const auto &instruction_ptr : successor->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) == old_predecessor) {
                phi->set_incoming_block(index, new_predecessor);
            }
        }
    }
    return true;
}

bool callee_is_inline_candidate(CoreIrFunction &callee,
                                const CoreIrCallGraphAnalysisResult &call_graph,
                                bool callsite_in_loop) {
    if (callee.get_basic_blocks().empty() || callee.get_is_variadic() ||
        call_graph.is_recursive(&callee) ||
        callee.get_basic_blocks().front() == nullptr) {
        return false;
    }
    const std::size_t inline_cost = count_inline_cost(callee);
    const std::size_t budget =
        callee.get_is_always_inline() ? kAlwaysInlineBudget : kInlineBudget;
    if (inline_cost > budget) {
        return false;
    }
    if (!callee.get_is_always_inline() && callee_has_pointer_parameter(callee) &&
        callee_has_cfg_backedge(callee) &&
        inline_cost > kPointerLoopInlineBudget) {
        return false;
    }
    if (!callee.get_is_always_inline() && !callee_has_pointer_parameter(callee) &&
        callee_has_cfg_backedge(callee) &&
        (!callsite_in_loop ||
         inline_cost > kLoopifiedScalarHotLoopInlineBudget)) {
        return false;
    }
    bool saw_return = false;
    for (const auto &block_ptr : callee.get_basic_blocks()) {
        const CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr || block->get_instructions().empty() ||
            !block->get_has_terminator()) {
            return false;
        }
        bool saw_terminator = false;
        for (const auto &instruction_ptr : block->get_instructions()) {
            const CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr ||
                !instruction_is_inline_supported(*instruction) ||
                instruction->get_opcode() == CoreIrOpcode::Call) {
                return false;
            }
            if (instruction->get_is_terminator()) {
                if (saw_terminator) {
                    return false;
                }
                saw_terminator = true;
                saw_return = saw_return ||
                             instruction->get_opcode() == CoreIrOpcode::Return;
            }
        }
        if (!saw_terminator) {
            return false;
        }
    }
    return saw_return;
}

CoreIrValue *remap_value(
    CoreIrValue *value,
    const std::unordered_map<const CoreIrValue *, CoreIrValue *> &value_map) {
    auto it = value_map.find(value);
    return it == value_map.end() ? value : it->second;
}

bool inline_direct_call(CoreIrCallInst &call, CoreIrFunction &callee,
                        std::size_t inline_site_id) {
    CoreIrBasicBlock *caller_block = call.get_parent();
    CoreIrFunction *caller_function =
        caller_block == nullptr ? nullptr : caller_block->get_parent();
    CoreIrBasicBlock *callee_entry =
        callee.get_basic_blocks().empty() ? nullptr : callee.get_basic_blocks().front().get();
    if (caller_block == nullptr || caller_function == nullptr ||
        callee_entry == nullptr || callee_entry->get_instructions().empty()) {
        return false;
    }
    const CoreIrType *void_type = nullptr;

    auto &caller_instructions = caller_block->get_instructions();
    auto call_it = std::find_if(
        caller_instructions.begin(), caller_instructions.end(),
        [&call](const std::unique_ptr<CoreIrInstruction> &instruction_ptr) {
            return instruction_ptr.get() == &call;
        });
    if (call_it == caller_instructions.end()) {
        return false;
    }

    auto continuation = std::make_unique<CoreIrBasicBlock>(
        call.get_name().empty() ? callee.get_name() + ".inline.cont." +
                                      std::to_string(inline_site_id)
                                : call.get_name() + ".inline.cont");
    CoreIrBasicBlock *continuation_block =
        caller_function->append_basic_block(std::move(continuation));
    if (continuation_block == nullptr) {
        return false;
    }

    std::vector<std::unique_ptr<CoreIrInstruction>> tail_instructions;
    for (auto it = std::next(call_it); it != caller_instructions.end(); ++it) {
        tail_instructions.push_back(std::move(*it));
    }
    caller_instructions.erase(std::next(call_it), caller_instructions.end());
    for (auto &instruction_ptr : tail_instructions) {
        if (instruction_ptr == nullptr) {
            continue;
        }
        continuation_block->append_instruction(std::move(instruction_ptr));
    }
    if (continuation_block->get_instructions().empty() ||
        !continuation_block->get_instructions().back()->get_is_terminator()) {
        return false;
    }
    void_type = continuation_block->get_instructions().back()->get_type();
    if (void_type == nullptr) {
        return false;
    }

    for (CoreIrBasicBlock *successor : collect_terminator_successors(
             continuation_block->get_instructions().back().get())) {
        if (!rewrite_phi_predecessor(successor, caller_block, continuation_block)) {
            return false;
        }
    }

    std::unordered_map<const CoreIrValue *, CoreIrValue *> value_map;
    for (std::size_t index = 0; index < callee.get_parameters().size(); ++index) {
        CoreIrParameter *parameter = callee.get_parameters()[index].get();
        if (parameter != nullptr && index < call.get_argument_count()) {
            value_map.emplace(parameter, call.get_argument(index));
        }
    }
    std::unordered_map<const CoreIrStackSlot *, CoreIrStackSlot *> stack_slot_map;
    for (const auto &stack_slot_ptr : callee.get_stack_slots()) {
        const CoreIrStackSlot *stack_slot = stack_slot_ptr.get();
        if (stack_slot == nullptr) {
            continue;
        }
        std::string stack_slot_name = callee.get_name() + ".inline." +
                                      std::to_string(inline_site_id) + "." +
                                      stack_slot->get_name();
        CoreIrStackSlot *cloned_stack_slot = caller_function->append_stack_slot(
            std::make_unique<CoreIrStackSlot>(
                std::move(stack_slot_name), stack_slot->get_allocated_type(),
                stack_slot->get_alignment()));
        if (cloned_stack_slot == nullptr) {
            return false;
        }
        stack_slot_map.emplace(stack_slot, cloned_stack_slot);
    }

    std::unordered_map<const CoreIrBasicBlock *, CoreIrBasicBlock *> block_map;
    for (const auto &block_ptr : callee.get_basic_blocks()) {
        const CoreIrBasicBlock *original_block = block_ptr.get();
        if (original_block == nullptr) {
            continue;
        }
        std::string block_name = callee.get_name() + ".inline." +
                                 std::to_string(inline_site_id) + "." +
                                 original_block->get_name();
        CoreIrBasicBlock *cloned_block = caller_function->append_basic_block(
            std::make_unique<CoreIrBasicBlock>(std::move(block_name)));
        if (cloned_block == nullptr) {
            return false;
        }
        block_map.emplace(original_block, cloned_block);
    }

    const bool needs_return_value =
        call.get_type() != nullptr &&
        call.get_type()->get_kind() != CoreIrTypeKind::Void &&
        !call.get_uses().empty();
    CoreIrPhiInst *return_phi = nullptr;
    if (needs_return_value) {
        auto phi = std::make_unique<CoreIrPhiInst>(
            call.get_type(),
            call.get_name().empty() ? callee.get_name() + ".inline.ret." +
                                          std::to_string(inline_site_id)
                                    : call.get_name() + ".inline.ret");
        return_phi = static_cast<CoreIrPhiInst *>(
            continuation_block->insert_instruction_before_first_non_phi(std::move(phi)));
        if (return_phi == nullptr) {
            return false;
        }
        call.replace_all_uses_with(return_phi);
    }

    for (const auto &block_ptr : callee.get_basic_blocks()) {
        const CoreIrBasicBlock *original_block = block_ptr.get();
        CoreIrBasicBlock *cloned_block = block_map[original_block];
        if (original_block == nullptr || cloned_block == nullptr) {
            return false;
        }
        for (const auto &instruction_ptr : original_block->get_instructions()) {
            auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
            if (phi == nullptr) {
                break;
            }
            auto clone = std::make_unique<CoreIrPhiInst>(phi->get_type(), phi->get_name());
            clone->set_source_span(phi->get_source_span());
            CoreIrInstruction *inserted = cloned_block->append_instruction(std::move(clone));
            value_map.emplace(phi, inserted);
        }
    }

    for (const auto &block_ptr : callee.get_basic_blocks()) {
        const CoreIrBasicBlock *original_block = block_ptr.get();
        CoreIrBasicBlock *cloned_block = block_map[original_block];
        if (original_block == nullptr || cloned_block == nullptr) {
            return false;
        }
        for (const auto &instruction_ptr : original_block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr ||
                instruction->get_opcode() == CoreIrOpcode::Phi) {
                continue;
            }
            if (auto *ret = dynamic_cast<CoreIrReturnInst *>(instruction); ret != nullptr) {
                if (return_phi != nullptr && ret->get_return_value() != nullptr) {
                    return_phi->add_incoming(
                        cloned_block,
                        remap_value(ret->get_return_value(), value_map));
                }
                auto jump =
                    std::make_unique<CoreIrJumpInst>(ret->get_type(), continuation_block);
                jump->set_source_span(ret->get_source_span());
                cloned_block->append_instruction(std::move(jump));
                continue;
            }
            std::unique_ptr<CoreIrInstruction> clone = clone_instruction_remapped(
                *instruction, value_map, &stack_slot_map, &block_map);
            if (clone == nullptr) {
                return false;
            }
            CoreIrInstruction *inserted = cloned_block->append_instruction(std::move(clone));
            value_map.emplace(instruction, inserted);
        }
    }

    for (const auto &block_ptr : callee.get_basic_blocks()) {
        const CoreIrBasicBlock *original_block = block_ptr.get();
        CoreIrBasicBlock *cloned_block = block_map[original_block];
        if (original_block == nullptr || cloned_block == nullptr) {
            return false;
        }
        std::size_t phi_index = 0;
        for (const auto &instruction_ptr : original_block->get_instructions()) {
            auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
            if (phi == nullptr) {
                break;
            }
            auto *cloned_phi = dynamic_cast<CoreIrPhiInst *>(
                cloned_block->get_instructions()[phi_index].get());
            if (cloned_phi == nullptr) {
                return false;
            }
            for (std::size_t incoming = 0; incoming < phi->get_incoming_count(); ++incoming) {
                cloned_phi->add_incoming(
                    block_map[phi->get_incoming_block(incoming)],
                    remap_value(phi->get_incoming_value(incoming), value_map));
            }
            ++phi_index;
        }
    }

    if (!erase_instruction(*caller_block, &call)) {
        return false;
    }
    caller_block->append_instruction(
        std::make_unique<CoreIrJumpInst>(void_type, block_map[callee_entry]));
    return true;
}

} // namespace

PassKind CoreIrInlinerPass::Kind() const { return PassKind::CoreIrInliner; }

const char *CoreIrInlinerPass::Name() const { return "CoreIrInlinerPass"; }

CoreIrPassMetadata CoreIrInlinerPass::Metadata() const noexcept {
    return CoreIrPassMetadata::core_ir_transform();
}

PassResult CoreIrInlinerPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir analysis manager");
    }

    const CoreIrCallGraphAnalysisResult &call_graph =
        analysis_manager->get_or_compute<CoreIrCallGraphAnalysis>(*module);

    CoreIrPassEffects effects;
    std::size_t inline_site_id = 0;
    for (const auto &function_ptr : module->get_functions()) {
        CoreIrFunction *caller = function_ptr.get();
        if (caller == nullptr) {
            continue;
        }
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*caller);
        bool changed = false;
        for (std::size_t block_index = 0;
             block_index < caller->get_basic_blocks().size(); ++block_index) {
            CoreIrBasicBlock *block = caller->get_basic_blocks()[block_index].get();
            if (block == nullptr) {
                continue;
            }
            const bool callsite_in_loop = block_is_inside_loop(block, loop_info);
            auto &instructions = block->get_instructions();
            for (std::size_t index = 0; index < instructions.size();) {
                auto *call = dynamic_cast<CoreIrCallInst *>(instructions[index].get());
                if (call == nullptr || !call->get_is_direct_call()) {
                    ++index;
                    continue;
                }
                CoreIrFunction *callee = module->find_function(call->get_callee_name());
                if (callee == nullptr || callee == caller ||
                    !callee_is_inline_candidate(*callee, call_graph,
                                                callsite_in_loop)) {
                    ++index;
                    continue;
                }
                if (!inline_direct_call(*call, *callee, inline_site_id++)) {
                    ++index;
                    continue;
                }
                changed = true;
                continue;
            }
        }
        if (changed) {
            effects.module_changed = true;
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
