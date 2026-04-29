#include "backend/ir/loop_memory_promotion/core_ir_loop_memory_promotion_pass.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/dominance_frontier_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/analysis/promotable_stack_slot_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::erase_instruction;
using sysycc::detail::insert_instruction_before;
using sysycc::detail::normalize_constant_stack_slot_path;
using sysycc::detail::paths_overlap;
using sysycc::detail::trace_stack_slot_prefix;

PassResult fail_missing_core_ir(CompilerContext &context,
                                const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

struct UnitLoopAccessInfo {
    const CoreIrPromotionUnitInfo *unit_info = nullptr;
    CoreIrStackSlot *slot = nullptr;
    CoreIrPromotionUnitKind kind = CoreIrPromotionUnitKind::WholeSlot;
    std::vector<std::uint64_t> access_path;
    const CoreIrType *value_type = nullptr;
    std::vector<CoreIrLoadInst *> loads;
    std::vector<CoreIrStoreInst *> stores;
    std::unordered_set<CoreIrBasicBlock *> def_blocks;
    std::unordered_set<CoreIrBasicBlock *> use_blocks;
};

struct LoopPromotionContext {
    UnitLoopAccessInfo *access_info = nullptr;
    std::unordered_map<CoreIrBasicBlock *, CoreIrPhiInst *> inserted_phis;
    const std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        *dominator_children = nullptr;
    std::vector<CoreIrValue *> value_stack;
    std::unordered_map<
        CoreIrBasicBlock *,
        std::vector<std::pair<CoreIrBasicBlock *, CoreIrValue *>>>
        exit_incomings;
    bool changed = false;
};

template <typename T>
bool append_unique_instruction(std::vector<T *> &instructions, T *instruction) {
    if (instruction == nullptr ||
        std::find(instructions.begin(), instructions.end(), instruction) !=
            instructions.end()) {
        return false;
    }
    instructions.push_back(instruction);
    return true;
}

bool loop_contains_block(const CoreIrLoopInfo &loop,
                         const CoreIrBasicBlock *block) {
    return block != nullptr &&
           loop.get_blocks().find(const_cast<CoreIrBasicBlock *>(block)) !=
               loop.get_blocks().end();
}

std::size_t count_loop_instructions(const CoreIrLoopInfo &loop) {
    std::size_t count = 0;
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block != nullptr) {
            count += block->get_instructions().size();
        }
    }
    return count;
}

bool erase_dead_address_chain(CoreIrBasicBlock &block, CoreIrValue *value) {
    bool changed = false;
    CoreIrInstruction *instruction = dynamic_cast<CoreIrInstruction *>(value);
    while (instruction != nullptr &&
           get_core_ir_instruction_effect(*instruction).is_pure_value &&
           instruction->get_uses().empty()) {
        CoreIrValue *next = nullptr;
        if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(instruction);
            gep != nullptr) {
            next = gep->get_base();
        }
        if (!erase_instruction(block, instruction)) {
            break;
        }
        changed = true;
        instruction = dynamic_cast<CoreIrInstruction *>(next);
    }
    return changed;
}

bool loop_unit_matches(const UnitLoopAccessInfo &info,
                       CoreIrStackSlot *stack_slot,
                       CoreIrPromotionUnitKind kind,
                       const std::vector<std::uint64_t> &access_path,
                       const CoreIrType *value_type) {
    return info.slot == stack_slot && info.kind == kind &&
           info.access_path == access_path &&
           detail::are_equivalent_types(info.value_type, value_type);
}

UnitLoopAccessInfo &get_or_create_loop_unit_info(
    std::vector<UnitLoopAccessInfo> &infos, CoreIrStackSlot *stack_slot,
    CoreIrPromotionUnitKind kind, const std::vector<std::uint64_t> &access_path,
    const CoreIrType *value_type) {
    for (UnitLoopAccessInfo &info : infos) {
        if (loop_unit_matches(info, stack_slot, kind, access_path, value_type)) {
            return info;
        }
    }
    UnitLoopAccessInfo info;
    info.slot = stack_slot;
    info.kind = kind;
    info.access_path = access_path;
    info.value_type = value_type;
    infos.push_back(std::move(info));
    return infos.back();
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
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::IndirectJump:
    case CoreIrOpcode::Return:
        return false;
    }
    return false;
}

void block_loop_promotions_for_unsafe_stack_pointer_use(
    CoreIrInstruction &instruction,
    std::unordered_map<CoreIrStackSlot *,
                       std::vector<std::vector<std::uint64_t>>>
        &blocked_prefixes) {
    const auto &operands = instruction.get_operands();
    for (std::size_t operand_index = 0; operand_index < operands.size();
         ++operand_index) {
        if (is_safe_address_user(instruction, operand_index)) {
            continue;
        }

        CoreIrStackSlot *root_slot = nullptr;
        std::vector<std::uint64_t> prefix_path;
        bool exact_path = true;
        if (!trace_stack_slot_prefix(operands[operand_index], root_slot,
                                     prefix_path, exact_path) ||
            root_slot == nullptr) {
            continue;
        }

        // A call or any other non-load/store address consumer can write through
        // a stack-derived pointer even when the address was materialized before
        // the loop. Keep overlapping loop memory in memory so out-parameters
        // such as luaL_checklstring(..., &len) remain observable.
        if (!exact_path) {
            prefix_path.clear();
        }
        blocked_prefixes[root_slot].push_back(std::move(prefix_path));
    }
}

bool access_info_matches_path(const UnitLoopAccessInfo &access_info,
                              CoreIrStackSlot *stack_slot,
                              const std::vector<std::uint64_t> &path,
                              const CoreIrType *value_type) {
    if (stack_slot == nullptr || access_info.slot != stack_slot ||
        !detail::are_equivalent_types(access_info.value_type, value_type)) {
        return false;
    }
    if (access_info.kind == CoreIrPromotionUnitKind::WholeSlot) {
        return path.empty();
    }
    return access_info.access_path == path;
}

