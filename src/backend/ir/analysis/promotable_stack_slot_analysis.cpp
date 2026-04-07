#include "backend/ir/analysis/promotable_stack_slot_analysis.hpp"

#include <algorithm>
#include <cstddef>
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

struct BlockedPrefix {
    std::vector<std::uint64_t> access_path;
    CoreIrPromotionFailureReason reason = CoreIrPromotionFailureReason::AddressEscaped;
};

std::string build_path_key(const std::vector<std::uint64_t> &path) {
    std::string key;
    for (std::uint64_t index : path) {
        key += std::to_string(index);
        key.push_back('/');
    }
    return key;
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
    if (auto *address = dynamic_cast<CoreIrAddressOfStackSlotInst *>(value);
        address != nullptr) {
        stack_slot = address->get_stack_slot();
        return stack_slot != nullptr;
    }

    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(value);
    if (gep == nullptr) {
        return false;
    }

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
    if (auto *address = dynamic_cast<CoreIrAddressOfStackSlotInst *>(value);
        address != nullptr) {
        stack_slot = address->get_stack_slot();
        is_exact = stack_slot != nullptr;
        return stack_slot != nullptr;
    }

    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(value);
    if (gep == nullptr) {
        return false;
    }

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
    CoreIrInstruction &instruction) {
    if (dynamic_cast<CoreIrGetElementPtrInst *>(&instruction) != nullptr) {
        return CoreIrPromotionFailureReason::DynamicIndex;
    }
    return CoreIrPromotionFailureReason::NonLoadStoreUser;
}

