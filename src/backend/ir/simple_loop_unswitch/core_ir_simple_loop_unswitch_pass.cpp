#include "backend/ir/simple_loop_unswitch/core_ir_simple_loop_unswitch_pass.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/analysis/scalar_evolution_lite_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::insert_instruction_before;
using sysycc::detail::paths_overlap;
using sysycc::detail::replace_instruction;
using sysycc::detail::trace_stack_slot_prefix;

constexpr std::size_t kMaxUnswitchNestingDepth = 2;
constexpr std::size_t kMaxConditionSliceInstructionCount = 8;

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

bool block_has_phi(const CoreIrBasicBlock &block) {
    for (const auto &instruction : block.get_instructions()) {
        if (instruction == nullptr) {
            continue;
        }
        if (instruction->get_opcode() == CoreIrOpcode::Phi) {
            return true;
        }
        break;
    }
    return false;
}

bool redirect_successor_edge(CoreIrBasicBlock &block, CoreIrBasicBlock *from,
                             CoreIrBasicBlock *to) {
    if (block.get_instructions().empty()) {
        return false;
    }
    CoreIrInstruction *terminator = block.get_instructions().back().get();
    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator);
        jump != nullptr) {
        if (jump->get_target_block() == from) {
            jump->set_target_block(to);
            return true;
        }
        return false;
    }

    auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
    if (cond_jump == nullptr) {
        return false;
    }

    bool changed = false;
    if (cond_jump->get_true_block() == from) {
        cond_jump->set_true_block(to);
        changed = true;
    }
    if (cond_jump->get_false_block() == from) {
        cond_jump->set_false_block(to);
        changed = true;
    }
    return changed;
}

CoreIrBasicBlock *
insert_new_block_before(CoreIrFunction &function, CoreIrBasicBlock *anchor,
                        std::unique_ptr<CoreIrBasicBlock> block) {
    if (anchor == nullptr || block == nullptr) {
        return nullptr;
    }
    block->set_parent(&function);
    CoreIrBasicBlock *block_ptr = block.get();
    auto &blocks = function.get_basic_blocks();
    auto it = std::find_if(
        blocks.begin(), blocks.end(),
        [anchor](const std::unique_ptr<CoreIrBasicBlock> &candidate) {
            return candidate.get() == anchor;
        });
    blocks.insert(it, std::move(block));
    return block_ptr;
}

bool ensure_loop_preheader(CoreIrFunction &function,
                           const CoreIrCfgAnalysisResult &cfg,
                           CoreIrLoopInfo &loop, const CoreIrType *void_type,
                           std::size_t &counter) {
    if (loop.get_preheader() != nullptr) {
        return false;
    }

    CoreIrBasicBlock *header = loop.get_header();
    if (header == nullptr || block_has_phi(*header)) {
        return false;
    }

    std::vector<CoreIrBasicBlock *> outside_preds;
    for (CoreIrBasicBlock *pred : cfg.get_predecessors(header)) {
        if (pred != nullptr && !loop_contains_block(loop, pred)) {
            outside_preds.push_back(pred);
        }
    }
    if (outside_preds.empty()) {
        return false;
    }
    if (outside_preds.size() == 1 &&
        cfg.get_successors(outside_preds.front()).size() == 1) {
        loop.set_preheader(outside_preds.front());
        return false;
    }

    auto preheader = std::make_unique<CoreIrBasicBlock>(
        header->get_name() + ".preheader.unsw." + std::to_string(counter++));
    CoreIrBasicBlock *preheader_ptr =
        insert_new_block_before(function, header, std::move(preheader));
    if (preheader_ptr == nullptr) {
        return false;
    }
    preheader_ptr->create_instruction<CoreIrJumpInst>(void_type, header);
    for (CoreIrBasicBlock *pred : outside_preds) {
        redirect_successor_edge(*pred, header, preheader_ptr);
    }
    loop.set_preheader(preheader_ptr);
    return true;
}

