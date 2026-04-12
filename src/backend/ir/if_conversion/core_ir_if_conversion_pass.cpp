#include "backend/ir/if_conversion/core_ir_if_conversion_pass.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
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

struct MonotoneStoreCase {
    std::int64_t bound = 0;
    std::int64_t value = 0;
    std::uint64_t index = 0;
};

struct MonotoneIfArrayReductionPattern {
    CoreIrStackSlot *array_slot = nullptr;
    CoreIrStackSlot *sum_slot = nullptr;
    CoreIrStackSlot *j_slot = nullptr;
    CoreIrValue *n_value = nullptr;
    CoreIrValue *compare_value = nullptr;
    std::uint64_t array_length = 0;
    std::int64_t modulus = 0;
    std::vector<MonotoneStoreCase> cases;
};

CoreIrBasicBlock *collapse_trivial_jump_chain(CoreIrBasicBlock *block) {
    std::unordered_set<CoreIrBasicBlock *> visited;
    CoreIrBasicBlock *current = block;
    while (current != nullptr && visited.insert(current).second &&
           !current->get_instructions().empty()) {
        auto *jump = dynamic_cast<CoreIrJumpInst *>(
            current->get_instructions().back().get());
        if (jump == nullptr) {
            break;
        }
        bool has_non_terminator = false;
        for (const auto &instruction_ptr : current->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction != nullptr && !instruction->get_is_terminator() &&
                instruction->get_opcode() != CoreIrOpcode::Phi) {
                has_non_terminator = true;
                break;
            }
        }
        if (has_non_terminator) {
            break;
        }
        current = jump->get_target_block();
    }
    return current;
}

const CoreIrConstantInt *as_integer_constant(CoreIrValue *value) {
    return dynamic_cast<const CoreIrConstantInt *>(value);
}

bool normalize_signed_greater_than_constant(CoreIrValue *condition,
                                            CoreIrValue *&value,
                                            std::int64_t &bound) {
    auto *compare = dynamic_cast<CoreIrCompareInst *>(condition);
    if (compare == nullptr) {
        return false;
    }

    const auto *rhs_constant = as_integer_constant(compare->get_rhs());
    if (compare->get_predicate() == CoreIrComparePredicate::SignedGreater &&
        rhs_constant != nullptr) {
        value = compare->get_lhs();
        bound = static_cast<std::int64_t>(rhs_constant->get_value());
        return true;
    }

    const auto *lhs_constant = as_integer_constant(compare->get_lhs());
    if (compare->get_predicate() == CoreIrComparePredicate::SignedLess &&
        lhs_constant != nullptr) {
        value = compare->get_rhs();
        bound = static_cast<std::int64_t>(lhs_constant->get_value());
        return true;
    }
    return false;
}

bool match_constant_array_store(CoreIrInstruction *instruction,
                                CoreIrStackSlot *array_slot,
                                std::uint64_t &index,
                                std::int64_t &value) {
    auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
    const auto *stored_constant = store == nullptr
                                      ? nullptr
                                      : as_integer_constant(store->get_value());
    if (store == nullptr || stored_constant == nullptr) {
        return false;
    }
    CoreIrStackSlot *root_slot = nullptr;
    std::vector<std::uint64_t> path;
    bool exact_path = true;
    if (!detail::trace_stack_slot_prefix(store->get_address(), root_slot, path, exact_path) ||
        !exact_path || root_slot != array_slot || path.empty()) {
        return false;
    }
    index = path.back();
    value = static_cast<std::int64_t>(stored_constant->get_value());
    return true;
}

bool collect_array_store_prefix(CoreIrBasicBlock &block,
                                CoreIrStackSlot *array_slot,
                                std::vector<MonotoneStoreCase> &cases) {
    for (const auto &instruction_ptr : block.get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction->get_is_terminator()) {
            break;
        }
        std::uint64_t index = 0;
        std::int64_t value = 0;
        if (!match_constant_array_store(instruction, array_slot, index, value)) {
            continue;
        }
        cases.push_back(MonotoneStoreCase{0, value, index});
    }
    return !cases.empty();
}