bool is_safe_address_user(CoreIrInstruction &user, std::size_t operand_index) {
    if (auto *load = dynamic_cast<CoreIrLoadInst *>(&user); load != nullptr) {
        return operand_index == 0 && load->get_address() != nullptr;
    }
    if (auto *store = dynamic_cast<CoreIrStoreInst *>(&user); store != nullptr) {
        return operand_index == 1 && store->get_address() != nullptr;
    }
    if (dynamic_cast<CoreIrGetElementPtrInst *>(&user) != nullptr) {
        return operand_index == 0;
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

bool unit_contains_instruction(const CoreIrPromotionUnitInfo &unit_info,
                               const CoreIrInstruction *instruction) {
    for (CoreIrLoadInst *load : unit_info.loads) {
        if (load == instruction) {
            return true;
        }
    }
    for (CoreIrStoreInst *store : unit_info.stores) {
        if (store == instruction) {
            return true;
        }
    }
    return false;
}

bool unit_is_definitely_defined_on_all_paths(
    const CoreIrPromotionUnitInfo &unit_info, const CoreIrFunction &function) {
    CoreIrCfgAnalysis cfg_analysis_runner;
    const CoreIrCfgAnalysisResult cfg_analysis = cfg_analysis_runner.Run(function);
    const CoreIrBasicBlock *entry_block = cfg_analysis.get_entry_block();
    if (entry_block == nullptr) {
        return false;
    }

    std::unordered_map<const CoreIrBasicBlock *, BlockSummary> block_summaries;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        BlockSummary summary;
        bool locally_defined = false;
        for (const auto &instruction : block->get_instructions()) {
            if (instruction == nullptr) {
                continue;
            }
            const bool in_unit = unit_contains_instruction(unit_info, instruction.get());
            if (!in_unit) {
                continue;
            }
            if (dynamic_cast<CoreIrLoadInst *>(instruction.get()) != nullptr &&
                !locally_defined) {
                summary.has_use_before_def = true;
            }
            if (dynamic_cast<CoreIrStoreInst *>(instruction.get()) != nullptr) {
                locally_defined = true;
                summary.has_def = true;
            }
        }
        block_summaries.emplace(block.get(), summary);
    }

    std::unordered_map<const CoreIrBasicBlock *, bool> in_defined;
    std::unordered_map<const CoreIrBasicBlock *, bool> out_defined;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        in_defined.emplace(block.get(), false);
        out_defined.emplace(block.get(), false);
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &block : function.get_basic_blocks()) {
            if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
                continue;
            }

            bool next_in = false;
            if (block.get() != entry_block) {
                const auto &predecessors = cfg_analysis.get_predecessors(block.get());
                next_in = !predecessors.empty();
                for (CoreIrBasicBlock *predecessor : predecessors) {
                    next_in = next_in && out_defined[predecessor];
                }
            }

            const bool next_out =
                block_summaries[block.get()].has_def ? true : next_in;
            if (in_defined[block.get()] != next_in ||
                out_defined[block.get()] != next_out) {
                in_defined[block.get()] = next_in;
                out_defined[block.get()] = next_out;
                changed = true;
            }
        }
    }

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        const BlockSummary &summary = block_summaries[block.get()];
        if (summary.has_use_before_def && !in_defined[block.get()]) {
            return false;
        }
    }

    CoreIrDominatorTreeAnalysis dominator_tree_runner;
    const CoreIrDominatorTreeAnalysisResult dominator_tree =
        dominator_tree_runner.Run(function, cfg_analysis);
    CoreIrDominanceFrontierAnalysis dominance_frontier_runner;
    const CoreIrDominanceFrontierAnalysisResult dominance_frontier =
        dominance_frontier_runner.Run(function, cfg_analysis, dominator_tree);

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
            if (frontier_block == nullptr) {
                continue;
            }
            for (CoreIrBasicBlock *predecessor :
                 cfg_analysis.get_predecessors(frontier_block)) {
                if (predecessor != nullptr && !out_defined[predecessor]) {
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

            if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction); load != nullptr) {
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
                        add_blocked_prefix(
                            blocked_prefixes, stack_slot, std::move(path),
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

            if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction); store != nullptr) {
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
                        add_blocked_prefix(
                            blocked_prefixes, stack_slot, std::move(path),
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

            if (auto *address = dynamic_cast<CoreIrAddressOfStackSlotInst *>(instruction);
                address != nullptr) {
                for (const CoreIrUse &use : address->get_uses()) {
                    CoreIrInstruction *user = use.get_user();
                    if (user != nullptr &&
                        is_safe_address_user(*user, use.get_operand_index())) {
                        continue;
                    }
                    add_blocked_prefix(
                        blocked_prefixes, address->get_stack_slot(), {},
                        classify_non_load_store_user(*user));
                }
            }

            if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(instruction);
                gep != nullptr) {
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
                    add_blocked_prefix(
                        blocked_prefixes, root_slot, prefix_path,
                        exact_path ? classify_non_load_store_user(*user)
                                   : CoreIrPromotionFailureReason::DynamicIndex);
                }
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

        std::unordered_map<std::string, PathCandidateGroup> path_groups;
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
                path_groups[build_path_key(access.access_path)];
            group.value_type = leaf_type;
            group.accesses.push_back(access);
        }
        if (path_groups.empty()) {
            continue;
        }

        const auto blocked_it = blocked_prefixes.find(slot.get());
        if (blocked_it != blocked_prefixes.end()) {
            for (const BlockedPrefix &blocked_prefix : blocked_it->second) {
                for (auto &entry : path_groups) {
                    const auto &path = entry.second.accesses.front().access_path;
                    if (paths_overlap(path, blocked_prefix.access_path)) {
                        entry.second.invalid = true;
                    }
                }
            }
        }

        std::vector<std::string> path_keys;
        path_keys.reserve(path_groups.size());
        for (const auto &entry : path_groups) {
            path_keys.push_back(entry.first);
        }

        for (std::size_t left = 0; left < path_keys.size(); ++left) {
            for (std::size_t right = left + 1; right < path_keys.size(); ++right) {
                const auto &lhs = path_groups[path_keys[left]].accesses.front().access_path;
                const auto &rhs = path_groups[path_keys[right]].accesses.front().access_path;
                if (!paths_overlap(lhs, rhs)) {
                    continue;
                }
                path_groups[path_keys[left]].invalid = true;
                path_groups[path_keys[right]].invalid = true;
            }
        }

        for (auto &entry : path_groups) {
            if (entry.second.invalid) {
                rejected_slot_list.push_back(
                    CoreIrRejectedStackSlot{slot.get(),
                                            CoreIrPromotionFailureReason::
                                                OverlappingAccessPath});
                continue;
            }

            CoreIrPromotionUnitInfo unit_info;
            unit_info.unit.kind = entry.second.accesses.front().access_path.empty()
                                      ? CoreIrPromotionUnitKind::WholeSlot
                                      : CoreIrPromotionUnitKind::AccessPath;
            unit_info.unit.stack_slot = slot.get();
            unit_info.unit.access_path = entry.second.accesses.front().access_path;
            unit_info.unit.value_type = entry.second.value_type;

            for (const CandidateAccess &access : entry.second.accesses) {
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

    unit_infos.erase(
        std::remove_if(
            unit_infos.begin(), unit_infos.end(),
            [&function, &rejected_slot_list](const CoreIrPromotionUnitInfo &unit_info) {
                if (unit_is_definitely_defined_on_all_paths(unit_info, function)) {
                    return false;
                }
                rejected_slot_list.push_back(
                    CoreIrRejectedStackSlot{unit_info.unit.stack_slot,
                                            CoreIrPromotionFailureReason::
                                                UndefinedOnSomePath});
                return true;
            }),
        unit_infos.end());

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