bool instruction_matches_access_info(const CoreIrInstruction &instruction,
                                     const UnitLoopAccessInfo &access_info) {
    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        if (load->get_stack_slot() != nullptr) {
            const std::vector<std::uint64_t> empty_path;
            return access_info_matches_path(access_info, load->get_stack_slot(),
                                            empty_path, load->get_type());
        }
        CoreIrStackSlot *stack_slot = nullptr;
        std::vector<std::uint64_t> path;
        if (!normalize_constant_stack_slot_path(load->get_address(), stack_slot,
                                                path)) {
            return false;
        }
        return access_info_matches_path(access_info, stack_slot, path,
                                        load->get_type());
    }

    const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
    if (store == nullptr) {
        return false;
    }
    if (store->get_stack_slot() != nullptr) {
        const std::vector<std::uint64_t> empty_path;
        return access_info_matches_path(
            access_info, store->get_stack_slot(), empty_path,
            store->get_value() == nullptr ? nullptr
                                          : store->get_value()->get_type());
    }
    CoreIrStackSlot *stack_slot = nullptr;
    std::vector<std::uint64_t> path;
    if (!normalize_constant_stack_slot_path(store->get_address(), stack_slot,
                                            path)) {
        return false;
    }
    return access_info_matches_path(access_info, stack_slot, path,
                                    store->get_value() == nullptr
                                        ? nullptr
                                        : store->get_value()->get_type());
}

bool instruction_belongs_to_promotable_unit(
    const CoreIrPromotableStackSlotAnalysisResult &promotable_units,
    const CoreIrInstruction *instruction, const UnitLoopAccessInfo &access_info) {
    return access_info.unit_info != nullptr &&
           promotable_units.find_unit_for_instruction(instruction) ==
               access_info.unit_info;
}

const CoreIrType *
resolve_access_path_type(const CoreIrType *root_type,
                         const std::vector<std::uint64_t> &path);
const CoreIrConstant *
extract_constant_for_access_path(const CoreIrConstant *constant,
                                 const std::vector<std::uint64_t> &path);

bool get_store_slot_and_path(const CoreIrStoreInst &store,
                             CoreIrStackSlot *&stack_slot,
                             std::vector<std::uint64_t> &path,
                             bool &exact_path) {
    stack_slot = nullptr;
    path.clear();
    exact_path = true;
    if (store.get_stack_slot() != nullptr) {
        stack_slot = store.get_stack_slot();
        return true;
    }
    if (store.get_address() == nullptr) {
        return false;
    }
    return trace_stack_slot_prefix(store.get_address(), stack_slot, path,
                                   exact_path) &&
           stack_slot != nullptr;
}

