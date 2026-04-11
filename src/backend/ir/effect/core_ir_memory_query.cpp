#include "backend/ir/effect/core_ir_memory_query.hpp"

#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"

namespace sysycc {

namespace {

using sysycc::detail::are_equivalent_pointer_values;

CoreIrStackSlot *get_memory_stack_slot(const CoreIrInstruction &instruction) {
    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        return load->get_stack_slot();
    }
    if (const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
        store != nullptr) {
        return store->get_stack_slot();
    }
    return nullptr;
}

CoreIrValue *get_memory_address_value(const CoreIrInstruction &instruction) {
    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        return load->get_address();
    }
    if (const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
        store != nullptr) {
        return store->get_address();
    }
    return nullptr;
}

bool instructions_share_exact_memory_access(const CoreIrInstruction &lhs,
                                            const CoreIrInstruction &rhs) {
    CoreIrStackSlot *lhs_slot = get_memory_stack_slot(lhs);
    CoreIrStackSlot *rhs_slot = get_memory_stack_slot(rhs);
    if (lhs_slot != nullptr || rhs_slot != nullptr) {
        return lhs_slot != nullptr && lhs_slot == rhs_slot &&
               rhs_slot != nullptr;
    }

    CoreIrValue *lhs_address = get_memory_address_value(lhs);
    CoreIrValue *rhs_address = get_memory_address_value(rhs);
    return lhs_address != nullptr &&
           are_equivalent_pointer_values(lhs_address, rhs_address) &&
           rhs_address != nullptr;
}

} // namespace

bool core_ir_memory_location_is_precise(
    const CoreIrMemoryLocation *location) noexcept {
    return location != nullptr && !location->is_unknown() &&
           location->exact_access_path;
}

CoreIrAliasKind get_precise_core_ir_memory_alias_kind(
    const CoreIrInstruction &lhs, const CoreIrInstruction &rhs,
    const CoreIrAliasAnalysisResult &alias_analysis) noexcept {
    const CoreIrAliasKind alias_kind =
        alias_analysis.alias_instructions(&lhs, &rhs);
    if (alias_kind != CoreIrAliasKind::MayAlias) {
        return alias_kind;
    }
    return instructions_share_exact_memory_access(lhs, rhs)
               ? CoreIrAliasKind::MustAlias
               : alias_kind;
}

bool core_ir_memory_location_is_non_escaping_local(
    const CoreIrMemoryLocation *location,
    const CoreIrEscapeAnalysisResult *escape_analysis) noexcept {
    return location != nullptr && escape_analysis != nullptr &&
           !location->is_unknown() &&
           escape_analysis->is_non_escaping_location(*location);
}

} // namespace sysycc
