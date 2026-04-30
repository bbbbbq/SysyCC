#include "backend/ir/analysis/promotable_stack_slot_analysis.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominance_frontier_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

struct CandidateAccess {
    CoreIrInstruction *instruction = nullptr;
    CoreIrBasicBlock *block = nullptr;
    CoreIrStackSlot *stack_slot = nullptr;
    std::vector<std::uint64_t> access_path;
    const CoreIrType *value_type = nullptr;
    bool is_store = false;
};

struct PathCandidateGroup {
    std::vector<CandidateAccess> accesses;
    const CoreIrType *value_type = nullptr;
    bool invalid = false;
};

struct BlockSummary {
    bool has_def = false;
    bool has_use_before_def = false;
};

struct PromotableCfgWorkset {
    static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

    std::vector<const CoreIrBasicBlock *> blocks;
    std::unordered_map<const CoreIrBasicBlock *, std::size_t> block_indices;
    std::vector<std::size_t> reachable_indices;
    std::vector<std::vector<std::size_t>> predecessor_indices;
    std::vector<std::vector<std::size_t>> successor_indices;
    std::size_t entry_index = npos;
};

PromotableCfgWorkset
build_promotable_cfg_workset(const CoreIrFunction &function,
                             const CoreIrCfgAnalysisResult &cfg_analysis) {
    PromotableCfgWorkset workset;
    const auto &function_blocks = function.get_basic_blocks();
    workset.blocks.reserve(function_blocks.size());
    workset.block_indices.reserve(function_blocks.size());

    for (const auto &block : function_blocks) {
        if (block == nullptr) {
            continue;
        }
        const std::size_t index = workset.blocks.size();
        workset.blocks.push_back(block.get());
        workset.block_indices.emplace(block.get(), index);
        if (block.get() == cfg_analysis.get_entry_block()) {
            workset.entry_index = index;
        }
    }

    workset.predecessor_indices.resize(workset.blocks.size());
    workset.successor_indices.resize(workset.blocks.size());
    workset.reachable_indices.reserve(workset.blocks.size());
    for (std::size_t index = 0; index < workset.blocks.size(); ++index) {
        const CoreIrBasicBlock *block = workset.blocks[index];
        if (cfg_analysis.is_reachable(block)) {
            workset.reachable_indices.push_back(index);
        }
        for (CoreIrBasicBlock *predecessor : cfg_analysis.get_predecessors(block)) {
            auto predecessor_it = workset.block_indices.find(predecessor);
            if (predecessor_it != workset.block_indices.end()) {
                workset.predecessor_indices[index].push_back(predecessor_it->second);
            }
        }
        for (CoreIrBasicBlock *successor : cfg_analysis.get_successors(block)) {
            auto successor_it = workset.block_indices.find(successor);
            if (successor_it != workset.block_indices.end()) {
                workset.successor_indices[index].push_back(successor_it->second);
            }
        }
    }

    return workset;
}

struct BlockedPrefix {
    std::vector<std::uint64_t> access_path;
    CoreIrPromotionFailureReason reason = CoreIrPromotionFailureReason::AddressEscaped;
};

CoreIrPromotionFailureReason classify_non_load_store_user(
    const CoreIrInstruction &instruction);

CoreIrPromotionFailureReason classify_non_load_store_user_or_default(
    const CoreIrInstruction *user) {
    if (user == nullptr) {
        return CoreIrPromotionFailureReason::AddressEscaped;
    }
    return classify_non_load_store_user(*user);
}

PathCandidateGroup &get_or_create_path_group(
    std::vector<PathCandidateGroup> &path_groups,
    const std::vector<std::uint64_t> &access_path,
    const CoreIrType *value_type) {
    for (PathCandidateGroup &group : path_groups) {
        if (!group.accesses.empty() &&
            group.accesses.front().access_path == access_path) {
            return group;
        }
    }
    PathCandidateGroup group;
    group.value_type = value_type;
    path_groups.push_back(std::move(group));
    return path_groups.back();
}