std::vector<UnitLoopAccessInfo> collect_loop_unit_accesses(
    const CoreIrLoopInfo &loop,
    const CoreIrPromotableStackSlotAnalysisResult &promotable_units) {
    std::vector<UnitLoopAccessInfo> infos;
    infos.reserve(loop.get_blocks().size());

    std::unordered_map<CoreIrStackSlot *,
                       std::vector<std::vector<std::uint64_t>>>
        blocked_prefixes;
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }

            block_loop_promotions_for_unsafe_stack_pointer_use(
                *instruction, blocked_prefixes);

            if (auto *address =
                    dynamic_cast<CoreIrAddressOfStackSlotInst *>(instruction);
                address != nullptr) {
                for (const CoreIrUse &use : address->get_uses()) {
                    CoreIrInstruction *user = use.get_user();
                    if (user != nullptr && user->get_parent() != nullptr &&
                        loop_contains_block(loop, user->get_parent()) &&
                        !is_safe_address_user(*user, use.get_operand_index())) {
                        blocked_prefixes[address->get_stack_slot()].push_back(
                            {});
                    }
                }
            }

            if (auto *gep =
                    dynamic_cast<CoreIrGetElementPtrInst *>(instruction);
                gep != nullptr) {
                CoreIrStackSlot *root_slot = nullptr;
                std::vector<std::uint64_t> prefix_path;
                bool exact_path = true;
                if (!trace_stack_slot_prefix(gep, root_slot, prefix_path,
                                             exact_path) ||
                    root_slot == nullptr) {
                    continue;
                }
                for (const CoreIrUse &use : gep->get_uses()) {
                    CoreIrInstruction *user = use.get_user();
                    if (user != nullptr && user->get_parent() != nullptr &&
                        loop_contains_block(loop, user->get_parent()) &&
                        !is_safe_address_user(*user, use.get_operand_index())) {
                        blocked_prefixes[root_slot].push_back(prefix_path);
                    }
                }
            }

            CoreIrStackSlot *stack_slot = nullptr;
            std::vector<std::uint64_t> path;
            const CoreIrType *value_type = nullptr;
            bool is_store = false;

            if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
                load != nullptr) {
                value_type = load->get_type();
                if (load->get_stack_slot() != nullptr) {
                    stack_slot = load->get_stack_slot();
                } else {
                    bool exact_path = true;
                    if (!trace_stack_slot_prefix(load->get_address(),
                                                 stack_slot, path,
                                                 exact_path) ||
                        stack_slot == nullptr) {
                        continue;
                    }
                    if (!exact_path) {
                        blocked_prefixes[stack_slot].push_back(path);
                        continue;
                    }
                }
            } else if (auto *store =
                           dynamic_cast<CoreIrStoreInst *>(instruction);
                       store != nullptr) {
                is_store = true;
                value_type = store->get_value() == nullptr
                                 ? nullptr
                                 : store->get_value()->get_type();
                if (store->get_stack_slot() != nullptr) {
                    stack_slot = store->get_stack_slot();
                } else {
                    bool exact_path = true;
                    if (!trace_stack_slot_prefix(store->get_address(),
                                                 stack_slot, path,
                                                 exact_path) ||
                        stack_slot == nullptr) {
                        continue;
                    }
                    if (!exact_path) {
                        blocked_prefixes[stack_slot].push_back(path);
                        continue;
                    }
                }
            } else {
                continue;
            }

            const CoreIrType *leaf_type = resolve_access_path_type(
                stack_slot->get_allocated_type(), path);
            if (leaf_type == nullptr ||
                !detail::are_equivalent_types(value_type, leaf_type)) {
                continue;
            }

            const CoreIrPromotionUnitKind kind =
                path.empty() ? CoreIrPromotionUnitKind::WholeSlot
                             : CoreIrPromotionUnitKind::AccessPath;
            UnitLoopAccessInfo &info = get_or_create_loop_unit_info(
                infos, stack_slot, kind, path, leaf_type);
            if (info.unit_info == nullptr) {
                info.unit_info =
                    promotable_units.find_unit_for_instruction(instruction);
            }
            if (is_store) {
                append_unique_instruction(
                    info.stores, static_cast<CoreIrStoreInst *>(instruction));
                info.def_blocks.insert(block);
            } else {
                append_unique_instruction(
                    info.loads, static_cast<CoreIrLoadInst *>(instruction));
            }
            info.use_blocks.insert(block);
        }
    }

    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            auto *store =
                dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
            const auto *constant =
                store == nullptr
                    ? nullptr
                    : dynamic_cast<const CoreIrConstant *>(store->get_value());
            if (store == nullptr || constant == nullptr) {
                continue;
            }

            CoreIrStackSlot *stack_slot = nullptr;
            std::vector<std::uint64_t> path;
            bool exact_path = true;
            if (!get_store_slot_and_path(*store, stack_slot, path,
                                         exact_path) ||
                !exact_path || stack_slot == nullptr || !path.empty()) {
                continue;
            }

            for (auto &info : infos) {
                if (info.kind != CoreIrPromotionUnitKind::AccessPath ||
                    info.slot != stack_slot ||
                    extract_constant_for_access_path(
                        constant, info.access_path) == nullptr) {
                    continue;
                }
                info.def_blocks.insert(block);
                info.use_blocks.insert(block);
            }
        }
    }

    for (auto it = infos.begin(); it != infos.end();) {
        bool invalid = false;
        const auto blocked_it = blocked_prefixes.find(it->slot);
        if (blocked_it != blocked_prefixes.end()) {
            for (const auto &blocked_path : blocked_it->second) {
                if (paths_overlap(it->access_path, blocked_path)) {
                    invalid = true;
                    break;
                }
            }
        }
        if (invalid) {
            it = infos.erase(it);
        } else {
            ++it;
        }
    }
    return infos;
}

