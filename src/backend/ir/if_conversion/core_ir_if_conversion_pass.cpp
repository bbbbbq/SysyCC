#include "backend/ir/if_conversion/core_ir_if_conversion_pass.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/detail/core_ir_clone_utils.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::clone_instruction_remapped;
using sysycc::detail::are_equivalent_pointer_values;
using sysycc::detail::erase_instruction;
using sysycc::detail::insert_instruction_before;
using sysycc::detail::replace_instruction;

constexpr std::size_t kMaxConvertedArmInstructions = 8;

PassResult fail_missing_core_ir(CompilerContext &context,
                                const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

bool loop_contains_block(const CoreIrLoopInfo &loop,
                         const CoreIrBasicBlock *block) {
    return block != nullptr &&
           loop.get_blocks().find(const_cast<CoreIrBasicBlock *>(block)) !=
               loop.get_blocks().end();
}

bool instruction_is_ifconvertible_value(const CoreIrInstruction &instruction) {
    if (instruction.get_is_terminator() ||
        instruction.get_opcode() == CoreIrOpcode::Phi) {
        return false;
    }

    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Select:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::GetElementPtr:
        return true;
    case CoreIrOpcode::Phi:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Call:
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::IndirectJump:
    case CoreIrOpcode::Return:
        return false;
    }
    return false;
}

CoreIrBasicBlock *get_unique_jump_target(CoreIrBasicBlock *block) {
    if (block == nullptr || block->get_instructions().empty()) {
        return nullptr;
    }
    auto *jump =
        dynamic_cast<CoreIrJumpInst *>(block->get_instructions().back().get());
    return jump == nullptr ? nullptr : jump->get_target_block();
}

bool block_has_phi(const CoreIrBasicBlock &block) {
    for (const auto &instruction : block.get_instructions()) {
        if (instruction == nullptr) {
            continue;
        }
        return instruction->get_opcode() == CoreIrOpcode::Phi;
    }
    return false;
}

std::vector<CoreIrInstruction *>
collect_arm_instructions(CoreIrBasicBlock &block) {
    std::vector<CoreIrInstruction *> instructions;
    for (const auto &instruction_ptr : block.get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction->get_is_terminator()) {
            continue;
        }
        instructions.push_back(instruction);
    }
    return instructions;
}

struct StoreDiamondArmInfo {
    std::vector<CoreIrInstruction *> prefix_instructions;
    CoreIrStoreInst *store = nullptr;
};

bool instruction_uses_stay_within_arm_or_merge(
    const CoreIrInstruction &instruction, const CoreIrBasicBlock &arm_block,
    const CoreIrBasicBlock &merge_block);

std::string make_ifc_clone_name(const CoreIrInstruction &instruction,
                                const char *suffix, std::size_t index);

bool collect_store_diamond_arm(CoreIrBasicBlock &block,
                               CoreIrBasicBlock *merge_block,
                               const CoreIrCfgAnalysisResult &cfg,
                               const CoreIrLoopInfo &loop,
                               StoreDiamondArmInfo &arm_info) {
    if (merge_block == nullptr || block_has_phi(block) ||
        cfg.get_predecessor_count(&block) != 1 ||
        get_unique_jump_target(&block) != merge_block) {
        return false;
    }

    std::vector<CoreIrInstruction *> instructions =
        collect_arm_instructions(block);
    if (instructions.empty() ||
        instructions.size() > kMaxConvertedArmInstructions) {
        return false;
    }

    auto *store = dynamic_cast<CoreIrStoreInst *>(instructions.back());
    if (store == nullptr || store->get_value() == nullptr) {
        return false;
    }
    if (store->get_stack_slot() == nullptr && store->get_address() == nullptr) {
        return false;
    }

    instructions.pop_back();
    for (CoreIrInstruction *instruction : instructions) {
        if (instruction == nullptr ||
            !instruction_is_ifconvertible_value(*instruction) ||
            !loop_contains_block(loop, instruction->get_parent()) ||
            !instruction_uses_stay_within_arm_or_merge(*instruction, block,
                                                       *merge_block)) {
            return false;
        }
    }

    arm_info.prefix_instructions = std::move(instructions);
    arm_info.store = store;
    return true;
}