CoreIrBasicBlock *get_use_location_block(const CoreIrUse &use) {
    CoreIrInstruction *user = use.get_user();
    if (user == nullptr) {
        return nullptr;
    }
    if (auto *phi = dynamic_cast<CoreIrPhiInst *>(user); phi != nullptr) {
        return phi->get_incoming_block(use.get_operand_index());
    }
    return user->get_parent();
}

bool instruction_has_external_use(const CoreIrInstruction &instruction,
                                  const CoreIrLoopInfo &loop) {
    for (const CoreIrUse &use : instruction.get_uses()) {
        if (!loop_contains_block(loop, get_use_location_block(use))) {
            return true;
        }
    }
    return false;
}

bool exit_blocks_have_phi(const CoreIrLoopInfo &loop) {
    for (CoreIrBasicBlock *exit_block : loop.get_exit_blocks()) {
        if (exit_block != nullptr && block_has_phi(*exit_block)) {
            return true;
        }
    }
    return false;
}

std::size_t count_loop_instructions(const CoreIrLoopInfo &loop) {
    std::size_t count = 0;
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        count += block->get_instructions().size();
    }
    return count;
}

std::size_t count_unswitch_markers(std::string_view name) {
    std::size_t count = 0;
    std::size_t position = 0;
    while ((position = name.find(".unsw.", position)) !=
           std::string_view::npos) {
        ++count;
        position += 6;
    }
    return count;
}

bool loop_has_external_value_use(const CoreIrLoopInfo &loop) {
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction != nullptr &&
                instruction_has_external_use(*instruction, loop)) {
                return true;
            }
        }
    }
    return false;
}

bool is_safe_address_user(CoreIrInstruction &user, std::size_t operand_index) {
    if (auto *load = dynamic_cast<CoreIrLoadInst *>(&user); load != nullptr) {
        return operand_index == 0 && load->get_address() != nullptr;
    }
    if (auto *store = dynamic_cast<CoreIrStoreInst *>(&user);
        store != nullptr) {
        return operand_index == 1 && store->get_address() != nullptr;
    }
    if (dynamic_cast<CoreIrGetElementPtrInst *>(&user) != nullptr) {
        return operand_index == 0;
    }
    return false;
}

bool loop_has_unsafe_address_use(const CoreIrLoopInfo &loop,
                                 const CoreIrStackSlot *stack_slot,
                                 const std::vector<std::uint64_t> &path) {
    if (stack_slot == nullptr) {
        return true;
    }
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }

            if (auto *address =
                    dynamic_cast<CoreIrAddressOfStackSlotInst *>(instruction);
                address != nullptr && address->get_stack_slot() == stack_slot) {
                for (const CoreIrUse &use : address->get_uses()) {
                    CoreIrInstruction *user = use.get_user();
                    if (user != nullptr && user->get_parent() != nullptr &&
                        loop_contains_block(loop, user->get_parent()) &&
                        !is_safe_address_user(*user, use.get_operand_index())) {
                        return true;
                    }
                }
                continue;
            }

            auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(instruction);
            if (gep == nullptr) {
                continue;
            }
            CoreIrStackSlot *root_slot = nullptr;
            std::vector<std::uint64_t> prefix_path;
            bool exact_path = true;
            if (!trace_stack_slot_prefix(gep, root_slot, prefix_path,
                                         exact_path) ||
                root_slot != stack_slot || !paths_overlap(path, prefix_path)) {
                continue;
            }
            for (const CoreIrUse &use : gep->get_uses()) {
                CoreIrInstruction *user = use.get_user();
                if (user != nullptr && user->get_parent() != nullptr &&
                    loop_contains_block(loop, user->get_parent()) &&
                    !is_safe_address_user(*user, use.get_operand_index())) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool loop_writes_stack_slot_path(const CoreIrLoopInfo &loop,
                                 const CoreIrStackSlot *stack_slot,
                                 const std::vector<std::uint64_t> &path) {
    if (stack_slot == nullptr) {
        return true;
    }
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            auto *store =
                dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
            if (store == nullptr) {
                continue;
            }
            if (store->get_stack_slot() == stack_slot) {
                return true;
            }
            CoreIrStackSlot *root_slot = nullptr;
            std::vector<std::uint64_t> store_path;
            bool exact_path = true;
            if (!trace_stack_slot_prefix(store->get_address(), root_slot,
                                         store_path, exact_path) ||
                root_slot != stack_slot) {
                continue;
            }
            if (!exact_path || paths_overlap(path, store_path)) {
                return true;
            }
        }
    }
    return false;
}

