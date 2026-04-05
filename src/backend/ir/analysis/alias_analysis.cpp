#include "backend/ir/analysis/alias_analysis.hpp"

#include <optional>
#include <vector>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

bool same_access_path(const std::vector<std::uint64_t> &lhs,
                      const std::vector<std::uint64_t> &rhs) noexcept {
    return lhs == rhs;
}

bool is_path_prefix(const std::vector<std::uint64_t> &prefix,
                    const std::vector<std::uint64_t> &full) noexcept {
    if (prefix.size() > full.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (prefix[index] != full[index]) {
            return false;
        }
    }
    return true;
}

std::optional<std::size_t>
find_parameter_index(const CoreIrFunction &function, const CoreIrValue *value) noexcept {
    const auto &parameters = function.get_parameters();
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (parameters[index].get() == value) {
            return index;
        }
    }
    return std::nullopt;
}

bool trace_memory_location(const CoreIrFunction &function, const CoreIrValue *value,
                           CoreIrMemoryLocation &location) {
    if (value == nullptr) {
        return false;
    }

    if (const auto parameter_index = find_parameter_index(function, value);
        parameter_index.has_value()) {
        location.root_kind = CoreIrMemoryLocationRootKind::ArgumentDerived;
        location.parameter_index = *parameter_index;
        return true;
    }

    if (auto *address_of_stack =
            dynamic_cast<const CoreIrAddressOfStackSlotInst *>(value);
        address_of_stack != nullptr) {
        location.root_kind = CoreIrMemoryLocationRootKind::StackSlot;
        location.stack_slot = address_of_stack->get_stack_slot();
        return location.stack_slot != nullptr;
    }

    if (auto *address_of_global =
            dynamic_cast<const CoreIrAddressOfGlobalInst *>(value);
        address_of_global != nullptr) {
        location.root_kind = CoreIrMemoryLocationRootKind::Global;
        location.global = address_of_global->get_global();
        return location.global != nullptr;
    }

    auto *gep = dynamic_cast<const CoreIrGetElementPtrInst *>(value);
    if (gep == nullptr) {
        return false;
    }

    CoreIrMemoryLocation base_location = CoreIrMemoryLocation::make_unknown();
    if (!trace_memory_location(function, gep->get_base(), base_location)) {
        return false;
    }

    std::vector<std::uint64_t> path = base_location.access_path;
    for (std::size_t index = 0; index < gep->get_index_count(); ++index) {
        const auto *constant_index =
            dynamic_cast<const CoreIrConstantInt *>(gep->get_index(index));
        if (constant_index == nullptr) {
            return false;
        }
        path.push_back(constant_index->get_value());
    }
    if (!path.empty() && path.front() == 0) {
        path.erase(path.begin());
    }
    base_location.access_path = std::move(path);
    location = std::move(base_location);
    return true;
}

} // namespace

CoreIrAliasKind alias_core_ir_memory_locations(
    const CoreIrMemoryLocation &lhs, const CoreIrMemoryLocation &rhs) noexcept {
    if (lhs.root_kind == CoreIrMemoryLocationRootKind::Unknown ||
        rhs.root_kind == CoreIrMemoryLocationRootKind::Unknown) {
        return CoreIrAliasKind::MayAlias;
    }

    if (lhs.root_kind != rhs.root_kind) {
        return CoreIrAliasKind::NoAlias;
    }

    switch (lhs.root_kind) {
    case CoreIrMemoryLocationRootKind::StackSlot:
        if (lhs.stack_slot != rhs.stack_slot) {
            return CoreIrAliasKind::NoAlias;
        }
        break;
    case CoreIrMemoryLocationRootKind::Global:
        if (lhs.global != rhs.global) {
            return CoreIrAliasKind::NoAlias;
        }
        break;
    case CoreIrMemoryLocationRootKind::ArgumentDerived:
        if (lhs.parameter_index != rhs.parameter_index) {
            return CoreIrAliasKind::NoAlias;
        }
        break;
    case CoreIrMemoryLocationRootKind::Unknown:
        return CoreIrAliasKind::MayAlias;
    }

    if (same_access_path(lhs.access_path, rhs.access_path)) {
        return CoreIrAliasKind::MustAlias;
    }
    if (is_path_prefix(lhs.access_path, rhs.access_path) ||
        is_path_prefix(rhs.access_path, lhs.access_path)) {
        return CoreIrAliasKind::MayAlias;
    }
    return CoreIrAliasKind::NoAlias;
}