const CoreIrType *
resolve_access_path_type(const CoreIrType *root_type,
                         const std::vector<std::uint64_t> &path) {
    const CoreIrType *current = root_type;
    for (std::uint64_t index : path) {
        if (const auto *array_type =
                dynamic_cast<const CoreIrArrayType *>(current);
            array_type != nullptr) {
            current = array_type->get_element_type();
            continue;
        }
        if (const auto *struct_type =
                dynamic_cast<const CoreIrStructType *>(current);
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

const CoreIrConstant *
extract_constant_for_access_path(const CoreIrConstant *constant,
                                 const std::vector<std::uint64_t> &path) {
    const CoreIrConstant *current = constant;
    const CoreIrType *current_type =
        constant == nullptr ? nullptr : constant->get_type();
    for (std::size_t path_index = 0; path_index < path.size(); ++path_index) {
        const std::uint64_t index = path[path_index];
        if (dynamic_cast<const CoreIrConstantZeroInitializer *>(current) !=
            nullptr) {
            CoreIrContext *parent_context = current->get_parent_context();
            const std::vector<std::uint64_t> remaining_path(
                path.begin() + static_cast<std::ptrdiff_t>(path_index),
                path.end());
            const CoreIrType *leaf_type =
                resolve_access_path_type(current_type, remaining_path);
            if (parent_context == nullptr || leaf_type == nullptr) {
                return current;
            }
            return parent_context
                ->create_constant<CoreIrConstantZeroInitializer>(leaf_type);
        }
        const auto *aggregate =
            dynamic_cast<const CoreIrConstantAggregate *>(current);
        if (aggregate == nullptr || index >= aggregate->get_elements().size()) {
            return nullptr;
        }
        if (const auto *array_type =
                dynamic_cast<const CoreIrArrayType *>(current_type);
            array_type != nullptr) {
            current_type = array_type->get_element_type();
        } else if (const auto *struct_type =
                       dynamic_cast<const CoreIrStructType *>(current_type);
                   struct_type != nullptr) {
            if (index >= struct_type->get_element_types().size()) {
                return nullptr;
            }
            current_type = struct_type->get_element_types()[index];
        } else {
            return nullptr;
        }
        current = aggregate->get_elements()[index];
    }
    return current;
}

CoreIrValue *extract_store_leaf_value(const CoreIrStoreInst &store,
                                      const UnitLoopAccessInfo &access_info) {
    if (instruction_matches_access_info(store, access_info)) {
        return store.get_value();
    }
    if (access_info.kind != CoreIrPromotionUnitKind::AccessPath) {
        return nullptr;
    }

    CoreIrStackSlot *stack_slot = nullptr;
    std::vector<std::uint64_t> path;
    bool exact_path = true;
    if (!get_store_slot_and_path(store, stack_slot, path, exact_path) ||
        !exact_path || stack_slot != access_info.slot || !path.empty()) {
        return nullptr;
    }

    const auto *constant =
        dynamic_cast<const CoreIrConstant *>(store.get_value());
    const CoreIrConstant *leaf =
        extract_constant_for_access_path(constant, access_info.access_path);
    return const_cast<CoreIrConstant *>(leaf);
}

bool store_clobbers_access_info(const CoreIrStoreInst &store,
                                const UnitLoopAccessInfo &access_info) {
    if (instruction_matches_access_info(store, access_info)) {
        return true;
    }
    if (access_info.kind != CoreIrPromotionUnitKind::AccessPath) {
        return false;
    }
    CoreIrStackSlot *stack_slot = nullptr;
    std::vector<std::uint64_t> path;
    bool exact_path = true;
    return get_store_slot_and_path(store, stack_slot, path, exact_path) &&
           exact_path && stack_slot == access_info.slot && path.empty();
}

CoreIrValue *find_initial_value_in_preheader(
    CoreIrBasicBlock &preheader, const UnitLoopAccessInfo &access_info,
    const CoreIrPromotableStackSlotAnalysisResult &promotable_units) {
    auto &instructions = preheader.get_instructions();
    for (auto it = instructions.rbegin(); it != instructions.rend(); ++it) {
        auto *store = dynamic_cast<CoreIrStoreInst *>(it->get());
        if (store != nullptr) {
            if (access_info.kind == CoreIrPromotionUnitKind::WholeSlot &&
                store->get_stack_slot() == access_info.slot) {
                return store->get_value();
            }
            if (instruction_matches_access_info(*store, access_info)) {
                return store->get_value();
            }
            if (access_info.kind == CoreIrPromotionUnitKind::AccessPath) {
                if (CoreIrValue *leaf =
                        extract_store_leaf_value(*store, access_info);
                    leaf != nullptr) {
                    return leaf;
                }
            }
        }
        auto *load = dynamic_cast<CoreIrLoadInst *>(it->get());
        if (load != nullptr) {
            if (access_info.kind == CoreIrPromotionUnitKind::WholeSlot &&
                load->get_stack_slot() == access_info.slot) {
                return load;
            }
            if (instruction_matches_access_info(*load, access_info)) {
                return load;
            }
            if (access_info.kind == CoreIrPromotionUnitKind::AccessPath &&
                instruction_belongs_to_promotable_unit(promotable_units, load,
                                                       access_info)) {
                return load;
            }
        }
    }
    return nullptr;
}

CoreIrValue *materialize_access_path_address(
    CoreIrBasicBlock &block, CoreIrContext &core_ir_context,
    const UnitLoopAccessInfo &access_info, CoreIrInstruction *anchor);

CoreIrValue *
materialize_initial_value_load(CoreIrBasicBlock &preheader,
                               CoreIrContext &core_ir_context,
                               const UnitLoopAccessInfo &access_info) {
    if (access_info.slot == nullptr || access_info.value_type == nullptr) {
        return nullptr;
    }

    CoreIrInstruction *anchor = preheader.get_instructions().empty()
                                    ? nullptr
                                    : preheader.get_instructions().back().get();
    if (access_info.kind == CoreIrPromotionUnitKind::WholeSlot) {
        auto load = std::make_unique<CoreIrLoadInst>(
            access_info.value_type, access_info.slot->get_name() + ".loop.seed",
            access_info.slot);
        return insert_instruction_before(preheader, anchor, std::move(load));
    }

    CoreIrValue *address = materialize_access_path_address(
        preheader, core_ir_context, access_info, anchor);
    if (address == nullptr) {
        return nullptr;
    }
    auto load = std::make_unique<CoreIrLoadInst>(
        access_info.value_type, access_info.slot->get_name() + ".loop.seed",
        address);
    return insert_instruction_before(preheader, anchor, std::move(load));
}

void build_dominator_children(
    CoreIrFunction &function, CoreIrAnalysisManager &analysis_manager,
    std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        &children) {
    const CoreIrCfgAnalysisResult &cfg_analysis =
        analysis_manager.get_or_compute<CoreIrCfgAnalysis>(function);
    const CoreIrDominatorTreeAnalysisResult &dominator_tree =
        analysis_manager.get_or_compute<CoreIrDominatorTreeAnalysis>(function);
    for (const auto &block : function.get_basic_blocks()) {
        if (block != nullptr && cfg_analysis.is_reachable(block.get())) {
            children.emplace(block.get(), std::vector<CoreIrBasicBlock *>{});
        }
    }
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        CoreIrBasicBlock *idom =
            dominator_tree.get_immediate_dominator(block.get());
        if (idom != nullptr) {
            children[idom].push_back(block.get());
        }
    }
}

CoreIrPhiInst *insert_phi_for_slot(CoreIrBasicBlock &block,
                                   const UnitLoopAccessInfo &access_info,
                                   const CoreIrType *value_type,
                                   std::size_t index) {
    auto phi = std::make_unique<CoreIrPhiInst>(
        value_type,
        access_info.slot->get_name() + ".loop." + std::to_string(index));
    return static_cast<CoreIrPhiInst *>(
        block.insert_instruction_before_first_non_phi(std::move(phi)));
}

void insert_loop_memory_phis(CoreIrFunction &function,
                             CoreIrAnalysisManager &analysis_manager,
                             const CoreIrLoopInfo &loop,
                             LoopPromotionContext &promotion_context) {
    const UnitLoopAccessInfo &access_info = *promotion_context.access_info;
    const CoreIrDominanceFrontierAnalysisResult &dominance_frontier =
        analysis_manager.get_or_compute<CoreIrDominanceFrontierAnalysis>(
            function);

    std::vector<CoreIrBasicBlock *> worklist(access_info.def_blocks.begin(),
                                             access_info.def_blocks.end());
    std::unordered_set<CoreIrBasicBlock *> queued(
        access_info.def_blocks.begin(), access_info.def_blocks.end());
    std::size_t phi_index = 0;

    CoreIrBasicBlock *header = loop.get_header();
    if (header != nullptr) {
        promotion_context.inserted_phis.emplace(
            header, insert_phi_for_slot(*header, access_info,
                                        access_info.value_type, phi_index++));
    }

    while (!worklist.empty()) {
        CoreIrBasicBlock *block = worklist.back();
        worklist.pop_back();
        if (block == nullptr) {
            continue;
        }
        for (CoreIrBasicBlock *frontier_block :
             dominance_frontier.get_frontier(block)) {
            if (frontier_block == nullptr ||
                !loop_contains_block(loop, frontier_block) ||
                promotion_context.inserted_phis.find(frontier_block) !=
                    promotion_context.inserted_phis.end()) {
                continue;
            }
            CoreIrPhiInst *phi =
                insert_phi_for_slot(*frontier_block, access_info,
                                    access_info.value_type, phi_index++);
            promotion_context.inserted_phis.emplace(frontier_block, phi);
            if (queued.insert(frontier_block).second) {
                worklist.push_back(frontier_block);
            }
        }
    }
}

void add_phi_and_exit_incoming_for_successors(
    CoreIrBasicBlock &block, const CoreIrLoopInfo &loop,
    LoopPromotionContext &promotion_context) {
    if (block.get_instructions().empty() ||
        promotion_context.value_stack.empty()) {
        return;
    }

    auto current_value = promotion_context.value_stack.back();
    auto add_phi_incoming = [&promotion_context, &block,
                             current_value](CoreIrBasicBlock *successor) {
        if (successor == nullptr) {
            return;
        }
        auto phi_it = promotion_context.inserted_phis.find(successor);
        if (phi_it == promotion_context.inserted_phis.end() ||
            phi_it->second == nullptr) {
            return;
        }
        phi_it->second->add_incoming(&block, current_value);
    };

    auto add_exit_incoming = [&promotion_context, &block,
                              current_value](CoreIrBasicBlock *successor) {
        if (successor == nullptr) {
            return;
        }
        promotion_context.exit_incomings[successor].push_back(
            std::make_pair(&block, current_value));
    };

    CoreIrInstruction *terminator = block.get_instructions().back().get();
    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator);
        jump != nullptr) {
        CoreIrBasicBlock *successor = jump->get_target_block();
        if (loop_contains_block(loop, successor)) {
            add_phi_incoming(successor);
        } else {
            add_exit_incoming(successor);
        }
    } else if (auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
               cond_jump != nullptr) {
        for (CoreIrBasicBlock *successor :
             {cond_jump->get_true_block(), cond_jump->get_false_block()}) {
            if (loop_contains_block(loop, successor)) {
                add_phi_incoming(successor);
            } else {
                add_exit_incoming(successor);
            }
        }
    }
}