bool is_scalar_promotable_type(const CoreIrType *type) {
    if (type == nullptr) {
        return false;
    }
    switch (type->get_kind()) {
    case CoreIrTypeKind::Integer:
    case CoreIrTypeKind::Float:
    case CoreIrTypeKind::Pointer:
        return true;
    case CoreIrTypeKind::Void:
    case CoreIrTypeKind::Vector:
    case CoreIrTypeKind::Array:
    case CoreIrTypeKind::Struct:
    case CoreIrTypeKind::Function:
        return false;
    }
    return false;
}

const CoreIrType *resolve_access_path_type(const CoreIrType *root_type,
                                           const std::vector<std::uint64_t> &path) {
    const CoreIrType *current = root_type;
    for (std::uint64_t index : path) {
        if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(current);
            array_type != nullptr) {
            current = array_type->get_element_type();
            continue;
        }
        if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(current);
            struct_type != nullptr) {
            if (index >= struct_type->get_element_types().size()) {
                return nullptr;
            }
            current = struct_type->get_element_types()[index];
            continue;
        }
        return nullptr;
    }
    return current;
}

bool normalize_constant_gep_path(CoreIrValue *value, CoreIrStackSlot *&stack_slot,
                                 std::vector<std::uint64_t> &path) {
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return false;
    }

    if (instruction->get_opcode() == CoreIrOpcode::AddressOfStackSlot) {
        auto *address = static_cast<CoreIrAddressOfStackSlotInst *>(instruction);
        stack_slot = address->get_stack_slot();
        return stack_slot != nullptr;
    }

    if (instruction->get_opcode() != CoreIrOpcode::GetElementPtr) {
        return false;
    }
    auto *gep = static_cast<CoreIrGetElementPtrInst *>(instruction);

    std::vector<std::uint64_t> base_path;
    if (!normalize_constant_gep_path(gep->get_base(), stack_slot, base_path) ||
        stack_slot == nullptr) {
        return false;
    }

    for (std::size_t index = 0; index < gep->get_index_count(); ++index) {
        const auto *constant_index =
            dynamic_cast<const CoreIrConstantInt *>(gep->get_index(index));
        if (constant_index == nullptr) {
            return false;
        }
        base_path.push_back(constant_index->get_value());
    }

    if (!base_path.empty() && base_path.front() == 0) {
        base_path.erase(base_path.begin());
    }
    path = std::move(base_path);
    return true;
}

bool trace_stack_slot_prefix(CoreIrValue *value, CoreIrStackSlot *&stack_slot,
                             std::vector<std::uint64_t> &path, bool &is_exact) {
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return false;
    }

    if (instruction->get_opcode() == CoreIrOpcode::AddressOfStackSlot) {
        auto *address = static_cast<CoreIrAddressOfStackSlotInst *>(instruction);
        stack_slot = address->get_stack_slot();
        is_exact = stack_slot != nullptr;
        return stack_slot != nullptr;
    }

    if (instruction->get_opcode() != CoreIrOpcode::GetElementPtr) {
        return false;
    }
    auto *gep = static_cast<CoreIrGetElementPtrInst *>(instruction);

    std::vector<std::uint64_t> base_path;
    if (!trace_stack_slot_prefix(gep->get_base(), stack_slot, base_path, is_exact) ||
        stack_slot == nullptr) {
        return false;
    }

    for (std::size_t index = 0; index < gep->get_index_count(); ++index) {
        const auto *constant_index =
            dynamic_cast<const CoreIrConstantInt *>(gep->get_index(index));
        if (constant_index == nullptr) {
            is_exact = false;
            break;
        }
        base_path.push_back(constant_index->get_value());
    }

    if (!base_path.empty() && base_path.front() == 0) {
        base_path.erase(base_path.begin());
    }
    path = std::move(base_path);
    return true;
}

CoreIrPromotionFailureReason classify_non_load_store_user(
    const CoreIrInstruction &instruction) {
    if (instruction.get_opcode() == CoreIrOpcode::GetElementPtr) {
        return CoreIrPromotionFailureReason::DynamicIndex;
    }
    return CoreIrPromotionFailureReason::NonLoadStoreUser;
}