CoreIrValue *remap_arm_value(
    CoreIrValue *value,
    const std::unordered_map<const CoreIrValue *, CoreIrValue *> &value_map) {
    if (value == nullptr) {
        return nullptr;
    }
    auto it = value_map.find(value);
    return it == value_map.end() ? value : it->second;
}

bool clone_arm_prefix_into_branch(
    CoreIrBasicBlock &branch_block, CoreIrInstruction *anchor,
    const std::vector<CoreIrInstruction *> &arm_instructions,
    const char *suffix,
    std::unordered_map<const CoreIrValue *, CoreIrValue *> &value_map,
    std::size_t &name_counter) {
    for (CoreIrInstruction *instruction : arm_instructions) {
        if (instruction == nullptr) {
            continue;
        }
        std::unique_ptr<CoreIrInstruction> clone =
            clone_instruction_remapped(*instruction, value_map);
        if (clone == nullptr) {
            return false;
        }
        clone->set_name(
            make_ifc_clone_name(*instruction, suffix, name_counter++));
        CoreIrInstruction *clone_ptr =
            insert_instruction_before(branch_block, anchor, std::move(clone));
        value_map[instruction] = clone_ptr;
    }
    return true;
}

bool instruction_uses_stay_within_arm_or_merge(
    const CoreIrInstruction &instruction, const CoreIrBasicBlock &arm_block,
    const CoreIrBasicBlock &merge_block) {
    for (const CoreIrUse &use : instruction.get_uses()) {
        CoreIrInstruction *user = use.get_user();
        if (user == nullptr) {
            continue;
        }
        if (user->get_parent() == &arm_block) {
            continue;
        }
        if (user->get_parent() == &merge_block &&
            user->get_opcode() == CoreIrOpcode::Phi) {
            continue;
        }
        return false;
    }
    return true;
}

bool block_is_ifconvertible_arm(CoreIrBasicBlock &block,
                                CoreIrBasicBlock *merge_block,
                                const CoreIrCfgAnalysisResult &cfg,
                                const CoreIrLoopInfo &loop,
                                std::size_t &instruction_count) {
    if (merge_block == nullptr || block_has_phi(block) ||
        cfg.get_predecessor_count(&block) != 1 ||
        get_unique_jump_target(&block) != merge_block) {
        return false;
    }

    std::vector<CoreIrInstruction *> instructions =
        collect_arm_instructions(block);
    instruction_count = instructions.size();
    if (instruction_count > kMaxConvertedArmInstructions) {
        return false;
    }

    for (CoreIrInstruction *instruction : instructions) {
        if (instruction == nullptr ||
            !instruction_is_ifconvertible_value(*instruction) ||
            !loop_contains_block(loop, instruction->get_parent()) ||
            !instruction_uses_stay_within_arm_or_merge(*instruction, block,
                                                       *merge_block)) {
            return false;
        }
    }
    return true;
}

std::vector<CoreIrPhiInst *>
collect_convertible_merge_phis(CoreIrBasicBlock &merge_block,
                               CoreIrBasicBlock *true_block,
                               CoreIrBasicBlock *false_block) {
    std::vector<CoreIrPhiInst *> phis;
    for (const auto &instruction_ptr : merge_block.get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if (phi->get_incoming_count() != 2) {
            return {};
        }
        CoreIrBasicBlock *incoming0 = phi->get_incoming_block(0);
        CoreIrBasicBlock *incoming1 = phi->get_incoming_block(1);
        if (!((incoming0 == true_block && incoming1 == false_block) ||
              (incoming0 == false_block && incoming1 == true_block))) {
            return {};
        }
        phis.push_back(phi);
    }
    return phis;
}

std::vector<CoreIrPhiInst *>
collect_convertible_triangle_merge_phis(CoreIrBasicBlock &merge_block,
                                        CoreIrBasicBlock &branch_block,
                                        CoreIrBasicBlock *side_block) {
    std::vector<CoreIrPhiInst *> phis;
    for (const auto &instruction_ptr : merge_block.get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if (phi->get_incoming_count() != 2) {
            return {};
        }

        bool saw_branch = false;
        bool saw_side = false;
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            CoreIrBasicBlock *incoming = phi->get_incoming_block(index);
            if (incoming == &branch_block) {
                saw_branch = true;
            } else if (incoming == side_block) {
                saw_side = true;
            } else {
                return {};
            }
        }
        if (!saw_branch || !saw_side) {
            return {};
        }
        phis.push_back(phi);
    }
    return phis;
}