bool instruction_is_condition_slice_cloneable(
    const CoreIrInstruction &instruction, const CoreIrLoopInfo &loop);

bool value_is_condition_slice_cloneable(
    CoreIrValue *value, const CoreIrLoopInfo &loop,
    std::unordered_set<const CoreIrInstruction *> &visiting,
    std::unordered_set<const CoreIrInstruction *> &collected,
    std::vector<const CoreIrInstruction *> &order) {
    if (value == nullptr) {
        return false;
    }
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return true;
    }
    if (!loop_contains_block(loop, instruction->get_parent())) {
        return true;
    }
    if (!visiting.insert(instruction).second) {
        return false;
    }
    if (!instruction_is_condition_slice_cloneable(*instruction, loop)) {
        visiting.erase(instruction);
        return false;
    }
    for (CoreIrValue *operand : instruction->get_operands()) {
        if (!value_is_condition_slice_cloneable(operand, loop, visiting,
                                                collected, order)) {
            visiting.erase(instruction);
            return false;
        }
    }
    visiting.erase(instruction);
    if (collected.insert(instruction).second) {
        order.push_back(instruction);
    }
    return true;
}

bool instruction_is_condition_slice_cloneable(
    const CoreIrInstruction &instruction, const CoreIrLoopInfo &loop) {
    if (instruction.get_is_terminator() ||
        instruction.get_opcode() == CoreIrOpcode::Phi) {
        return false;
    }
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::GetElementPtr:
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Select:
    case CoreIrOpcode::Cast:
        return true;
    case CoreIrOpcode::Load: {
        const auto &load = static_cast<const CoreIrLoadInst &>(instruction);
        if (load.get_stack_slot() != nullptr) {
            const std::vector<std::uint64_t> direct_path;
            return !loop_writes_stack_slot_path(loop, load.get_stack_slot(),
                                                direct_path) &&
                   !loop_has_unsafe_address_use(loop, load.get_stack_slot(),
                                                direct_path);
        }

        CoreIrStackSlot *stack_slot = nullptr;
        std::vector<std::uint64_t> path;
        bool exact_path = true;
        return trace_stack_slot_prefix(load.get_address(), stack_slot, path,
                                       exact_path) &&
               exact_path &&
               !loop_writes_stack_slot_path(loop, stack_slot, path) &&
               !loop_has_unsafe_address_use(loop, stack_slot, path);
    }
    case CoreIrOpcode::Phi:
        return false;
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Call:
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::Return:
        return false;
    }
    return false;
}