bool is_safe_address_user(CoreIrInstruction &user, std::size_t operand_index) {
    switch (user.get_opcode()) {
    case CoreIrOpcode::Load: {
        auto *load = static_cast<CoreIrLoadInst *>(&user);
        return operand_index == 0 && load->get_address() != nullptr;
    }
    case CoreIrOpcode::Store: {
        auto *store = static_cast<CoreIrStoreInst *>(&user);
        return operand_index == 1 && store->get_address() != nullptr;
    }
    case CoreIrOpcode::GetElementPtr:
        return operand_index == 0;
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
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::Call:
    case CoreIrOpcode::DynamicAlloca:
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::IndirectJump:
    case CoreIrOpcode::Return:
        return false;
    }
    return false;
}

void add_blocked_prefix(
    std::unordered_map<CoreIrStackSlot *, std::vector<BlockedPrefix>> &blocked_prefixes,
    CoreIrStackSlot *stack_slot, std::vector<std::uint64_t> access_path,
    CoreIrPromotionFailureReason reason) {
    if (stack_slot == nullptr) {
        return;
    }
    blocked_prefixes[stack_slot].push_back(
        BlockedPrefix{std::move(access_path), reason});
}

bool paths_overlap(const std::vector<std::uint64_t> &lhs,
                   const std::vector<std::uint64_t> &rhs) {
    const std::size_t shared = std::min(lhs.size(), rhs.size());
    for (std::size_t index = 0; index < shared; ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

bool unit_is_definitely_defined_on_all_paths(
    const CoreIrPromotionUnitInfo &unit_info,
    const CoreIrDominanceFrontierAnalysisResult &dominance_frontier,
    const PromotableCfgWorkset &cfg_workset,
    const std::unordered_map<const CoreIrInstruction *, std::size_t>
        &instruction_order) {
    if (unit_info.loads.empty()) {
        return true;
    }
    if (unit_info.stores.empty()) {
        return false;
    }
    if (cfg_workset.entry_index == PromotableCfgWorkset::npos) {
        return false;
    }

    struct OrderedAccess {
        std::size_t order = 0;
        bool is_store = false;
    };
    std::vector<std::vector<OrderedAccess>> accesses_by_block(
        cfg_workset.blocks.size());
    std::vector<bool> is_reachable(cfg_workset.blocks.size(), false);
    for (std::size_t block_index : cfg_workset.reachable_indices) {
        if (block_index < is_reachable.size()) {
            is_reachable[block_index] = true;
        }
    }
    for (CoreIrLoadInst *load : unit_info.loads) {
        if (load == nullptr || load->get_parent() == nullptr) {
            continue;
        }
        auto order_it = instruction_order.find(load);
        if (order_it == instruction_order.end()) {
            continue;
        }
        auto block_it = cfg_workset.block_indices.find(load->get_parent());
        if (block_it != cfg_workset.block_indices.end() &&
            is_reachable[block_it->second]) {
            accesses_by_block[block_it->second].push_back(
                OrderedAccess{order_it->second, false});
        }
    }
    for (CoreIrStoreInst *store : unit_info.stores) {
        if (store == nullptr || store->get_parent() == nullptr) {
            continue;
        }
        auto order_it = instruction_order.find(store);
        if (order_it == instruction_order.end()) {
            continue;
        }
        auto block_it = cfg_workset.block_indices.find(store->get_parent());
        if (block_it != cfg_workset.block_indices.end() &&
            is_reachable[block_it->second]) {
            accesses_by_block[block_it->second].push_back(
                OrderedAccess{order_it->second, true});
        }
    }

    std::vector<BlockSummary> block_summaries(cfg_workset.blocks.size());
    bool has_use_before_def = false;
    for (std::size_t block_index = 0; block_index < accesses_by_block.size();
         ++block_index) {
        std::vector<OrderedAccess> &accesses = accesses_by_block[block_index];
        if (accesses.empty()) {
            continue;
        }
        std::sort(accesses.begin(), accesses.end(),
                  [](const OrderedAccess &lhs, const OrderedAccess &rhs) {
                      return lhs.order < rhs.order;
                  });

        BlockSummary summary;
        bool locally_defined = false;
        for (const OrderedAccess &access : accesses) {
            if (!access.is_store && !locally_defined) {
                summary.has_use_before_def = true;
            }
            if (access.is_store) {
                locally_defined = true;
                summary.has_def = true;
            }
        }
        block_summaries[block_index] = summary;
        has_use_before_def = has_use_before_def || summary.has_use_before_def;
    }

    if (!has_use_before_def) {
        return true;
    }

    std::vector<bool> in_defined(cfg_workset.blocks.size(), false);
    std::vector<bool> out_defined(cfg_workset.blocks.size(), false);
    for (std::size_t block_index : cfg_workset.reachable_indices) {
        const bool starts_defined = block_index != cfg_workset.entry_index;
        in_defined[block_index] = starts_defined;
        out_defined[block_index] = starts_defined;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t block_index : cfg_workset.reachable_indices) {
            bool next_in = false;
            if (block_index != cfg_workset.entry_index) {
                const auto &predecessors =
                    cfg_workset.predecessor_indices[block_index];
                next_in = !predecessors.empty();
                for (std::size_t predecessor_index : predecessors) {
                    next_in = next_in && is_reachable[predecessor_index] &&
                              out_defined[predecessor_index];
                }
            }

            const bool next_out =
                block_summaries[block_index].has_def ? true : next_in;
            if (in_defined[block_index] != next_in ||
                out_defined[block_index] != next_out) {
                in_defined[block_index] = next_in;
                out_defined[block_index] = next_out;
                changed = true;
            }
        }
    }

    for (std::size_t block_index : cfg_workset.reachable_indices) {
        const BlockSummary &summary = block_summaries[block_index];
        if (summary.has_use_before_def && !in_defined[block_index]) {
            return false;
        }
    }

    std::vector<bool> live_in(cfg_workset.blocks.size(), false);
    for (std::size_t block_index : cfg_workset.reachable_indices) {
        live_in[block_index] = block_summaries[block_index].has_use_before_def;
    }

    changed = true;
    while (changed) {
        changed = false;
        for (std::size_t block_index : cfg_workset.reachable_indices) {
            const BlockSummary &summary = block_summaries[block_index];
            bool next_live_in = summary.has_use_before_def;
            if (!next_live_in && !summary.has_def) {
                for (std::size_t successor_index :
                     cfg_workset.successor_indices[block_index]) {
                    if (is_reachable[successor_index] &&
                        live_in[successor_index]) {
                        next_live_in = true;
                        break;
                    }
                }
            }
            if (live_in[block_index] != next_live_in) {
                live_in[block_index] = next_live_in;
                changed = true;
            }
        }
    }

    std::vector<CoreIrBasicBlock *> worklist(unit_info.def_blocks.begin(),
                                             unit_info.def_blocks.end());
    std::unordered_set<CoreIrBasicBlock *> visited(unit_info.def_blocks.begin(),
                                                   unit_info.def_blocks.end());
    while (!worklist.empty()) {
        CoreIrBasicBlock *block = worklist.back();
        worklist.pop_back();
        if (block == nullptr) {
            continue;
        }
        for (CoreIrBasicBlock *frontier_block :
             dominance_frontier.get_frontier(block)) {
            auto frontier_it = cfg_workset.block_indices.find(frontier_block);
            if (frontier_block == nullptr ||
                frontier_it == cfg_workset.block_indices.end() ||
                !live_in[frontier_it->second]) {
                continue;
            }
            for (std::size_t predecessor_index :
                 cfg_workset.predecessor_indices[frontier_it->second]) {
                if (!out_defined[predecessor_index]) {
                    return false;
                }
            }
            if (visited.insert(frontier_block).second) {
                worklist.push_back(frontier_block);
            }
        }
    }
    return true;
}

} // namespace