CoreIrValue *get_phi_incoming_for_block(CoreIrPhiInst &phi,
                                        CoreIrBasicBlock *block) {
    for (std::size_t index = 0; index < phi.get_incoming_count(); ++index) {
        if (phi.get_incoming_block(index) == block) {
            return phi.get_incoming_value(index);
        }
    }
    return nullptr;
}

std::string make_ifc_clone_name(const CoreIrInstruction &instruction,
                                const char *suffix, std::size_t index) {
    if (!instruction.get_name().empty()) {
        return instruction.get_name() + suffix;
    }
    return std::string("ifc.") + std::to_string(index) + suffix;
}

bool clone_arm_into_branch(
    CoreIrBasicBlock &branch_block, CoreIrInstruction *anchor,
    const std::vector<CoreIrInstruction *> &arm_instructions,
    const char *suffix,
    std::unordered_map<const CoreIrValue *, CoreIrValue *> &value_map,
    std::size_t &name_counter) {
    for (CoreIrInstruction *instruction : arm_instructions) {
        if (instruction == nullptr) {
            continue;
        }
        std::unique_ptr<CoreIrInstruction> clone =
            clone_instruction_remapped(*instruction, value_map);
        if (clone == nullptr) {
            return false;
        }
        clone->set_name(
            make_ifc_clone_name(*instruction, suffix, name_counter++));
        CoreIrInstruction *clone_ptr =
            insert_instruction_before(branch_block, anchor, std::move(clone));
        value_map[instruction] = clone_ptr;
    }
    return true;
}

bool if_convert_same_store_diamond(const CoreIrCfgAnalysisResult &cfg,
                                   const CoreIrLoopInfo &loop,
                                   CoreIrBasicBlock &branch_block,
                                   std::size_t &name_counter) {
    auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
        branch_block.get_instructions().empty()
            ? nullptr
            : branch_block.get_instructions().back().get());
    if (branch == nullptr) {
        return false;
    }

    CoreIrBasicBlock *true_block = branch->get_true_block();
    CoreIrBasicBlock *false_block = branch->get_false_block();
    if (true_block == nullptr || false_block == nullptr ||
        true_block == false_block || !loop_contains_block(loop, true_block) ||
        !loop_contains_block(loop, false_block)) {
        return false;
    }

    CoreIrBasicBlock *merge_block = get_unique_jump_target(true_block);
    if (merge_block == nullptr ||
        merge_block != get_unique_jump_target(false_block) ||
        !loop_contains_block(loop, merge_block) ||
        cfg.get_predecessor_count(merge_block) != 2 || block_has_phi(*merge_block)) {
        return false;
    }

    StoreDiamondArmInfo true_arm;
    StoreDiamondArmInfo false_arm;
    if (!collect_store_diamond_arm(*true_block, merge_block, cfg, loop, true_arm) ||
        !collect_store_diamond_arm(*false_block, merge_block, cfg, loop, false_arm)) {
        return false;
    }

    if (true_arm.prefix_instructions.size() + false_arm.prefix_instructions.size() + 2 >
        kMaxConvertedArmInstructions) {
        return false;
    }

    if (!detail::are_equivalent_types(true_arm.store->get_value()->get_type(),
                                      false_arm.store->get_value()->get_type())) {
        return false;
    }
    if (true_arm.store->get_stack_slot() != false_arm.store->get_stack_slot()) {
        if (true_arm.store->get_stack_slot() != nullptr ||
            false_arm.store->get_stack_slot() != nullptr ||
            !are_equivalent_pointer_values(true_arm.store->get_address(),
                                           false_arm.store->get_address())) {
            return false;
        }
    }

    std::unordered_map<const CoreIrValue *, CoreIrValue *> true_map;
    std::unordered_map<const CoreIrValue *, CoreIrValue *> false_map;
    if (!clone_arm_prefix_into_branch(branch_block, branch,
                                      true_arm.prefix_instructions,
                                      ".ifc.store.true", true_map,
                                      name_counter) ||
        !clone_arm_prefix_into_branch(branch_block, branch,
                                      false_arm.prefix_instructions,
                                      ".ifc.store.false", false_map,
                                      name_counter)) {
        return false;
    }

    CoreIrValue *true_value =
        remap_arm_value(true_arm.store->get_value(), true_map);
    CoreIrValue *false_value =
        remap_arm_value(false_arm.store->get_value(), false_map);
    if (true_value == nullptr || false_value == nullptr) {
        return false;
    }

    CoreIrValue *selected_value = true_value;
    if (true_value != false_value) {
        auto select = std::make_unique<CoreIrSelectInst>(
            true_arm.store->get_value()->get_type(),
            "ifc.store.sel." + std::to_string(name_counter++),
            branch->get_condition(), true_value, false_value);
        select->set_source_span(branch->get_source_span());
        selected_value =
            insert_instruction_before(branch_block, branch, std::move(select));
    }

    std::unique_ptr<CoreIrInstruction> merged_store;
    if (true_arm.store->get_stack_slot() != nullptr) {
        merged_store = std::make_unique<CoreIrStoreInst>(
            branch->get_type(), selected_value, true_arm.store->get_stack_slot(),
            true_arm.store->get_alignment());
    } else {
        CoreIrValue *store_address =
            remap_arm_value(true_arm.store->get_address(), true_map);
        if (store_address == nullptr) {
            store_address =
                remap_arm_value(false_arm.store->get_address(), false_map);
        }
        if (store_address == nullptr) {
            return false;
        }
        merged_store = std::make_unique<CoreIrStoreInst>(
            branch->get_type(), selected_value, store_address,
            true_arm.store->get_alignment());
    }
    merged_store->set_source_span(true_arm.store->get_source_span());
    insert_instruction_before(branch_block, branch, std::move(merged_store));

    auto replacement =
        std::make_unique<CoreIrJumpInst>(branch->get_type(), merge_block);
    replacement->set_source_span(branch->get_source_span());
    replace_instruction(branch_block, branch, std::move(replacement));
    return true;
}

