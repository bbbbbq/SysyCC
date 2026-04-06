#include "backend/asm_gen/aarch64/support/aarch64_phi_lowering_support.hpp"

#include <optional>
#include <unordered_set>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

std::size_t AArch64PhiEdgeKeyHash::operator()(
    const AArch64PhiEdgeKey &key) const noexcept {
    const std::size_t lhs = std::hash<const CoreIrBasicBlock *>{}(key.predecessor);
    const std::size_t rhs = std::hash<const CoreIrBasicBlock *>{}(key.successor);
    return lhs ^ (rhs + 0x9e3779b97f4a7c15ULL + (lhs << 6U) + (lhs >> 2U));
}

bool emit_parallel_phi_copies(AArch64MachineBlock &machine_block,
                              const AArch64PhiEdgePlan &plan,
                              AArch64MachineFunction &function,
                              AArch64PhiCopyLoweringContext &context) {
    struct PendingPhiCopy {
        AArch64VirtualReg destination;
        std::optional<AArch64VirtualReg> source_reg;
        const CoreIrValue *source_value = nullptr;
    };

    std::vector<PendingPhiCopy> pending;
    for (const AArch64PhiCopyOp &copy : plan.copies) {
        if (copy.phi == nullptr || copy.source_value == nullptr) {
            context.report_error(
                "encountered malformed phi copy while lowering the AArch64 "
                "native backend");
            return false;
        }
        PendingPhiCopy pending_copy;
        if (!context.require_canonical_vreg(copy.phi, pending_copy.destination)) {
            return false;
        }
        pending_copy.source_value = copy.source_value;
        AArch64VirtualReg source_reg;
        if (context.try_get_value_vreg(copy.source_value, source_reg)) {
            pending_copy.source_reg = source_reg;
        }
        if (pending_copy.source_reg.has_value() &&
            pending_copy.source_reg->get_id() == pending_copy.destination.get_id() &&
            pending_copy.source_reg->get_kind() ==
                pending_copy.destination.get_kind()) {
            continue;
        }
        pending.push_back(std::move(pending_copy));
    }

    while (!pending.empty()) {
        bool progressed = false;
        std::unordered_set<std::size_t> pending_destinations;
        for (const PendingPhiCopy &copy : pending) {
            pending_destinations.insert(copy.destination.get_id());
        }

        for (auto it = pending.begin(); it != pending.end(); ++it) {
            const bool source_is_blocked =
                it->source_reg.has_value() &&
                pending_destinations.find(it->source_reg->get_id()) !=
                    pending_destinations.end();
            if (source_is_blocked) {
                continue;
            }
            if (it->source_reg.has_value()) {
                append_register_copy(machine_block, it->destination,
                                     *it->source_reg);
            } else if (!context.materialize_value(machine_block, it->source_value,
                                                  it->destination)) {
                return false;
            }
            pending.erase(it);
            progressed = true;
            break;
        }

        if (progressed) {
            continue;
        }

        PendingPhiCopy &cycle_head = pending.front();
        if (!cycle_head.source_reg.has_value()) {
            context.report_error(
                "failed to schedule phi copies in the AArch64 native backend");
            return false;
        }
        const AArch64VirtualReg saved_destination =
            function.create_virtual_reg(cycle_head.destination.get_kind());
        append_register_copy(machine_block, saved_destination,
                             cycle_head.destination);
        for (PendingPhiCopy &copy : pending) {
            if (copy.source_reg.has_value() &&
                copy.source_reg->get_id() == cycle_head.destination.get_id()) {
                copy.source_reg = saved_destination;
            }
        }
    }

    return true;
}

} // namespace sysycc