bool rename_promoted_slot(CoreIrBasicBlock &block, const CoreIrLoopInfo &loop,
                          LoopPromotionContext &promotion_context) {
    const UnitLoopAccessInfo &access_info = *promotion_context.access_info;
    std::size_t saved_depth = promotion_context.value_stack.size();

    auto phi_it = promotion_context.inserted_phis.find(&block);
    if (phi_it != promotion_context.inserted_phis.end() &&
        phi_it->second != nullptr) {
        promotion_context.value_stack.push_back(phi_it->second);
    }

    auto &instructions = block.get_instructions();
    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(index));
            promotion_context.changed = true;
            continue;
        }
        if (dynamic_cast<CoreIrPhiInst *>(instruction) != nullptr) {
            ++index;
            continue;
        }

        auto in_unit = [&access_info](const CoreIrInstruction *candidate) {
            if (candidate == nullptr) {
                return false;
            }
            for (CoreIrLoadInst *load : access_info.loads) {
                if (load == candidate) {
                    return true;
                }
            }
            for (CoreIrStoreInst *store : access_info.stores) {
                if (store == candidate) {
                    return true;
                }
            }
            return false;
        };

        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
            load != nullptr && in_unit(load)) {
            if (!promotion_context.value_stack.empty()) {
                load->replace_all_uses_with(
                    promotion_context.value_stack.back());
            }
            CoreIrValue *address = load->get_address();
            erase_instruction(block, load);
            if (address != nullptr) {
                erase_dead_address_chain(block, address);
            }
            promotion_context.changed = true;
            continue;
        }

        if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
            store != nullptr && (in_unit(store) || store_clobbers_access_info(
                                                       *store, access_info))) {
            CoreIrValue *stored_value =
                extract_store_leaf_value(*store, access_info);
            if (stored_value == nullptr && !in_unit(store)) {
                ++index;
                continue;
            }
            promotion_context.value_stack.push_back(
                stored_value == nullptr ? store->get_value() : stored_value);
            if (!in_unit(store)) {
                ++index;
                continue;
            }
            CoreIrValue *address = store->get_address();
            erase_instruction(block, store);
            if (address != nullptr) {
                erase_dead_address_chain(block, address);
            }
            promotion_context.changed = true;
            continue;
        }

        ++index;
    }

    add_phi_and_exit_incoming_for_successors(block, loop, promotion_context);
    if (promotion_context.dominator_children != nullptr) {
        auto children_it = promotion_context.dominator_children->find(&block);
        if (children_it != promotion_context.dominator_children->end()) {
            for (CoreIrBasicBlock *child : children_it->second) {
                if (loop_contains_block(loop, child)) {
                    rename_promoted_slot(*child, loop, promotion_context);
                }
            }
        }
    }

    while (promotion_context.value_stack.size() > saved_depth) {
        promotion_context.value_stack.pop_back();
    }
    return promotion_context.changed;
}