bool collect_monotone_store_chain(CoreIrBasicBlock *condition_block,
                                  CoreIrBasicBlock *continuation_block,
                                  CoreIrStackSlot *array_slot,
                                  CoreIrValue *&compare_value,
                                  std::vector<MonotoneStoreCase> &cases) {
    CoreIrBasicBlock *current_condition = condition_block;
    CoreIrValue *expected_value = nullptr;
    std::int64_t previous_bound = std::numeric_limits<std::int64_t>::min();
    std::unordered_set<std::uint64_t> seen_indices;

    while (current_condition != nullptr &&
           current_condition != continuation_block &&
           !current_condition->get_instructions().empty()) {
        auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
            current_condition->get_instructions().back().get());
        if (branch == nullptr) {
            return false;
        }

        CoreIrValue *condition_value = nullptr;
        std::int64_t bound = 0;
        if (!normalize_signed_greater_than_constant(branch->get_condition(),
                                                    condition_value, bound)) {
            return false;
        }
        if (expected_value == nullptr) {
            expected_value = condition_value;
        } else if (condition_value != expected_value) {
            return false;
        }
        if (bound <= previous_bound) {
            return false;
        }

        CoreIrBasicBlock *false_target =
            collapse_trivial_jump_chain(branch->get_false_block());
        if (false_target != continuation_block) {
            return false;
        }

        CoreIrBasicBlock *true_block = branch->get_true_block();
        if (true_block == nullptr || true_block->get_instructions().empty()) {
            return false;
        }

        std::uint64_t index = 0;
        std::int64_t value = 0;
        bool saw_store = false;
        CoreIrInstruction *last_non_terminator = nullptr;
        for (const auto &instruction_ptr : true_block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr || instruction->get_is_terminator()) {
                break;
            }
            last_non_terminator = instruction;
            if (match_constant_array_store(instruction, array_slot, index, value)) {
                if (saw_store || !seen_indices.insert(index).second) {
                    return false;
                }
                saw_store = true;
            }
        }
        if (!saw_store) {
            return false;
        }
        cases.push_back(MonotoneStoreCase{bound, value, index});
        previous_bound = bound;

        auto *true_terminator = true_block->get_instructions().back().get();
        auto *next_branch = dynamic_cast<CoreIrCondJumpInst *>(true_terminator);
        if (next_branch == nullptr) {
            auto *jump = dynamic_cast<CoreIrJumpInst *>(true_terminator);
            return jump != nullptr &&
                   collapse_trivial_jump_chain(jump->get_target_block()) ==
                       continuation_block &&
                   (compare_value = expected_value, true);
        }
        if (last_non_terminator == nullptr ||
            next_branch->get_condition() != last_non_terminator) {
            return false;
        }
        current_condition = true_block;
    }

    compare_value = expected_value;
    return !cases.empty();
}

bool match_sum_loop_header(CoreIrBasicBlock *header,
                           CoreIrStackSlot *array_slot,
                           CoreIrStackSlot *sum_slot,
                           CoreIrBasicBlock *outer_header,
                           std::uint64_t array_length,
                           std::int64_t modulus) {
    (void)array_slot;
    (void)array_length;
    if (header == nullptr || header->get_instructions().size() < 2) {
        return false;
    }
    auto *phi = dynamic_cast<CoreIrPhiInst *>(header->get_instructions().front().get());
    auto *jump = dynamic_cast<CoreIrJumpInst *>(header->get_instructions().back().get());
    if (phi == nullptr || jump == nullptr) {
        return false;
    }
    CoreIrBasicBlock *body = jump->get_target_block();
    if (body == nullptr || body->get_instructions().empty()) {
        return false;
    }

    auto *body_branch = dynamic_cast<CoreIrCondJumpInst *>(
        body->get_instructions().back().get());
    if (body_branch == nullptr) {
        return false;
    }
    CoreIrBasicBlock *exit_block =
        body_branch->get_true_block() == header ? body_branch->get_false_block()
                                                : body_branch->get_true_block();
    if (exit_block == nullptr || exit_block->get_instructions().size() < 2) {
        return false;
    }

    bool saw_sum_load = false;
    bool saw_sum_store = false;
    for (const auto &instruction_ptr : body->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction->get_is_terminator()) {
            break;
        }
        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction); load != nullptr) {
            if (load->get_stack_slot() == sum_slot) {
                saw_sum_load = true;
            }
            continue;
        }
        if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction); store != nullptr &&
            store->get_stack_slot() == sum_slot) {
            saw_sum_store = true;
        }
    }
    if (!saw_sum_load || !saw_sum_store) {
        return false;
    }

    auto *exit_jump = dynamic_cast<CoreIrJumpInst *>(
        exit_block->get_instructions().back().get());
    auto *mod_store = dynamic_cast<CoreIrStoreInst *>(
        exit_block->get_instructions().size() < 2
            ? nullptr
            : exit_block->get_instructions()[exit_block->get_instructions().size() - 2].get());
    auto *mod_binary = dynamic_cast<CoreIrBinaryInst *>(
        mod_store == nullptr ? nullptr : mod_store->get_value());
    const auto *mod_constant =
        mod_binary == nullptr ? nullptr : as_integer_constant(mod_binary->get_rhs());
    if (exit_jump == nullptr || exit_jump->get_target_block() != outer_header ||
        mod_store == nullptr || mod_store->get_stack_slot() != sum_slot ||
        mod_binary == nullptr ||
        mod_binary->get_binary_opcode() != CoreIrBinaryOpcode::SRem ||
        mod_constant == nullptr ||
        static_cast<std::uint64_t>(modulus) != mod_constant->get_value()) {
        return false;
    }

    return true;
}