bool if_convert_short_circuit_bool(const CoreIrCfgAnalysisResult &cfg,
                                   const CoreIrLoopInfo &loop,
                                   CoreIrBasicBlock &branch_block,
                                   std::size_t &name_counter) {
    auto *outer_branch = dynamic_cast<CoreIrCondJumpInst *>(
        branch_block.get_instructions().empty()
            ? nullptr
            : branch_block.get_instructions().back().get());
    if (outer_branch == nullptr) {
        return false;
    }

    CoreIrBasicBlock *rhs_block = outer_branch->get_true_block();
    CoreIrBasicBlock *false_block = outer_branch->get_false_block();
    if (rhs_block == nullptr || false_block == nullptr ||
        rhs_block == false_block || !loop_contains_block(loop, rhs_block) ||
        !loop_contains_block(loop, false_block) ||
        cfg.get_predecessor_count(rhs_block) != 1) {
        return false;
    }

    auto *false_jump = dynamic_cast<CoreIrJumpInst *>(
        false_block->get_instructions().empty()
            ? nullptr
            : false_block->get_instructions().back().get());
    auto *rhs_branch = dynamic_cast<CoreIrCondJumpInst *>(
        rhs_block->get_instructions().empty()
            ? nullptr
            : rhs_block->get_instructions().back().get());
    if (false_jump == nullptr || rhs_branch == nullptr ||
        get_unique_jump_target(false_block) == nullptr) {
        return false;
    }

    CoreIrBasicBlock *merge_block = false_jump->get_target_block();
    if (merge_block == nullptr || rhs_branch->get_true_block() != merge_block ||
        rhs_branch->get_false_block() != false_block ||
        cfg.get_predecessor_count(merge_block) != 2 ||
        !loop_contains_block(loop, merge_block)) {
        return false;
    }

    auto *phi = dynamic_cast<CoreIrPhiInst *>(
        merge_block->get_instructions().empty()
            ? nullptr
            : merge_block->get_instructions().front().get());
    if (phi == nullptr || phi->get_incoming_count() != 2 ||
        phi->get_incoming_block(0) == phi->get_incoming_block(1)) {
        return false;
    }

    CoreIrValue *rhs_value = get_phi_incoming_for_block(*phi, rhs_block);
    CoreIrValue *false_value = get_phi_incoming_for_block(*phi, false_block);
    auto *rhs_constant = dynamic_cast<CoreIrConstantInt *>(rhs_value);
    auto *false_constant = dynamic_cast<CoreIrConstantInt *>(false_value);
    if (rhs_constant == nullptr || false_constant == nullptr ||
        rhs_constant->get_value() != 1 || false_constant->get_value() != 0) {
        return false;
    }

    std::vector<CoreIrInstruction *> rhs_instructions =
        collect_arm_instructions(*rhs_block);
    if (rhs_instructions.size() > kMaxConvertedArmInstructions) {
        return false;
    }
    for (CoreIrInstruction *instruction : rhs_instructions) {
        if (instruction == nullptr ||
            !instruction_is_ifconvertible_value(*instruction) ||
            !loop_contains_block(loop, instruction->get_parent())) {
            return false;
        }
    }
    std::unordered_map<const CoreIrValue *, CoreIrValue *> rhs_map;
    if (!clone_arm_prefix_into_branch(branch_block, outer_branch, rhs_instructions,
                                      ".ifc.short.rhs", rhs_map,
                                      name_counter)) {
        return false;
    }

    CoreIrValue *inner_condition =
        remap_arm_value(rhs_branch->get_condition(), rhs_map);
    if (inner_condition == nullptr) {
        return false;
    }

    auto inner_select = std::make_unique<CoreIrSelectInst>(
        phi->get_type(), "ifc.short.inner." + std::to_string(name_counter++),
        inner_condition, rhs_constant, false_constant);
    CoreIrInstruction *inner_value =
        insert_instruction_before(branch_block, outer_branch, std::move(inner_select));
    auto outer_select = std::make_unique<CoreIrSelectInst>(
        phi->get_type(), phi->get_name().empty()
                             ? "ifc.short.outer." + std::to_string(name_counter++)
                             : phi->get_name() + ".ifc.short",
        outer_branch->get_condition(), inner_value, false_constant);
    CoreIrInstruction *replacement_value =
        insert_instruction_before(branch_block, outer_branch, std::move(outer_select));

    phi->replace_all_uses_with(replacement_value);
    erase_instruction(*merge_block, phi);
    auto replacement =
        std::make_unique<CoreIrJumpInst>(outer_branch->get_type(), merge_block);
    replacement->set_source_span(outer_branch->get_source_span());
    replace_instruction(branch_block, outer_branch, std::move(replacement));
    return true;
}