CoreIrValue *materialize_access_path_address(
    CoreIrBasicBlock &block, CoreIrContext &core_ir_context,
    const UnitLoopAccessInfo &access_info, CoreIrInstruction *anchor) {
    if (access_info.slot == nullptr) {
        return nullptr;
    }
    if (access_info.kind == CoreIrPromotionUnitKind::WholeSlot) {
        return nullptr;
    }

    const CoreIrType *current_type = access_info.slot->get_allocated_type();
    if (current_type == nullptr) {
        return nullptr;
    }
    const CoreIrType *base_ptr_type =
        core_ir_context.create_type<CoreIrPointerType>(current_type);
    auto base = std::make_unique<CoreIrAddressOfStackSlotInst>(
        base_ptr_type, access_info.slot->get_name() + ".addr",
        access_info.slot);
    CoreIrInstruction *base_ptr =
        insert_instruction_before(block, anchor, std::move(base));
    CoreIrValue *current_value = base_ptr;

    auto *i32_type = core_ir_context.create_type<CoreIrIntegerType>(32);
    std::vector<CoreIrValue *> indices;
    indices.push_back(
        core_ir_context.create_constant<CoreIrConstantInt>(i32_type, 0));
    for (std::size_t index = 0; index < access_info.access_path.size();
         ++index) {
        indices.push_back(core_ir_context.create_constant<CoreIrConstantInt>(
            i32_type, access_info.access_path[index]));
    }
    const CoreIrType *pointee_type = resolve_access_path_type(
        access_info.slot->get_allocated_type(), access_info.access_path);
    if (pointee_type == nullptr) {
        return nullptr;
    }
    const CoreIrType *ptr_type =
        core_ir_context.create_type<CoreIrPointerType>(pointee_type);
    auto gep = std::make_unique<CoreIrGetElementPtrInst>(
        ptr_type, access_info.slot->get_name() + ".exit.addr", current_value,
        std::move(indices));
    CoreIrInstruction *gep_ptr =
        insert_instruction_before(block, anchor, std::move(gep));
    return gep_ptr;
}

void materialize_exit_store(CoreIrBasicBlock &exit_block,
                            CoreIrContext &core_ir_context,
                            const UnitLoopAccessInfo &access_info,
                            CoreIrValue *value, const CoreIrType *void_type,
                            CoreIrInstruction *anchor) {
    if (access_info.kind == CoreIrPromotionUnitKind::WholeSlot) {
        auto store = std::make_unique<CoreIrStoreInst>(void_type, value,
                                                       access_info.slot);
        insert_instruction_before(exit_block, anchor, std::move(store));
        return;
    }

    CoreIrValue *address = materialize_access_path_address(
        exit_block, core_ir_context, access_info, anchor);
    auto store = std::make_unique<CoreIrStoreInst>(void_type, value, address);
    insert_instruction_before(exit_block, anchor, std::move(store));
}

CoreIrInstruction *
find_first_exit_block_access_clobber(CoreIrBasicBlock &exit_block,
                                     const UnitLoopAccessInfo &access_info) {
    for (const auto &instruction_ptr : exit_block.get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr ||
            dynamic_cast<CoreIrPhiInst *>(instruction) != nullptr) {
            continue;
        }
        auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
        if (store != nullptr &&
            store_clobbers_access_info(*store, access_info)) {
            return instruction;
        }
        if (instruction->get_is_terminator()) {
            return instruction;
        }
    }
    return exit_block.get_instructions().empty()
               ? nullptr
               : exit_block.get_instructions().back().get();
}

bool rewrite_exit_block_local_loads(CoreIrBasicBlock &exit_block,
                                    const UnitLoopAccessInfo &access_info,
                                    CoreIrValue *replacement) {
    bool changed = false;
    CoreIrValue *current_replacement = replacement;
    auto &instructions = exit_block.get_instructions();
    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr || instruction->get_is_terminator()) {
            ++index;
            continue;
        }

        auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
        if (store != nullptr &&
            store_clobbers_access_info(*store, access_info)) {
            CoreIrValue *leaf_value =
                extract_store_leaf_value(*store, access_info);
            if (leaf_value == nullptr) {
                break;
            }
            current_replacement = leaf_value;
            ++index;
            continue;
        }

        auto *load = dynamic_cast<CoreIrLoadInst *>(instruction);
        if (load == nullptr ||
            !instruction_matches_access_info(*load, access_info)) {
            ++index;
            continue;
        }

        load->replace_all_uses_with(current_replacement);
        CoreIrValue *address = load->get_address();
        erase_instruction(exit_block, load);
        if (address != nullptr) {
            erase_dead_address_chain(exit_block, address);
        }
        changed = true;
    }
    return changed;
}

void materialize_exit_values(const CoreIrLoopInfo &loop,
                             LoopPromotionContext &promotion_context,
                             CoreIrContext &core_ir_context,
                             const CoreIrType *void_type) {
    const UnitLoopAccessInfo &access_info = *promotion_context.access_info;
    for (auto &entry : promotion_context.exit_incomings) {
        CoreIrBasicBlock *exit_block = entry.first;
        auto &incomings = entry.second;
        if (exit_block == nullptr || incomings.empty()) {
            continue;
        }
        CoreIrInstruction *anchor =
            find_first_exit_block_access_clobber(*exit_block, access_info);
        if (incomings.size() == 1) {
            rewrite_exit_block_local_loads(*exit_block, access_info,
                                           incomings.front().second);
            materialize_exit_store(*exit_block, core_ir_context, access_info,
                                   incomings.front().second, void_type, anchor);
            promotion_context.changed = true;
            continue;
        }

        auto phi = std::make_unique<CoreIrPhiInst>(
            access_info.value_type, access_info.slot->get_name() + ".exit");
        CoreIrPhiInst *phi_ptr = phi.get();
        for (const auto &incoming : incomings) {
            phi_ptr->add_incoming(incoming.first, incoming.second);
        }
        exit_block->insert_instruction_before_first_non_phi(std::move(phi));
        rewrite_exit_block_local_loads(*exit_block, access_info, phi_ptr);
        materialize_exit_store(*exit_block, core_ir_context, access_info,
                               phi_ptr, void_type, anchor);
        promotion_context.changed = true;
    }
}