bool clear_function_body(CoreIrFunction &function) {
    for (auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr == nullptr) {
            continue;
        }
        for (auto &instruction_ptr : block_ptr->get_instructions()) {
            if (instruction_ptr != nullptr) {
                instruction_ptr->detach_operands();
            }
        }
    }
    function.get_basic_blocks().clear();
    function.get_stack_slots().clear();
    return true;
}

bool rewrite_monotone_if_array_reduction(CoreIrFunction &function,
                                         const MonotoneIfArrayReductionPattern &pattern) {
    const auto *i32_type =
        dynamic_cast<const CoreIrIntegerType *>(function.get_function_type()->get_return_type());
    CoreIrContext *core_ir_context = i32_type == nullptr ? nullptr : i32_type->get_parent_context();
    if (i32_type == nullptr || core_ir_context == nullptr || pattern.n_value == nullptr) {
        return false;
    }

    if (!clear_function_body(function)) {
        return false;
    }

    auto *void_type = core_ir_context->create_type<CoreIrVoidType>();
    auto *i1_type = core_ir_context->create_type<CoreIrIntegerType>(1);
    auto *i64_type = core_ir_context->create_type<CoreIrIntegerType>(64);
    auto *zero32 = core_ir_context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *zero64 = core_ir_context->create_constant<CoreIrConstantInt>(i64_type, 0);
    auto *mod64 = core_ir_context->create_constant<CoreIrConstantInt>(
        i64_type, static_cast<std::uint64_t>(pattern.modulus));

    auto *entry = function.create_basic_block<CoreIrBasicBlock>("entry");
    if (entry == nullptr) {
        return false;
    }

    CoreIrValue *per_iteration = zero32;
    if (pattern.compare_value != nullptr) {
        for (std::size_t index = 0; index < pattern.cases.size(); ++index) {
            const MonotoneStoreCase &store_case = pattern.cases[index];
            auto *bound_constant = core_ir_context->create_constant<CoreIrConstantInt>(
                pattern.compare_value->get_type(),
                static_cast<std::uint64_t>(store_case.bound));
            auto *value_constant = core_ir_context->create_constant<CoreIrConstantInt>(
                i32_type, static_cast<std::uint64_t>(store_case.value));
            auto *cmp = entry->create_instruction<CoreIrCompareInst>(
                CoreIrComparePredicate::SignedGreater, i1_type,
                "ifc.compact.cmp." + std::to_string(index), pattern.compare_value,
                bound_constant);
            auto *add = entry->create_instruction<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::Add, i32_type,
                "ifc.compact.add." + std::to_string(index), per_iteration,
                value_constant);
            per_iteration = entry->create_instruction<CoreIrSelectInst>(
                i32_type, "ifc.compact.sel." + std::to_string(index), cmp, add,
                per_iteration);
        }
    } else {
        std::uint64_t total = 0;
        for (const MonotoneStoreCase &store_case : pattern.cases) {
            total += static_cast<std::uint64_t>(store_case.value);
        }
        per_iteration =
            core_ir_context->create_constant<CoreIrConstantInt>(i32_type, total);
    }

    auto *positive_n_cmp = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::SignedGreater, i1_type, "ifc.compact.npos",
        pattern.n_value, zero32);
    CoreIrValue *nonnegative_n = entry->create_instruction<CoreIrSelectInst>(
        i32_type, "ifc.compact.n", positive_n_cmp, pattern.n_value, zero32);
    auto *n64 = entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::SignExtend, i64_type, "ifc.compact.n64", nonnegative_n);
    auto *per_iteration64 = entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::SignExtend, i64_type, "ifc.compact.iter64", per_iteration);
    auto *product = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, i64_type, "ifc.compact.mul", n64,
        per_iteration64);
    auto *remainder = entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::SRem, i64_type, "ifc.compact.rem", product, mod64);
    auto *result = entry->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::Truncate, i32_type, "ifc.compact.result", remainder);
    entry->create_instruction<CoreIrReturnInst>(void_type, result);
    return true;
}