CoreIrAliasAnalysisResult::CoreIrAliasAnalysisResult(
    const CoreIrFunction *function,
    std::unordered_map<const CoreIrValue *, CoreIrMemoryLocation> value_locations,
    std::unordered_map<const CoreIrInstruction *, CoreIrMemoryLocation>
        instruction_locations) noexcept
    : function_(function), value_locations_(std::move(value_locations)),
      instruction_locations_(std::move(instruction_locations)) {}

const CoreIrMemoryLocation *
CoreIrAliasAnalysisResult::get_location_for_value(const CoreIrValue *value) const noexcept {
    auto it = value_locations_.find(value);
    return it == value_locations_.end() ? nullptr : &it->second;
}

const CoreIrMemoryLocation *CoreIrAliasAnalysisResult::get_location_for_instruction(
    const CoreIrInstruction *instruction) const noexcept {
    auto it = instruction_locations_.find(instruction);
    return it == instruction_locations_.end() ? nullptr : &it->second;
}

CoreIrAliasKind CoreIrAliasAnalysisResult::alias_values(
    const CoreIrValue *lhs, const CoreIrValue *rhs) const noexcept {
    const CoreIrMemoryLocation *lhs_location = get_location_for_value(lhs);
    const CoreIrMemoryLocation *rhs_location = get_location_for_value(rhs);
    if (lhs_location == nullptr || rhs_location == nullptr) {
        return CoreIrAliasKind::MayAlias;
    }
    return alias_core_ir_memory_locations(*lhs_location, *rhs_location);
}

CoreIrAliasKind CoreIrAliasAnalysisResult::alias_instructions(
    const CoreIrInstruction *lhs, const CoreIrInstruction *rhs) const noexcept {
    const CoreIrMemoryLocation *lhs_location = get_location_for_instruction(lhs);
    const CoreIrMemoryLocation *rhs_location = get_location_for_instruction(rhs);
    if (lhs_location == nullptr || rhs_location == nullptr) {
        return CoreIrAliasKind::MayAlias;
    }
    return alias_core_ir_memory_locations(*lhs_location, *rhs_location);
}

CoreIrAliasAnalysisResult CoreIrAliasAnalysis::Run(
    const CoreIrFunction &function) const {
    std::unordered_map<const CoreIrValue *, CoreIrMemoryLocation> value_locations;
    std::unordered_map<const CoreIrInstruction *, CoreIrMemoryLocation>
        instruction_locations;

    const auto &parameters = function.get_parameters();
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        const auto &parameter = parameters[index];
        if (parameter == nullptr || parameter->get_type() == nullptr ||
            parameter->get_type()->get_kind() != CoreIrTypeKind::Pointer) {
            continue;
        }
        CoreIrMemoryLocation location = CoreIrMemoryLocation::make_unknown();
        location.root_kind = CoreIrMemoryLocationRootKind::ArgumentDerived;
        location.parameter_index = index;
        value_locations.emplace(parameter.get(), std::move(location));
    }

    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            const CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr) {
                continue;
            }

            CoreIrMemoryLocation location = CoreIrMemoryLocation::make_unknown();
            bool have_location = false;
            if (auto *load = dynamic_cast<const CoreIrLoadInst *>(instruction);
                load != nullptr) {
                if (load->get_stack_slot() != nullptr) {
                    location.root_kind = CoreIrMemoryLocationRootKind::StackSlot;
                    location.stack_slot = load->get_stack_slot();
                    have_location = true;
                } else {
                    have_location = trace_memory_location(function, load->get_address(),
                                                          location);
                }
            } else if (auto *store =
                           dynamic_cast<const CoreIrStoreInst *>(instruction);
                       store != nullptr) {
                if (store->get_stack_slot() != nullptr) {
                    location.root_kind = CoreIrMemoryLocationRootKind::StackSlot;
                    location.stack_slot = store->get_stack_slot();
                    have_location = true;
                } else {
                    have_location =
                        trace_memory_location(function, store->get_address(), location);
                }
            } else {
                have_location = trace_memory_location(function, instruction, location);
            }

            if (!have_location) {
                continue;
            }
            value_locations.emplace(instruction, location);
            instruction_locations.emplace(instruction, std::move(location));
        }
    }

    return CoreIrAliasAnalysisResult(&function, std::move(value_locations),
                                     std::move(instruction_locations));
}

} // namespace sysycc