bool unit_has_outside_accesses(
    const CoreIrFunction &function, const CoreIrLoopInfo &loop,
    const CoreIrPromotableStackSlotAnalysisResult &promotable_units,
    const UnitLoopAccessInfo &access_info) {
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || loop_contains_block(loop, block.get())) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }
            if (instruction_matches_access_info(*instruction, access_info)) {
                return true;
            }
            if (instruction_belongs_to_promotable_unit(
                    promotable_units, instruction, access_info)) {
                return true;
            }
        }
    }
    return false;
}

bool unit_has_outside_store(
    const CoreIrFunction &function, const CoreIrLoopInfo &loop,
    const CoreIrPromotableStackSlotAnalysisResult &promotable_units,
    const UnitLoopAccessInfo &access_info) {
    CoreIrBasicBlock *preheader = loop.get_preheader();
    std::size_t preheader_order = 0;
    bool have_preheader_order = false;
    std::unordered_map<const CoreIrBasicBlock *, std::size_t> block_order;
    std::size_t next_order = 0;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        block_order.emplace(block.get(), next_order);
        if (block.get() == preheader) {
            preheader_order = next_order;
            have_preheader_order = true;
        }
        ++next_order;
    }
    auto is_preheader_seed_store = [&](const CoreIrStoreInst *store) {
        if (store == nullptr || store->get_parent() == nullptr ||
            !have_preheader_order) {
            return false;
        }
        auto it = block_order.find(store->get_parent());
        return it != block_order.end() && it->second < preheader_order;
    };
    if (access_info.unit_info != nullptr) {
        for (CoreIrStoreInst *store : access_info.unit_info->stores) {
            if (store == nullptr || store->get_parent() == preheader ||
                is_preheader_seed_store(store) ||
                std::find(access_info.stores.begin(), access_info.stores.end(),
                          store) != access_info.stores.end()) {
                continue;
            }
            return true;
        }
    }
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || loop_contains_block(loop, block.get())) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
            if (store == nullptr || store->get_parent() == preheader ||
                is_preheader_seed_store(store)) {
                continue;
            }
            if (instruction_matches_access_info(*store, access_info)) {
                return true;
            }
            if (instruction_belongs_to_promotable_unit(
                    promotable_units, instruction, access_info)) {
                return true;
            }
        }
    }
    return false;
}

bool exit_blocks_contain_local_store_conflict(
    const CoreIrLoopInfo &loop, const UnitLoopAccessInfo &access_info) {
    if (access_info.kind == CoreIrPromotionUnitKind::AccessPath) {
        return false;
    }
    for (CoreIrBasicBlock *exit_block : loop.get_exit_blocks()) {
        if (exit_block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : exit_block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }
            auto *store = dynamic_cast<CoreIrStoreInst *>(instruction);
            if (store != nullptr &&
                instruction_matches_access_info(*store, access_info)) {
                return true;
            }
        }
    }
    return false;
}

bool loop_has_dedicated_exit_blocks(const CoreIrLoopInfo &loop,
                                    const CoreIrCfgAnalysisResult &cfg_analysis) {
    for (CoreIrBasicBlock *exit_block : loop.get_exit_blocks()) {
        if (exit_block == nullptr) {
            continue;
        }
        for (CoreIrBasicBlock *predecessor :
             cfg_analysis.get_predecessors(exit_block)) {
            if (!loop_contains_block(loop, predecessor)) {
                return false;
            }
        }
    }
    return true;
}

bool whole_slot_accesses_nested_subloop(const CoreIrLoopInfo &loop,
                                        const UnitLoopAccessInfo &access_info) {
    if (access_info.kind != CoreIrPromotionUnitKind::WholeSlot) {
        return false;
    }

    std::vector<const CoreIrLoopInfo *> worklist;
    for (CoreIrLoopInfo *subloop : loop.get_subloops()) {
        if (subloop != nullptr) {
            worklist.push_back(subloop);
        }
    }

    while (!worklist.empty()) {
        const CoreIrLoopInfo *subloop = worklist.back();
        worklist.pop_back();
        if (subloop == nullptr) {
            continue;
        }

        for (CoreIrBasicBlock *block : subloop->get_blocks()) {
            if (block != nullptr && (access_info.def_blocks.find(block) !=
                                         access_info.def_blocks.end() ||
                                     access_info.use_blocks.find(block) !=
                                         access_info.use_blocks.end())) {
                return true;
            }
        }

        for (CoreIrLoopInfo *child : subloop->get_subloops()) {
            if (child != nullptr) {
                worklist.push_back(child);
            }
        }
    }

    return false;
}

bool access_path_has_nonconstant_whole_slot_store(
    const CoreIrLoopInfo &loop, const UnitLoopAccessInfo &access_info) {
    if (access_info.kind != CoreIrPromotionUnitKind::AccessPath) {
        return false;
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

            CoreIrStackSlot *stack_slot = nullptr;
            std::vector<std::uint64_t> path;
            bool exact_path = true;
            if (!get_store_slot_and_path(*store, stack_slot, path,
                                         exact_path) ||
                !exact_path || stack_slot != access_info.slot ||
                !path.empty()) {
                continue;
            }

            if (dynamic_cast<const CoreIrConstant *>(store->get_value()) ==
                nullptr) {
                return true;
            }
        }
    }
    return false;
}