bool if_convert_diamond(const CoreIrCfgAnalysisResult &cfg,
                        const CoreIrLoopInfo &loop,
                        CoreIrBasicBlock &branch_block,
                        std::size_t &name_counter) {
    auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
        branch_block.get_instructions().empty()
            ? nullptr
            : branch_block.get_instructions().back().get());
    if (branch == nullptr) {
        return false;
    }

    CoreIrBasicBlock *true_block = branch->get_true_block();
    CoreIrBasicBlock *false_block = branch->get_false_block();
    if (true_block == nullptr || false_block == nullptr ||
        true_block == false_block || !loop_contains_block(loop, true_block) ||
        !loop_contains_block(loop, false_block)) {
        return false;
    }

    CoreIrBasicBlock *merge_block = get_unique_jump_target(true_block);
    if (merge_block == nullptr ||
        merge_block != get_unique_jump_target(false_block) ||
        !loop_contains_block(loop, merge_block) ||
        cfg.get_predecessor_count(merge_block) != 2) {
        return false;
    }

    std::vector<CoreIrPhiInst *> merge_phis =
        collect_convertible_merge_phis(*merge_block, true_block, false_block);
    if (merge_phis.empty()) {
        return false;
    }

    std::size_t true_count = 0;
    std::size_t false_count = 0;
    if (!block_is_ifconvertible_arm(*true_block, merge_block, cfg, loop,
                                    true_count) ||
        !block_is_ifconvertible_arm(*false_block, merge_block, cfg, loop,
                                    false_count) ||
        true_count + false_count > kMaxConvertedArmInstructions) {
        return false;
    }

    std::unordered_map<const CoreIrValue *, CoreIrValue *> true_map;
    std::unordered_map<const CoreIrValue *, CoreIrValue *> false_map;
    if (!clone_arm_into_branch(branch_block, branch,
                               collect_arm_instructions(*true_block),
                               ".ifc.true", true_map, name_counter) ||
        !clone_arm_into_branch(branch_block, branch,
                               collect_arm_instructions(*false_block),
                               ".ifc.false", false_map, name_counter)) {
        return false;
    }

    for (CoreIrPhiInst *phi : merge_phis) {
        if (phi == nullptr) {
            continue;
        }
        CoreIrValue *true_value = get_phi_incoming_for_block(*phi, true_block);
        CoreIrValue *false_value =
            get_phi_incoming_for_block(*phi, false_block);
        if (true_value == nullptr || false_value == nullptr) {
            return false;
        }
        if (auto it = true_map.find(true_value); it != true_map.end()) {
            true_value = it->second;
        }
        if (auto it = false_map.find(false_value); it != false_map.end()) {
            false_value = it->second;
        }

        CoreIrValue *replacement = true_value;
        if (true_value != false_value) {
            auto select = std::make_unique<CoreIrSelectInst>(
                phi->get_type(),
                phi->get_name().empty()
                    ? "ifc.sel." + std::to_string(name_counter++)
                    : phi->get_name() + ".ifc",
                branch->get_condition(), true_value, false_value);
            select->set_source_span(phi->get_source_span());
            replacement = insert_instruction_before(branch_block, branch,
                                                    std::move(select));
        }
        phi->replace_all_uses_with(replacement);
    }

    for (CoreIrPhiInst *phi : merge_phis) {
        erase_instruction(*merge_block, phi);
    }

    auto replacement =
        std::make_unique<CoreIrJumpInst>(branch->get_type(), merge_block);
    replacement->set_source_span(branch->get_source_span());
    replace_instruction(branch_block, branch, std::move(replacement));
    return true;
}