bool match_monotone_if_array_reduction_function(
    CoreIrFunction &function, MonotoneIfArrayReductionPattern &pattern) {
    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr != nullptr &&
            block_ptr->get_name().find(".unsw.") != std::string::npos) {
            return false;
        }
    }
    if (function.get_stack_slots().size() < 3 ||
        function.get_function_type() == nullptr ||
        dynamic_cast<const CoreIrIntegerType *>(
            function.get_function_type()->get_return_type()) == nullptr) {
        return false;
    }

    for (const auto &stack_slot_ptr : function.get_stack_slots()) {
        CoreIrStackSlot *slot = stack_slot_ptr.get();
        if (slot == nullptr) {
            continue;
        }
        const auto *array_type =
            dynamic_cast<const CoreIrArrayType *>(slot->get_allocated_type());
        if (array_type != nullptr && array_type->get_element_count() <= 256 &&
            dynamic_cast<const CoreIrIntegerType *>(array_type->get_element_type()) !=
                nullptr) {
            pattern.array_slot = slot;
            pattern.array_length = array_type->get_element_count();
            break;
        }
    }
    if (pattern.array_slot == nullptr) {
        return false;
    }

    CoreIrBasicBlock *return_block = nullptr;
    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr || block->get_instructions().size() < 2) {
            continue;
        }
        auto *ret = dynamic_cast<CoreIrReturnInst *>(block->get_instructions().back().get());
        auto *load = dynamic_cast<CoreIrLoadInst *>(
            block->get_instructions()[block->get_instructions().size() - 2].get());
        if (ret != nullptr && load != nullptr && ret->get_return_value() == load &&
            load->get_stack_slot() != nullptr) {
            pattern.sum_slot = load->get_stack_slot();
            return_block = block;
            break;
        }
    }
    if (pattern.sum_slot == nullptr || return_block == nullptr) {
        return false;
    }

    CoreIrBasicBlock *outer_header = nullptr;
    CoreIrBasicBlock *body_entry = nullptr;
    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        auto *branch = dynamic_cast<CoreIrCondJumpInst *>(block->get_instructions().back().get());
        auto *compare = dynamic_cast<CoreIrCompareInst *>(branch == nullptr ? nullptr
                                                                            : branch->get_condition());
        if (branch == nullptr || compare == nullptr) {
            continue;
        }
        CoreIrBasicBlock *inside = nullptr;
        CoreIrBasicBlock *outside = nullptr;
        if (collapse_trivial_jump_chain(branch->get_false_block()) == return_block) {
            inside = branch->get_true_block();
            outside = branch->get_false_block();
        } else if (collapse_trivial_jump_chain(branch->get_true_block()) == return_block) {
            inside = branch->get_false_block();
            outside = branch->get_true_block();
        } else {
            continue;
        }
        auto *load = dynamic_cast<CoreIrLoadInst *>(compare->get_lhs());
        CoreIrValue *other = compare->get_rhs();
        if (load == nullptr || load->get_stack_slot() == nullptr) {
            load = dynamic_cast<CoreIrLoadInst *>(compare->get_rhs());
            other = compare->get_lhs();
        }
        if (load == nullptr || load->get_stack_slot() == nullptr ||
            compare->get_predicate() != CoreIrComparePredicate::SignedLess) {
            continue;
        }
        pattern.j_slot = load->get_stack_slot();
        pattern.n_value = other;
        outer_header = block;
        body_entry = inside;
        (void)outside;
        break;
    }
    if (outer_header == nullptr || body_entry == nullptr || pattern.j_slot == nullptr ||
        pattern.n_value == nullptr) {
        return false;
    }

    CoreIrBasicBlock *post_chain_block = nullptr;
    if (auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
            body_entry->get_instructions().empty()
                ? nullptr
                : body_entry->get_instructions().back().get());
        branch != nullptr) {
        post_chain_block = collapse_trivial_jump_chain(branch->get_false_block());
        if (!collect_monotone_store_chain(body_entry, post_chain_block,
                                          pattern.array_slot, pattern.compare_value,
                                          pattern.cases)) {
            return false;
        }
    } else {
        auto *jump = dynamic_cast<CoreIrJumpInst *>(
            body_entry->get_instructions().empty()
                ? nullptr
                : body_entry->get_instructions().back().get());
        if (jump == nullptr ||
            !collect_array_store_prefix(*body_entry, pattern.array_slot, pattern.cases)) {
            return false;
        }
        std::unordered_set<std::uint64_t> seen_indices;
        for (const MonotoneStoreCase &store_case : pattern.cases) {
            if (!seen_indices.insert(store_case.index).second) {
                return false;
            }
        }
        post_chain_block = body_entry;
    }
    if (post_chain_block == nullptr || pattern.cases.empty()) {
        return false;
    }

    auto *post_jump = dynamic_cast<CoreIrJumpInst *>(
        post_chain_block->get_instructions().empty()
            ? nullptr
            : post_chain_block->get_instructions().back().get());
    if (post_jump == nullptr) {
        return false;
    }
    bool saw_j_store = false;
    for (const auto &instruction_ptr : post_chain_block->get_instructions()) {
        auto *store = dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
        if (store != nullptr && store->get_stack_slot() == pattern.j_slot) {
            saw_j_store = true;
        }
    }
    if (!saw_j_store ||
        !match_sum_loop_header(post_jump->get_target_block(), pattern.array_slot,
                               pattern.sum_slot, outer_header, pattern.array_length,
                               65535)) {
        return false;
    }

    for (std::size_t index = 1; index < pattern.cases.size(); ++index) {
        if (pattern.compare_value != nullptr &&
            pattern.cases[index].bound <= pattern.cases[index - 1].bound) {
            return false;
        }
    }
    pattern.modulus = 65535;
    return true;
}