bool can_promote_slot_in_loop(
    const CoreIrFunction &function, const CoreIrLoopInfo &loop,
    const CoreIrPromotableStackSlotAnalysisResult &promotable_units,
    const UnitLoopAccessInfo &access_info) {
    if (access_info.slot == nullptr || access_info.loads.empty() ||
        access_info.stores.empty() || access_info.value_type == nullptr ||
        loop.get_preheader() == nullptr) {
        return false;
    }
    // Outer-loop promotion can bypass loop-local reset stores on the edge into
    // an inner loop, so keep whole-slot scalars in memory when the accesses
    // live in nested subloops.
    if (whole_slot_accesses_nested_subloop(loop, access_info)) {
        return false;
    }
    if (access_path_has_nonconstant_whole_slot_store(loop, access_info)) {
        return false;
    }
    if (access_info.kind == CoreIrPromotionUnitKind::WholeSlot &&
        unit_has_outside_store(function, loop, promotable_units, access_info)) {
        return false;
    }
    if (find_initial_value_in_preheader(*loop.get_preheader(), access_info,
                                        promotable_units) != nullptr) {
        return true;
    }
    return (loop.get_parent_loop() == nullptr ||
            access_info.kind == CoreIrPromotionUnitKind::AccessPath) &&
           loop.get_blocks().size() <= 8 &&
           count_loop_instructions(loop) <= 256 &&
           unit_has_outside_accesses(function, loop, promotable_units,
                                     access_info);
}

bool promote_slot_in_loop(
    CoreIrFunction &function, const CoreIrLoopInfo &loop,
    CoreIrAnalysisManager &analysis_manager,
    const CoreIrPromotableStackSlotAnalysisResult &promotable_units,
    CoreIrContext &core_ir_context, UnitLoopAccessInfo &access_info,
    const CoreIrType *void_type,
    const std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        &dominator_children) {
    if (!can_promote_slot_in_loop(function, loop, promotable_units,
                                  access_info)) {
        return false;
    }

    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *preheader = loop.get_preheader();
    if (header == nullptr || preheader == nullptr) {
        return false;
    }
    const CoreIrCfgAnalysisResult &cfg_analysis =
        analysis_manager.get_or_compute<CoreIrCfgAnalysis>(function);
    if (!loop_has_dedicated_exit_blocks(loop, cfg_analysis)) {
        return false;
    }

    CoreIrValue *initial_value = find_initial_value_in_preheader(
        *preheader, access_info, promotable_units);
    if (initial_value == nullptr) {
        initial_value = materialize_initial_value_load(
            *preheader, core_ir_context, access_info);
    }
    if (initial_value == nullptr) {
        return false;
    }

    LoopPromotionContext promotion_context;
    promotion_context.access_info = &access_info;
    promotion_context.dominator_children = &dominator_children;
    insert_loop_memory_phis(function, analysis_manager, loop,
                            promotion_context);
    auto header_phi_it = promotion_context.inserted_phis.find(header);
    if (header_phi_it != promotion_context.inserted_phis.end() &&
        header_phi_it->second != nullptr) {
        header_phi_it->second->add_incoming(preheader, initial_value);
    }

    rename_promoted_slot(*header, loop, promotion_context);
    materialize_exit_values(loop, promotion_context, core_ir_context,
                            void_type);
    return promotion_context.changed;
}

} // namespace

PassKind CoreIrLoopMemoryPromotionPass::Kind() const {
    return PassKind::CoreIrLoopMemoryPromotion;
}

const char *CoreIrLoopMemoryPromotionPass::Name() const {
    return "CoreIrLoopMemoryPromotionPass";
}

PassResult CoreIrLoopMemoryPromotionPass::Run(CompilerContext &context) {
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
            "missing core ir loop memory promotion dependencies");
    }
    const CoreIrType *void_type =
        core_ir_context->create_type<CoreIrVoidType>();

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
        const CoreIrPromotableStackSlotAnalysisResult &promotable_units =
            analysis_manager->get_or_compute<CoreIrPromotableStackSlotAnalysis>(
                *function);
        bool function_changed = false;
        std::vector<const CoreIrLoopInfo *> ordered_loops;
        ordered_loops.reserve(loop_info.get_loops().size());
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr != nullptr) {
                ordered_loops.push_back(loop_ptr.get());
            }
        }
        std::stable_sort(
            ordered_loops.begin(), ordered_loops.end(),
            [](const CoreIrLoopInfo *lhs, const CoreIrLoopInfo *rhs) {
                if (lhs == nullptr || rhs == nullptr) {
                    return rhs != nullptr;
                }
                return lhs->get_depth() > rhs->get_depth();
            });

        std::unordered_map<CoreIrBasicBlock *,
                           std::vector<CoreIrBasicBlock *>>
            dominator_children;
        if (!ordered_loops.empty()) {
            build_dominator_children(*function, *analysis_manager,
                                     dominator_children);
        }
        for (const CoreIrLoopInfo *loop_ptr : ordered_loops) {
            auto access_infos =
                collect_loop_unit_accesses(*loop_ptr, promotable_units);
            for (UnitLoopAccessInfo &access_info : access_infos) {
                function_changed = promote_slot_in_loop(
                                       *function, *loop_ptr, *analysis_manager,
                                       promotable_units, *core_ir_context,
                                       access_info, void_type,
                                       dominator_children) ||
                                   function_changed;
            }
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