bool if_convert_triangle(const CoreIrCfgAnalysisResult &cfg,
                         const CoreIrLoopInfo &loop,
                         CoreIrBasicBlock &branch_block,
                         std::size_t &name_counter) {
    auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
        branch_block.get_instructions().empty()
            ? nullptr
            : branch_block.get_instructions().back().get());
    if (branch == nullptr) {
        return false;
    }

    CoreIrBasicBlock *true_block = branch->get_true_block();
    CoreIrBasicBlock *false_block = branch->get_false_block();
    if (true_block == nullptr || false_block == nullptr ||
        true_block == false_block || !loop_contains_block(loop, true_block) ||
        !loop_contains_block(loop, false_block)) {
        return false;
    }

    CoreIrBasicBlock *merge_block = nullptr;
    CoreIrBasicBlock *side_block = nullptr;
    bool condition_true_is_direct = false;
    if (get_unique_jump_target(false_block) == true_block &&
        cfg.get_predecessor_count(true_block) == 2 &&
        cfg.get_predecessor_count(false_block) == 1) {
        merge_block = true_block;
        side_block = false_block;
        condition_true_is_direct = true;
    } else if (get_unique_jump_target(true_block) == false_block &&
               cfg.get_predecessor_count(false_block) == 2 &&
               cfg.get_predecessor_count(true_block) == 1) {
        merge_block = false_block;
        side_block = true_block;
    } else {
        return false;
    }

    if (merge_block == nullptr || side_block == nullptr ||
        !loop_contains_block(loop, merge_block) ||
        !loop_contains_block(loop, side_block)) {
        return false;
    }

    std::vector<CoreIrPhiInst *> merge_phis =
        collect_convertible_triangle_merge_phis(*merge_block, branch_block,
                                               side_block);
    if (merge_phis.empty()) {
        return false;
    }

    std::size_t side_count = 0;
    if (!block_is_ifconvertible_arm(*side_block, merge_block, cfg, loop,
                                    side_count)) {
        return false;
    }

    std::unordered_map<const CoreIrValue *, CoreIrValue *> side_map;
    if (!clone_arm_into_branch(branch_block, branch,
                               collect_arm_instructions(*side_block),
                               ".ifc.tri.side", side_map, name_counter)) {
        return false;
    }

    for (CoreIrPhiInst *phi : merge_phis) {
        if (phi == nullptr) {
            continue;
        }

        CoreIrValue *direct_value =
            get_phi_incoming_for_block(*phi, &branch_block);
        CoreIrValue *side_value = get_phi_incoming_for_block(*phi, side_block);
        if (direct_value == nullptr || side_value == nullptr) {
            return false;
        }
        if (auto it = side_map.find(side_value); it != side_map.end()) {
            side_value = it->second;
        }

        CoreIrValue *true_value =
            condition_true_is_direct ? direct_value : side_value;
        CoreIrValue *false_value =
            condition_true_is_direct ? side_value : direct_value;
        CoreIrValue *replacement = true_value;
        if (true_value != false_value) {
            auto select = std::make_unique<CoreIrSelectInst>(
                phi->get_type(),
                phi->get_name().empty()
                    ? "ifc.tri.sel." + std::to_string(name_counter++)
                    : phi->get_name() + ".ifc",
                branch->get_condition(), true_value, false_value);
            select->set_source_span(phi->get_source_span());
            replacement = insert_instruction_before(branch_block, branch,
                                                    std::move(select));
        }
        phi->replace_all_uses_with(replacement);
    }

    for (CoreIrPhiInst *phi : merge_phis) {
        erase_instruction(*merge_block, phi);
    }

    auto replacement =
        std::make_unique<CoreIrJumpInst>(branch->get_type(), merge_block);
    replacement->set_source_span(branch->get_source_span());
    replace_instruction(branch_block, branch, std::move(replacement));
    return true;
}