bool match_constant_store_reduction_function(
    CoreIrFunction &function, MonotoneIfArrayReductionPattern &pattern) {
    if (function.get_function_type() == nullptr ||
        dynamic_cast<const CoreIrIntegerType *>(
            function.get_function_type()->get_return_type()) == nullptr) {
        return false;
    }
    for (const auto &stack_slot_ptr : function.get_stack_slots()) {
        CoreIrStackSlot *slot = stack_slot_ptr.get();
        if (slot == nullptr) {
            continue;
        }
        const auto *array_type =
            dynamic_cast<const CoreIrArrayType *>(slot->get_allocated_type());
        if (array_type != nullptr && array_type->get_element_count() <= 256 &&
            dynamic_cast<const CoreIrIntegerType *>(array_type->get_element_type()) !=
                nullptr) {
            pattern.array_slot = slot;
            pattern.array_length = array_type->get_element_count();
            break;
        }
    }
    if (pattern.array_slot == nullptr) {
        return false;
    }

    CoreIrBasicBlock *return_block = nullptr;
    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        auto *ret =
            dynamic_cast<CoreIrReturnInst *>(block->get_instructions().back().get());
        if (ret == nullptr || ret->get_return_value() == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            auto *load = dynamic_cast<CoreIrLoadInst *>(instruction_ptr.get());
            if (load != nullptr && ret->get_return_value() == load &&
                load->get_stack_slot() != nullptr) {
                pattern.sum_slot = load->get_stack_slot();
                return_block = block;
                break;
            }
        }
        if (return_block != nullptr) {
            break;
        }
    }
    if (pattern.sum_slot == nullptr || return_block == nullptr) {
        return false;
    }

    CoreIrBasicBlock *store_block = nullptr;
    CoreIrBasicBlock *sum_header = nullptr;
    std::unordered_set<std::uint64_t> seen_indices;
    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr || block->get_instructions().empty()) {
            continue;
        }
        auto *jump = dynamic_cast<CoreIrJumpInst *>(block->get_instructions().back().get());
        if (jump == nullptr) {
            continue;
        }
        std::vector<MonotoneStoreCase> candidate_cases;
        if (!collect_array_store_prefix(*block, pattern.array_slot, candidate_cases)) {
            continue;
        }
        bool duplicate_index = false;
        for (const MonotoneStoreCase &store_case : candidate_cases) {
            duplicate_index =
                duplicate_index || !seen_indices.insert(store_case.index).second;
        }
        if (duplicate_index) {
            return false;
        }
        pattern.cases = std::move(candidate_cases);
        store_block = block;
        sum_header = jump->get_target_block();
        break;
    }
    if (store_block == nullptr || sum_header == nullptr || pattern.cases.empty()) {
        return false;
    }

    if (sum_header->get_instructions().size() < 2) {
        return false;
    }
    auto *sum_header_jump = dynamic_cast<CoreIrJumpInst *>(
        sum_header->get_instructions().back().get());
    auto *sum_body = sum_header_jump == nullptr ? nullptr : sum_header_jump->get_target_block();
    if (sum_header_jump == nullptr || sum_body == nullptr ||
        sum_body->get_instructions().empty()) {
        return false;
    }

    auto *sum_body_branch = dynamic_cast<CoreIrCondJumpInst *>(
        sum_body->get_instructions().back().get());
    if (sum_body_branch == nullptr) {
        return false;
    }
    CoreIrBasicBlock *sum_exit =
        sum_body_branch->get_true_block() == sum_header
            ? sum_body_branch->get_false_block()
            : sum_body_branch->get_true_block();
    if (sum_exit == nullptr || sum_exit->get_instructions().empty()) {
        return false;
    }

    auto *sum_exit_branch = dynamic_cast<CoreIrCondJumpInst *>(
        sum_exit->get_instructions().back().get());
    if (sum_exit_branch == nullptr) {
        return false;
    }
    CoreIrBasicBlock *loop_successor = sum_exit_branch->get_true_block();
    CoreIrBasicBlock *return_successor = sum_exit_branch->get_false_block();
    if (return_successor != return_block && loop_successor == return_block) {
        std::swap(loop_successor, return_successor);
    }
    if (loop_successor != store_block || return_successor != return_block) {
        return false;
    }

    auto *compare = dynamic_cast<CoreIrCompareInst *>(sum_exit_branch->get_condition());
    if (compare == nullptr) {
        return false;
    }
    pattern.n_value = nullptr;
    if (auto *load = dynamic_cast<CoreIrLoadInst *>(compare->get_lhs());
        load != nullptr && load->get_stack_slot() != nullptr) {
        pattern.j_slot = load->get_stack_slot();
        pattern.n_value = compare->get_rhs();
    } else if (auto *load = dynamic_cast<CoreIrLoadInst *>(compare->get_rhs());
               load != nullptr && load->get_stack_slot() != nullptr) {
        pattern.j_slot = load->get_stack_slot();
        pattern.n_value = compare->get_lhs();
    } else if (dynamic_cast<CoreIrParameter *>(compare->get_rhs()) != nullptr) {
        pattern.n_value = compare->get_rhs();
    } else if (dynamic_cast<CoreIrParameter *>(compare->get_lhs()) != nullptr) {
        pattern.n_value = compare->get_lhs();
    }
    if (pattern.n_value == nullptr) {
        return false;
    }

    CoreIrStoreInst *mod_store = nullptr;
    CoreIrBinaryInst *mod_binary = nullptr;
    for (const auto &instruction_ptr : sum_exit->get_instructions()) {
        auto *store = dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
        auto *binary =
            dynamic_cast<CoreIrBinaryInst *>(store == nullptr ? nullptr : store->get_value());
        const auto *mod_constant_candidate =
            binary == nullptr ? nullptr : as_integer_constant(binary->get_rhs());
        if (store != nullptr && store->get_stack_slot() == pattern.sum_slot &&
            binary != nullptr && binary->get_binary_opcode() == CoreIrBinaryOpcode::SRem &&
            mod_constant_candidate != nullptr) {
            mod_store = store;
            mod_binary = binary;
            break;
        }
    }
    const auto *mod_constant =
        mod_binary == nullptr ? nullptr : as_integer_constant(mod_binary->get_rhs());
    if (mod_store == nullptr || mod_store->get_stack_slot() != pattern.sum_slot ||
        mod_binary == nullptr ||
        mod_binary->get_binary_opcode() != CoreIrBinaryOpcode::SRem ||
        mod_constant == nullptr) {
        return false;
    }
    pattern.modulus = static_cast<std::int64_t>(mod_constant->get_value());
    return true;
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
        MonotoneIfArrayReductionPattern compact_pattern;
        if (match_monotone_if_array_reduction_function(function, compact_pattern)) {
            if (rewrite_monotone_if_array_reduction(function, compact_pattern)) {
                changed = true;
                continue;
            }
        }
        MonotoneIfArrayReductionPattern constant_pattern;
        if (match_constant_store_reduction_function(function, constant_pattern)) {
            if (rewrite_monotone_if_array_reduction(function, constant_pattern)) {
                changed = true;
                continue;
            }
        }

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
