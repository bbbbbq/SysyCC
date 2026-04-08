#include "backend/ir/analysis/alias_analysis.hpp"

#include "backend/ir/effect/core_ir_memory_location.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
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

CoreIrAliasKind
alias_same_root_memory_locations(const CoreIrMemoryLocation &lhs,
                                 const CoreIrMemoryLocation &rhs) noexcept {
    if (same_access_path(lhs.access_path, rhs.access_path)) {
        return CoreIrAliasKind::MustAlias;
    }
    if (is_path_prefix(lhs.access_path, rhs.access_path) ||
        is_path_prefix(rhs.access_path, lhs.access_path)) {
        return CoreIrAliasKind::MayAlias;
    }
    return CoreIrAliasKind::NoAlias;
}

} // namespace

CoreIrAliasKind
alias_core_ir_memory_locations(const CoreIrMemoryLocation &lhs,
                               const CoreIrMemoryLocation &rhs) noexcept {
    if (lhs.kind == CoreIrMemoryLocationKind::Unknown ||
        rhs.kind == CoreIrMemoryLocationKind::Unknown) {
        return CoreIrAliasKind::MayAlias;
    }

    if (lhs.kind == CoreIrMemoryLocationKind::StackSlot ||
        rhs.kind == CoreIrMemoryLocationKind::StackSlot) {
        if (lhs.kind != CoreIrMemoryLocationKind::StackSlot ||
            rhs.kind != CoreIrMemoryLocationKind::StackSlot) {
            return CoreIrAliasKind::NoAlias;
        }
        if (lhs.stack_slot != rhs.stack_slot) {
            return CoreIrAliasKind::NoAlias;
        }
        return alias_same_root_memory_locations(lhs, rhs);
    }

    if (lhs.kind == CoreIrMemoryLocationKind::Global &&
        rhs.kind == CoreIrMemoryLocationKind::Global) {
        if (lhs.global != rhs.global) {
            return CoreIrAliasKind::NoAlias;
        }
        return alias_same_root_memory_locations(lhs, rhs);
    }

    if (lhs.kind == CoreIrMemoryLocationKind::ArgumentDerived &&
        rhs.kind == CoreIrMemoryLocationKind::ArgumentDerived) {
        if (lhs.parameter != rhs.parameter) {
            return CoreIrAliasKind::MayAlias;
        }
        return alias_same_root_memory_locations(lhs, rhs);
    }

    if ((lhs.kind == CoreIrMemoryLocationKind::ArgumentDerived &&
         rhs.kind == CoreIrMemoryLocationKind::Global) ||
        (lhs.kind == CoreIrMemoryLocationKind::Global &&
         rhs.kind == CoreIrMemoryLocationKind::ArgumentDerived)) {
        return CoreIrAliasKind::MayAlias;
    }

    return lhs.kind == rhs.kind ? alias_same_root_memory_locations(lhs, rhs)
                                : CoreIrAliasKind::MayAlias;
}

CoreIrAliasAnalysisResult::CoreIrAliasAnalysisResult(
    const CoreIrFunction *function,
    std::unordered_map<const CoreIrValue *, CoreIrMemoryLocation>
        value_locations,
    std::unordered_map<const CoreIrInstruction *, CoreIrMemoryLocation>
        instruction_locations) noexcept
    : function_(function), value_locations_(std::move(value_locations)),
      instruction_locations_(std::move(instruction_locations)) {}

const CoreIrMemoryLocation *CoreIrAliasAnalysisResult::get_location_for_value(
    const CoreIrValue *value) const noexcept {
    auto it = value_locations_.find(value);
    return it == value_locations_.end() ? nullptr : &it->second;
}

const CoreIrMemoryLocation *
CoreIrAliasAnalysisResult::get_location_for_instruction(
    const CoreIrInstruction *instruction) const noexcept {
    auto it = instruction_locations_.find(instruction);
    return it == instruction_locations_.end() ? nullptr : &it->second;
}

CoreIrAliasKind
CoreIrAliasAnalysisResult::alias_values(const CoreIrValue *lhs,
                                        const CoreIrValue *rhs) const noexcept {
    const CoreIrMemoryLocation *lhs_location = get_location_for_value(lhs);
    const CoreIrMemoryLocation *rhs_location = get_location_for_value(rhs);
    if (lhs_location == nullptr || rhs_location == nullptr) {
        return CoreIrAliasKind::MayAlias;
    }
    return alias_core_ir_memory_locations(*lhs_location, *rhs_location);
}

CoreIrAliasKind CoreIrAliasAnalysisResult::alias_instructions(
    const CoreIrInstruction *lhs, const CoreIrInstruction *rhs) const noexcept {
    const CoreIrMemoryLocation *lhs_location =
        get_location_for_instruction(lhs);
    const CoreIrMemoryLocation *rhs_location =
        get_location_for_instruction(rhs);
    if (lhs_location == nullptr || rhs_location == nullptr) {
        return CoreIrAliasKind::MayAlias;
    }
    return alias_core_ir_memory_locations(*lhs_location, *rhs_location);
}

CoreIrAliasAnalysisResult
CoreIrAliasAnalysis::Run(const CoreIrFunction &function) const {
    std::unordered_map<const CoreIrValue *, CoreIrMemoryLocation>
        value_locations;
    std::unordered_map<const CoreIrInstruction *, CoreIrMemoryLocation>
        instruction_locations;

    for (const auto &parameter : function.get_parameters()) {
        if (parameter == nullptr || parameter->get_type() == nullptr ||
            parameter->get_type()->get_kind() != CoreIrTypeKind::Pointer) {
            continue;
        }
        CoreIrMemoryLocation location =
            describe_memory_location(parameter.get());
        if (!location.is_unknown()) {
            value_locations.emplace(parameter.get(), std::move(location));
        }
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

            CoreIrMemoryLocation location =
                CoreIrMemoryLocation::make_unknown();
            bool have_location = false;
            if (auto *load = dynamic_cast<const CoreIrLoadInst *>(instruction);
                load != nullptr) {
                if (load->get_stack_slot() != nullptr) {
                    location.kind = CoreIrMemoryLocationKind::StackSlot;
                    location.stack_slot = load->get_stack_slot();
                    have_location = true;
                } else {
                    location = describe_memory_location(load->get_address());
                    have_location = !location.is_unknown();
                }
            } else if (auto *store =
                           dynamic_cast<const CoreIrStoreInst *>(instruction);
                       store != nullptr) {
                if (store->get_stack_slot() != nullptr) {
                    location.kind = CoreIrMemoryLocationKind::StackSlot;
                    location.stack_slot = store->get_stack_slot();
                    have_location = true;
                } else {
                    location = describe_memory_location(store->get_address());
                    have_location = !location.is_unknown();
                }
            } else {
                location = describe_memory_location(instruction);
                have_location = !location.is_unknown();
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