bool run_if_conversion_on_function(CoreIrFunction &function) {
    std::size_t name_counter = 0;
    bool changed = false;
    while (true) {
        CoreIrCfgAnalysis cfg_analysis_runner;
        const CoreIrCfgAnalysisResult cfg = cfg_analysis_runner.Run(function);
        CoreIrDominatorTreeAnalysis dom_runner;
        const CoreIrDominatorTreeAnalysisResult dom =
            dom_runner.Run(function, cfg);
        CoreIrLoopInfoAnalysis loop_runner;
        const CoreIrLoopInfoAnalysisResult loop_info =
            loop_runner.Run(function, cfg, dom);

        std::vector<const CoreIrLoopInfo *> ordered_loops;
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr != nullptr) {
                ordered_loops.push_back(loop_ptr.get());
            }
        }
        std::sort(ordered_loops.begin(), ordered_loops.end(),
                  [](const CoreIrLoopInfo *lhs, const CoreIrLoopInfo *rhs) {
                      return lhs->get_depth() > rhs->get_depth();
                  });

        bool iteration_changed = false;
        for (const CoreIrLoopInfo *loop : ordered_loops) {
            if (loop == nullptr) {
                continue;
            }
            for (CoreIrBasicBlock *block : loop->get_blocks()) {
                if (block != nullptr &&
                    (if_convert_short_circuit_bool(cfg, *loop, *block,
                                                   name_counter) ||
                     if_convert_triangle(cfg, *loop, *block, name_counter) ||
                     if_convert_diamond(cfg, *loop, *block, name_counter) ||
                     if_convert_same_store_diamond(cfg, *loop, *block,
                                                   name_counter))) {
                    iteration_changed = true;
                    changed = true;
                    break;
                }
            }
            if (iteration_changed) {
                break;
            }
        }
        if (!iteration_changed) {
            return changed;
        }
    }
}

} // namespace

PassKind CoreIrIfConversionPass::Kind() const {
    return PassKind::CoreIrIfConversion;
}

const char *CoreIrIfConversionPass::Name() const {
    return "CoreIrIfConversionPass";
}

PassResult CoreIrIfConversionPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        if (function != nullptr && run_if_conversion_on_function(*function)) {
            effects.changed_functions.insert(function.get());
            effects.cfg_changed_functions.insert(function.get());
        }
    }

    effects.preserved_analyses = effects.has_changes()
                                     ? CoreIrPreservedAnalyses::preserve_none()
                                     : CoreIrPreservedAnalyses::preserve_all();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