CoreIrInstruction *
clone_instruction_preserving_operands(const CoreIrInstruction &instruction) {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Phi: {
        const auto &phi = static_cast<const CoreIrPhiInst &>(instruction);
        auto clone =
            std::make_unique<CoreIrPhiInst>(phi.get_type(), phi.get_name());
        clone->set_source_span(phi.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Binary: {
        const auto &binary = static_cast<const CoreIrBinaryInst &>(instruction);
        auto clone = std::make_unique<CoreIrBinaryInst>(
            binary.get_binary_opcode(), binary.get_type(), binary.get_name(),
            binary.get_lhs(), binary.get_rhs());
        clone->set_source_span(binary.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Unary: {
        const auto &unary = static_cast<const CoreIrUnaryInst &>(instruction);
        auto clone = std::make_unique<CoreIrUnaryInst>(
            unary.get_unary_opcode(), unary.get_type(), unary.get_name(),
            unary.get_operand());
        clone->set_source_span(unary.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Compare: {
        const auto &compare =
            static_cast<const CoreIrCompareInst &>(instruction);
        auto clone = std::make_unique<CoreIrCompareInst>(
            compare.get_predicate(), compare.get_type(), compare.get_name(),
            compare.get_lhs(), compare.get_rhs());
        clone->set_source_span(compare.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Select: {
        const auto &select = static_cast<const CoreIrSelectInst &>(instruction);
        auto clone = std::make_unique<CoreIrSelectInst>(
            select.get_type(), select.get_name(), select.get_condition(),
            select.get_true_value(), select.get_false_value());
        clone->set_source_span(select.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Cast: {
        const auto &cast = static_cast<const CoreIrCastInst &>(instruction);
        auto clone = std::make_unique<CoreIrCastInst>(
            cast.get_cast_kind(), cast.get_type(), cast.get_name(),
            cast.get_operand());
        clone->set_source_span(cast.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::AddressOfFunction: {
        const auto &address =
            static_cast<const CoreIrAddressOfFunctionInst &>(instruction);
        auto clone = std::make_unique<CoreIrAddressOfFunctionInst>(
            address.get_type(), address.get_name(), address.get_function());
        clone->set_source_span(address.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::AddressOfGlobal: {
        const auto &address =
            static_cast<const CoreIrAddressOfGlobalInst &>(instruction);
        auto clone = std::make_unique<CoreIrAddressOfGlobalInst>(
            address.get_type(), address.get_name(), address.get_global());
        clone->set_source_span(address.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::AddressOfStackSlot: {
        const auto &address =
            static_cast<const CoreIrAddressOfStackSlotInst &>(instruction);
        auto clone = std::make_unique<CoreIrAddressOfStackSlotInst>(
            address.get_type(), address.get_name(), address.get_stack_slot());
        clone->set_source_span(address.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::GetElementPtr: {
        const auto &gep =
            static_cast<const CoreIrGetElementPtrInst &>(instruction);
        std::vector<CoreIrValue *> indices;
        indices.reserve(gep.get_index_count());
        for (std::size_t index = 0; index < gep.get_index_count(); ++index) {
            indices.push_back(gep.get_index(index));
        }
        auto clone = std::make_unique<CoreIrGetElementPtrInst>(
            gep.get_type(), gep.get_name(), gep.get_base(), std::move(indices));
        clone->set_source_span(gep.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Load: {
        const auto &load = static_cast<const CoreIrLoadInst &>(instruction);
        std::unique_ptr<CoreIrLoadInst> clone;
        if (load.get_stack_slot() != nullptr) {
            clone = std::make_unique<CoreIrLoadInst>(
                load.get_type(), load.get_name(), load.get_stack_slot());
        } else {
            clone = std::make_unique<CoreIrLoadInst>(
                load.get_type(), load.get_name(), load.get_address());
        }
        clone->set_source_span(load.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Store: {
        const auto &store = static_cast<const CoreIrStoreInst &>(instruction);
        std::unique_ptr<CoreIrStoreInst> clone;
        if (store.get_stack_slot() != nullptr) {
            clone = std::make_unique<CoreIrStoreInst>(
                store.get_type(), store.get_value(), store.get_stack_slot());
        } else {
            clone = std::make_unique<CoreIrStoreInst>(
                store.get_type(), store.get_value(), store.get_address());
        }
        clone->set_source_span(store.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Call: {
        const auto &call = static_cast<const CoreIrCallInst &>(instruction);
        std::vector<CoreIrValue *> arguments;
        const auto &operands = call.get_operands();
        for (std::size_t index = call.get_argument_begin_index();
             index < operands.size(); ++index) {
            arguments.push_back(operands[index]);
        }
        std::unique_ptr<CoreIrCallInst> clone;
        if (call.get_is_direct_call()) {
            clone = std::make_unique<CoreIrCallInst>(
                call.get_type(), call.get_name(), call.get_callee_name(),
                call.get_callee_type(), std::move(arguments));
        } else {
            clone = std::make_unique<CoreIrCallInst>(
                call.get_type(), call.get_name(), call.get_callee_value(),
                call.get_callee_type(), std::move(arguments));
        }
        clone->set_source_span(call.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Jump: {
        const auto &jump = static_cast<const CoreIrJumpInst &>(instruction);
        auto clone = std::make_unique<CoreIrJumpInst>(jump.get_type(),
                                                      jump.get_target_block());
        clone->set_source_span(jump.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::CondJump: {
        const auto &jump = static_cast<const CoreIrCondJumpInst &>(instruction);
        auto clone = std::make_unique<CoreIrCondJumpInst>(
            jump.get_type(), jump.get_condition(), jump.get_true_block(),
            jump.get_false_block());
        clone->set_source_span(jump.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Return: {
        const auto &ret = static_cast<const CoreIrReturnInst &>(instruction);
        std::unique_ptr<CoreIrReturnInst> clone;
        if (ret.get_return_value() == nullptr) {
            clone = std::make_unique<CoreIrReturnInst>(ret.get_type());
        } else {
            clone = std::make_unique<CoreIrReturnInst>(ret.get_type(),
                                                       ret.get_return_value());
        }
        clone->set_source_span(ret.get_source_span());
        return clone.release();
    }
    }
    return nullptr;
}

struct LoopUnswitchCandidate {
    CoreIrBasicBlock *condition_block = nullptr;
    std::vector<const CoreIrInstruction *> condition_slice;
};

std::optional<LoopUnswitchCandidate> find_loop_body_unswitch_candidate(
    CoreIrFunction &function, const CoreIrLoopInfo &loop,
    const CoreIrScalarEvolutionLiteAnalysisResult &scev) {
    if (loop.get_preheader() == nullptr || exit_blocks_have_phi(loop) ||
        loop_has_external_value_use(loop) || loop.get_blocks().size() > 256 ||
        count_loop_instructions(loop) > 4096 ||
        count_unswitch_markers(loop.get_header()->get_name()) >=
            kMaxUnswitchNestingDepth) {
        return std::nullopt;
    }

    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr || block == loop.get_header() ||
            !loop_contains_block(loop, block) ||
            block->get_instructions().empty()) {
            continue;
        }

        auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
            block->get_instructions().back().get());
        if (branch == nullptr) {
            continue;
        }
        if ((loop_contains_block(loop, branch->get_true_block()) &&
             block_has_phi(*branch->get_true_block())) ||
            (loop_contains_block(loop, branch->get_false_block()) &&
             block_has_phi(*branch->get_false_block()))) {
            continue;
        }

        std::unordered_set<const CoreIrInstruction *> visiting;
        std::unordered_set<const CoreIrInstruction *> collected;
        std::vector<const CoreIrInstruction *> order;
        if (!value_is_condition_slice_cloneable(branch->get_condition(), loop,
                                                visiting, collected, order)) {
            continue;
        }
        if (order.size() > kMaxConditionSliceInstructionCount) {
            continue;
        }
        return LoopUnswitchCandidate{block, std::move(order)};
    }

    return std::nullopt;
}

CoreIrInstruction *clone_condition_slice_into_preheader(
    CoreIrBasicBlock &preheader,
    const std::vector<const CoreIrInstruction *> &condition_slice,
    CoreIrInstruction *condition_instruction) {
    CoreIrInstruction *anchor = preheader.get_instructions().back().get();
    std::unordered_map<const CoreIrInstruction *, CoreIrInstruction *>
        clone_map;

    for (const CoreIrInstruction *instruction : condition_slice) {
        if (instruction == nullptr) {
            return nullptr;
        }
        CoreIrInstruction *clone =
            clone_instruction_preserving_operands(*instruction);
        if (clone == nullptr) {
            return nullptr;
        }
        clone_map.emplace(
            instruction,
            insert_instruction_before(
                preheader, anchor, std::unique_ptr<CoreIrInstruction>(clone)));
    }

    for (const auto &entry : clone_map) {
        CoreIrInstruction *clone = entry.second;
        if (clone == nullptr) {
            return nullptr;
        }
        const auto &operands = clone->get_operands();
        for (std::size_t index = 0; index < operands.size(); ++index) {
            auto it = clone_map.find(
                dynamic_cast<const CoreIrInstruction *>(operands[index]));
            if (it != clone_map.end()) {
                clone->set_operand(index, it->second);
            }
        }
    }

    auto it = clone_map.find(condition_instruction);
    return it == clone_map.end() ? nullptr : it->second;
}

struct ClonedLoopVariant {
    std::unordered_map<CoreIrBasicBlock *, CoreIrBasicBlock *> block_map;
    std::unordered_map<const CoreIrInstruction *, CoreIrInstruction *>
        instruction_map;
    CoreIrBasicBlock *header = nullptr;
};

std::optional<ClonedLoopVariant>
clone_loop_variant(CoreIrFunction &function, const CoreIrLoopInfo &loop,
                   CoreIrBasicBlock &condition_block, bool take_true,
                   std::string_view suffix) {
    ClonedLoopVariant variant;
    std::vector<CoreIrBasicBlock *> original_blocks;
    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block != nullptr && loop_contains_block(loop, block)) {
            original_blocks.push_back(block);
        }
    }

    for (CoreIrBasicBlock *block : original_blocks) {
        auto clone = std::make_unique<CoreIrBasicBlock>(
            block->get_name() + "." + std::string(suffix));
        CoreIrBasicBlock *clone_ptr =
            function.append_basic_block(std::move(clone));
        variant.block_map.emplace(block, clone_ptr);
        if (block == loop.get_header()) {
            variant.header = clone_ptr;
        }
    }

    for (CoreIrBasicBlock *block : original_blocks) {
        CoreIrBasicBlock *clone_block = variant.block_map[block];
        if (clone_block == nullptr) {
            return std::nullopt;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }
            CoreIrInstruction *clone =
                clone_instruction_preserving_operands(*instruction);
            if (clone == nullptr) {
                return std::nullopt;
            }
            clone_block->append_instruction(
                std::unique_ptr<CoreIrInstruction>(clone));
            variant.instruction_map.emplace(instruction, clone);
        }
    }

    for (const auto &entry : variant.instruction_map) {
        CoreIrInstruction *original =
            const_cast<CoreIrInstruction *>(entry.first);
        CoreIrInstruction *clone = entry.second;
        if (original == nullptr || clone == nullptr) {
            return std::nullopt;
        }
        const auto &operands = clone->get_operands();
        for (std::size_t index = 0; index < operands.size(); ++index) {
            auto it = variant.instruction_map.find(
                dynamic_cast<const CoreIrInstruction *>(operands[index]));
            if (it != variant.instruction_map.end()) {
                clone->set_operand(index, it->second);
            }
        }

        if (auto *jump = dynamic_cast<CoreIrJumpInst *>(clone);
            jump != nullptr) {
            CoreIrBasicBlock *target =
                static_cast<CoreIrJumpInst *>(original)->get_target_block();
            auto block_it = variant.block_map.find(target);
            if (block_it != variant.block_map.end()) {
                jump->set_target_block(block_it->second);
            }
            continue;
        }

        if (auto *phi = dynamic_cast<CoreIrPhiInst *>(clone); phi != nullptr) {
            const auto &original_phi =
                static_cast<const CoreIrPhiInst &>(*original);
            for (std::size_t index = 0;
                 index < original_phi.get_incoming_count(); ++index) {
                CoreIrBasicBlock *incoming_block =
                    original_phi.get_incoming_block(index);
                CoreIrValue *incoming_value =
                    original_phi.get_incoming_value(index);
                auto block_it = variant.block_map.find(incoming_block);
                if (block_it != variant.block_map.end()) {
                    incoming_block = block_it->second;
                }
                auto value_it = variant.instruction_map.find(
                    dynamic_cast<const CoreIrInstruction *>(incoming_value));
                if (value_it != variant.instruction_map.end()) {
                    incoming_value = value_it->second;
                }
                phi->add_incoming(incoming_block, incoming_value);
            }
            continue;
        }

        auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(clone);
        if (cond_jump == nullptr) {
            continue;
        }
        auto *original_cond_jump = static_cast<CoreIrCondJumpInst *>(original);
        CoreIrBasicBlock *true_block = original_cond_jump->get_true_block();
        CoreIrBasicBlock *false_block = original_cond_jump->get_false_block();
        if (CoreIrBasicBlock *mapped_true =
                variant.block_map.count(true_block) > 0
                    ? variant.block_map[true_block]
                    : nullptr;
            mapped_true != nullptr) {
            cond_jump->set_true_block(mapped_true);
        }
        if (CoreIrBasicBlock *mapped_false =
                variant.block_map.count(false_block) > 0
                    ? variant.block_map[false_block]
                    : nullptr;
            mapped_false != nullptr) {
            cond_jump->set_false_block(mapped_false);
        }

        if (original->get_parent() != &condition_block) {
            continue;
        }

        CoreIrBasicBlock *selected_successor =
            take_true ? cond_jump->get_true_block()
                      : cond_jump->get_false_block();
        auto replacement = std::make_unique<CoreIrJumpInst>(
            cond_jump->get_type(), selected_successor);
        replacement->set_source_span(cond_jump->get_source_span());
        replace_instruction(*clone->get_parent(), clone,
                            std::move(replacement));
    }

    return variant.header == nullptr
               ? std::nullopt
               : std::optional<ClonedLoopVariant>(std::move(variant));
}

bool unswitch_loop_body_condition(
    CoreIrFunction &function, const CoreIrLoopInfo &loop,
    const CoreIrScalarEvolutionLiteAnalysisResult &scev) {
    const std::optional<LoopUnswitchCandidate> candidate =
        find_loop_body_unswitch_candidate(function, loop, scev);
    if (!candidate.has_value() || candidate->condition_block == nullptr ||
        candidate->condition_block->get_instructions().empty()) {
        return false;
    }

    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        loop.get_preheader()->get_instructions().empty()
            ? nullptr
            : loop.get_preheader()->get_instructions().back().get());
    auto *condition_branch = dynamic_cast<CoreIrCondJumpInst *>(
        candidate->condition_block->get_instructions().back().get());
    auto *condition_instruction = dynamic_cast<CoreIrInstruction *>(
        condition_branch == nullptr ? nullptr
                                    : condition_branch->get_condition());
    if (preheader_jump == nullptr || condition_branch == nullptr ||
        preheader_jump->get_target_block() != loop.get_header() ||
        condition_instruction == nullptr) {
        return false;
    }

    const auto true_variant = clone_loop_variant(
        function, loop, *candidate->condition_block, true, "unsw.true");
    const auto false_variant = clone_loop_variant(
        function, loop, *candidate->condition_block, false, "unsw.false");
    if (!true_variant.has_value() || !false_variant.has_value()) {
        return false;
    }

    CoreIrInstruction *cloned_condition = clone_condition_slice_into_preheader(
        *loop.get_preheader(), candidate->condition_slice,
        condition_instruction);
    if (cloned_condition == nullptr) {
        return false;
    }

    auto replacement = std::make_unique<CoreIrCondJumpInst>(
        preheader_jump->get_type(), cloned_condition, true_variant->header,
        false_variant->header);
    replacement->set_source_span(preheader_jump->get_source_span());
    replace_instruction(*loop.get_preheader(), preheader_jump,
                        std::move(replacement));

    CoreIrBasicBlock *header = loop.get_header();
    if (header != nullptr) {
        for (const auto &instruction_ptr : header->get_instructions()) {
            auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
            if (phi == nullptr) {
                break;
            }
            phi->remove_incoming_block(loop.get_preheader());
        }
    }
    return true;
}

bool redirect_preheader_entry_to_body(
    const CoreIrLoopInfo &loop, CoreIrBasicBlock &header,
    CoreIrBasicBlock &preheader, CoreIrBasicBlock *inside_successor,
    CoreIrBasicBlock *outside_successor,
    const CoreIrScalarEvolutionLiteAnalysisResult &scev) {
    if (inside_successor == nullptr || outside_successor == nullptr ||
        preheader.get_instructions().empty() ||
        header.get_instructions().empty()) {
        return false;
    }

    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        header.get_instructions().back().get());
    if (header_branch == nullptr ||
        !scev.is_loop_invariant(header_branch->get_condition(), loop)) {
        return false;
    }

    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        preheader.get_instructions().back().get());
    if (preheader_jump == nullptr ||
        preheader_jump->get_target_block() != &header) {
        return false;
    }

    if (block_has_phi(header) || block_has_phi(*inside_successor) ||
        block_has_phi(*outside_successor)) {
        return false;
    }

    CoreIrValue *condition = header_branch->get_condition();
    preheader_jump->detach_operands();
    auto replacement = std::make_unique<CoreIrCondJumpInst>(
        header_branch->get_type(), condition, inside_successor,
        outside_successor);
    replacement->set_source_span(header_branch->get_source_span());
    replacement->set_parent(&preheader);
    preheader.get_instructions().back() = std::move(replacement);

    header_branch->detach_operands();
    auto header_replacement = std::make_unique<CoreIrJumpInst>(
        header_branch->get_type(), inside_successor);
    header_replacement->set_source_span(header_branch->get_source_span());
    header_replacement->set_parent(&header);
    header.get_instructions().back() = std::move(header_replacement);
    return true;
}

bool unswitch_loop_header(CoreIrFunction &function, const CoreIrLoopInfo &loop,
                          const CoreIrScalarEvolutionLiteAnalysisResult &scev) {
    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *preheader = loop.get_preheader();
    if (header == nullptr || preheader == nullptr ||
        header->get_instructions().empty()) {
        return false;
    }

    auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
        header->get_instructions().back().get());
    if (branch == nullptr) {
        return false;
    }

    CoreIrBasicBlock *inside_successor = nullptr;
    CoreIrBasicBlock *outside_successor = nullptr;
    if (loop_contains_block(loop, branch->get_true_block()) &&
        !loop_contains_block(loop, branch->get_false_block())) {
        inside_successor = branch->get_true_block();
        outside_successor = branch->get_false_block();
    } else if (!loop_contains_block(loop, branch->get_true_block()) &&
               loop_contains_block(loop, branch->get_false_block())) {
        inside_successor = branch->get_false_block();
        outside_successor = branch->get_true_block();
    } else {
        return false;
    }

    return redirect_preheader_entry_to_body(
        loop, *header, *preheader, inside_successor, outside_successor, scev);
}

} // namespace

PassKind CoreIrSimpleLoopUnswitchPass::Kind() const {
    return PassKind::CoreIrSimpleLoopUnswitch;
}

const char *CoreIrSimpleLoopUnswitchPass::Name() const {
    return "CoreIrSimpleLoopUnswitchPass";
}

PassResult CoreIrSimpleLoopUnswitchPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager =
        build_result->get_analysis_manager();
    CoreIrContext *core_ir_context = build_result->get_context();
    if (analysis_manager == nullptr || core_ir_context == nullptr) {
        return PassResult::Failure(
            "missing core ir simple loop unswitch dependencies");
    }
    const CoreIrType *void_type =
        core_ir_context->create_type<CoreIrVoidType>();

    CoreIrPassEffects effects;
    std::size_t preheader_counter = 0;
    for (const auto &function : module->get_functions()) {
        const CoreIrCfgAnalysisResult &cfg =
            analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
        const CoreIrScalarEvolutionLiteAnalysisResult &scev =
            analysis_manager->get_or_compute<CoreIrScalarEvolutionLiteAnalysis>(
                *function);

        bool function_changed = false;
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr == nullptr) {
                continue;
            }
            function_changed =
                ensure_loop_preheader(*function, cfg, *loop_ptr, void_type,
                                      preheader_counter) ||
                function_changed;
            bool loop_changed =
                unswitch_loop_header(*function, *loop_ptr, scev);
            loop_changed =
                unswitch_loop_body_condition(*function, *loop_ptr, scev) ||
                loop_changed;
            function_changed = loop_changed || function_changed;
        }

        if (function_changed) {
            effects.changed_functions.insert(function.get());
            effects.cfg_changed_functions.insert(function.get());
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