CoreIrPromotableStackSlotAnalysisResult::CoreIrPromotableStackSlotAnalysisResult(
    const CoreIrFunction *function, std::vector<CoreIrPromotionUnitInfo> unit_infos,
    std::unordered_map<const CoreIrInstruction *, std::size_t> instruction_to_unit,
    std::vector<CoreIrRejectedStackSlot> rejected_slots) noexcept
    : function_(function), unit_infos_(std::move(unit_infos)),
      instruction_to_unit_(std::move(instruction_to_unit)),
      rejected_slots_(std::move(rejected_slots)) {}

const CoreIrPromotionUnitInfo *
CoreIrPromotableStackSlotAnalysisResult::find_unit_for_instruction(
    const CoreIrInstruction *instruction) const noexcept {
    auto it = instruction_to_unit_.find(instruction);
    if (it == instruction_to_unit_.end() || it->second >= unit_infos_.size()) {
        return nullptr;
    }
    return &unit_infos_[it->second];
}

CoreIrPromotableStackSlotAnalysisResult
CoreIrPromotableStackSlotAnalysis::Run(const CoreIrFunction &function) const {
    std::unordered_map<CoreIrStackSlot *, std::vector<CandidateAccess>> slot_accesses;
    std::unordered_map<CoreIrStackSlot *, CoreIrPromotionFailureReason> rejected_slots;
    std::unordered_map<CoreIrStackSlot *, std::vector<BlockedPrefix>> blocked_prefixes;

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }

            switch (instruction->get_opcode()) {
            case CoreIrOpcode::Load: {
                auto *load = static_cast<CoreIrLoadInst *>(instruction);
                CoreIrStackSlot *stack_slot = nullptr;
                std::vector<std::uint64_t> path;
                const CoreIrType *value_type = load->get_type();
                if (load->get_stack_slot() != nullptr) {
                    stack_slot = load->get_stack_slot();
                } else {
                    bool exact_path = true;
                    if (!trace_stack_slot_prefix(load->get_address(), stack_slot, path,
                                                 exact_path)) {
                        continue;
                    }
                    if (!exact_path) {
                        // A dynamic index from any derived aggregate address can
                        // legally reach sibling elements, so keep the root slot
                        // intact instead of promoting only exact constant paths.
                        add_blocked_prefix(
                            blocked_prefixes, stack_slot, {},
                            CoreIrPromotionFailureReason::DynamicIndex);
                        continue;
                    }
                }
                if (stack_slot == nullptr) {
                    continue;
                }
                slot_accesses[stack_slot].push_back(
                    CandidateAccess{load, block.get(), stack_slot, std::move(path),
                                    value_type, false});
                continue;
            }

            case CoreIrOpcode::Store: {
                auto *store = static_cast<CoreIrStoreInst *>(instruction);
                CoreIrStackSlot *stack_slot = nullptr;
                std::vector<std::uint64_t> path;
                const CoreIrType *value_type =
                    store->get_value() == nullptr ? nullptr : store->get_value()->get_type();
                if (store->get_stack_slot() != nullptr) {
                    stack_slot = store->get_stack_slot();
                } else {
                    bool exact_path = true;
                    if (!trace_stack_slot_prefix(store->get_address(), stack_slot, path,
                                                 exact_path)) {
                        continue;
                    }
                    if (!exact_path) {
                        // A dynamic store through a derived aggregate address may
                        // alias any sibling element reachable from that base.
                        add_blocked_prefix(
                            blocked_prefixes, stack_slot, {},
                            CoreIrPromotionFailureReason::DynamicIndex);
                        continue;
                    }
                }
                if (stack_slot == nullptr) {
                    continue;
                }
                slot_accesses[stack_slot].push_back(
                    CandidateAccess{store, block.get(), stack_slot, std::move(path),
                                    value_type, true});
                continue;
            }

            case CoreIrOpcode::AddressOfStackSlot: {
                auto *address =
                    static_cast<CoreIrAddressOfStackSlotInst *>(instruction);
                for (const CoreIrUse &use : address->get_uses()) {
                    CoreIrInstruction *user = use.get_user();
                    if (user != nullptr &&
                        is_safe_address_user(*user, use.get_operand_index())) {
                        continue;
                    }
                    add_blocked_prefix(
                        blocked_prefixes, address->get_stack_slot(), {},
                        classify_non_load_store_user_or_default(user));
                }
                break;
            }

            case CoreIrOpcode::GetElementPtr: {
                auto *gep = static_cast<CoreIrGetElementPtrInst *>(instruction);
                CoreIrStackSlot *root_slot = nullptr;
                std::vector<std::uint64_t> prefix_path;
                bool exact_path = true;
                if (!trace_stack_slot_prefix(gep, root_slot, prefix_path, exact_path) ||
                    root_slot == nullptr) {
                    continue;
                }
                for (const CoreIrUse &use : gep->get_uses()) {
                    CoreIrInstruction *user = use.get_user();
                    if (user != nullptr &&
                        is_safe_address_user(*user, use.get_operand_index())) {
                        continue;
                    }
                    // Once a derived subobject address escapes to an unknown user such as
                    // a call, later pointer arithmetic can legally or effectively observe
                    // sibling elements through that escaped base. Keep the whole aggregate
                    // intact instead of promoting only the exact leaf path.
                    add_blocked_prefix(
                        blocked_prefixes, root_slot, {},
                        exact_path ? classify_non_load_store_user_or_default(user)
                                   : CoreIrPromotionFailureReason::DynamicIndex);
                }
                break;
            }

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
            case CoreIrOpcode::AddressOfFunction:
            case CoreIrOpcode::AddressOfGlobal:
            case CoreIrOpcode::DynamicAlloca:
            case CoreIrOpcode::Call:
            case CoreIrOpcode::Jump:
            case CoreIrOpcode::CondJump:
            case CoreIrOpcode::IndirectJump:
            case CoreIrOpcode::Return:
                break;
            }
        }
    }

    std::vector<CoreIrPromotionUnitInfo> unit_infos;
    std::unordered_map<const CoreIrInstruction *, std::size_t> instruction_to_unit;
    std::vector<CoreIrRejectedStackSlot> rejected_slot_list;

    for (const auto &slot : function.get_stack_slots()) {
        auto rejected_it = rejected_slots.find(slot.get());
        if (rejected_it != rejected_slots.end()) {
            rejected_slot_list.push_back(
                CoreIrRejectedStackSlot{slot.get(), rejected_it->second});
            continue;
        }

        auto accesses_it = slot_accesses.find(slot.get());
        if (accesses_it == slot_accesses.end()) {
            continue;
        }

        std::vector<PathCandidateGroup> path_groups;
        for (const CandidateAccess &access : accesses_it->second) {
            const CoreIrType *leaf_type =
                resolve_access_path_type(slot->get_allocated_type(), access.access_path);
            if (leaf_type == nullptr || !is_scalar_promotable_type(leaf_type) ||
                !detail::are_equivalent_types(access.value_type, leaf_type)) {
                rejected_slot_list.push_back(
                    CoreIrRejectedStackSlot{slot.get(),
                                            leaf_type == nullptr
                                                ? CoreIrPromotionFailureReason::DynamicIndex
                                                : (!is_scalar_promotable_type(leaf_type)
                                                       ? CoreIrPromotionFailureReason::
                                                             UnsupportedLeafType
                                                       : CoreIrPromotionFailureReason::
                                                             TypeMismatch)});
                path_groups.clear();
                break;
            }
            PathCandidateGroup &group =
                get_or_create_path_group(path_groups, access.access_path,
                                         leaf_type);
            group.accesses.push_back(access);
        }
        if (path_groups.empty()) {
            continue;
        }

        const auto blocked_it = blocked_prefixes.find(slot.get());
        if (blocked_it != blocked_prefixes.end()) {
            for (const BlockedPrefix &blocked_prefix : blocked_it->second) {
                for (PathCandidateGroup &group : path_groups) {
                    const auto &path = group.accesses.front().access_path;
                    if (paths_overlap(path, blocked_prefix.access_path)) {
                        group.invalid = true;
                    }
                }
            }
        }

        for (std::size_t left = 0; left < path_groups.size(); ++left) {
            for (std::size_t right = left + 1; right < path_groups.size(); ++right) {
                const auto &lhs = path_groups[left].accesses.front().access_path;
                const auto &rhs = path_groups[right].accesses.front().access_path;
                if (!paths_overlap(lhs, rhs)) {
                    continue;
                }
                path_groups[left].invalid = true;
                path_groups[right].invalid = true;
            }
        }

        for (PathCandidateGroup &group : path_groups) {
            if (group.invalid) {
                rejected_slot_list.push_back(
                    CoreIrRejectedStackSlot{slot.get(),
                                            CoreIrPromotionFailureReason::
                                                OverlappingAccessPath});
                continue;
            }

            CoreIrPromotionUnitInfo unit_info;
            unit_info.unit.kind = group.accesses.front().access_path.empty()
                                      ? CoreIrPromotionUnitKind::WholeSlot
                                      : CoreIrPromotionUnitKind::AccessPath;
            unit_info.unit.stack_slot = slot.get();
            unit_info.unit.access_path = group.accesses.front().access_path;
            unit_info.unit.value_type = group.value_type;

            for (const CandidateAccess &access : group.accesses) {
                if (access.is_store) {
                    auto *store = static_cast<CoreIrStoreInst *>(access.instruction);
                    unit_info.stores.push_back(store);
                    unit_info.def_blocks.insert(access.block);
                } else {
                    unit_info.loads.push_back(
                        static_cast<CoreIrLoadInst *>(access.instruction));
                }
            }

            const std::size_t unit_index = unit_infos.size();
            for (CoreIrLoadInst *load : unit_info.loads) {
                instruction_to_unit.emplace(load, unit_index);
            }
            for (CoreIrStoreInst *store : unit_info.stores) {
                instruction_to_unit.emplace(store, unit_index);
            }
            unit_infos.push_back(std::move(unit_info));
        }
    }

    if (!unit_infos.empty()) {
        CoreIrCfgAnalysis cfg_analysis_runner;
        const CoreIrCfgAnalysisResult cfg_analysis =
            cfg_analysis_runner.Run(function);
        const PromotableCfgWorkset cfg_workset =
            build_promotable_cfg_workset(function, cfg_analysis);
        CoreIrDominatorTreeAnalysis dominator_tree_runner;
        const CoreIrDominatorTreeAnalysisResult dominator_tree =
            dominator_tree_runner.Run(function, cfg_analysis);
        CoreIrDominanceFrontierAnalysis dominance_frontier_runner;
        const CoreIrDominanceFrontierAnalysisResult dominance_frontier =
            dominance_frontier_runner.Run(function, cfg_analysis, dominator_tree);
        std::unordered_map<const CoreIrInstruction *, std::size_t>
            instruction_order;
        std::size_t next_instruction_order = 0;
        for (const auto &block : function.get_basic_blocks()) {
            if (block == nullptr) {
                continue;
            }
            for (const auto &instruction : block->get_instructions()) {
                if (instruction != nullptr) {
                    instruction_order.emplace(instruction.get(),
                                              next_instruction_order++);
                }
            }
        }

        unit_infos.erase(
            std::remove_if(
                unit_infos.begin(), unit_infos.end(),
                [&dominance_frontier, &cfg_workset, &instruction_order,
                 &rejected_slot_list](const CoreIrPromotionUnitInfo &unit_info) {
                    if (unit_is_definitely_defined_on_all_paths(
                            unit_info, dominance_frontier, cfg_workset,
                            instruction_order)) {
                        return false;
                    }
                    rejected_slot_list.push_back(
                        CoreIrRejectedStackSlot{unit_info.unit.stack_slot,
                                                CoreIrPromotionFailureReason::
                                                    UndefinedOnSomePath});
                    return true;
                }),
            unit_infos.end());
    }

    instruction_to_unit.clear();
    for (std::size_t unit_index = 0; unit_index < unit_infos.size(); ++unit_index) {
        for (CoreIrLoadInst *load : unit_infos[unit_index].loads) {
            instruction_to_unit.emplace(load, unit_index);
        }
        for (CoreIrStoreInst *store : unit_infos[unit_index].stores) {
            instruction_to_unit.emplace(store, unit_index);
        }
    }

    return CoreIrPromotableStackSlotAnalysisResult(
        &function, std::move(unit_infos), std::move(instruction_to_unit),
        std::move(rejected_slot_list));
}

} // namespace sysycc
