#include "backend/asm_gen/aarch64/support/aarch64_phi_lowering_support.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <unordered_set>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

namespace {

std::string sanitize_label_fragment(const std::string &text) {
    std::string sanitized;
    sanitized.reserve(text.size());
    for (char ch : text) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        return "unnamed";
    }
    return sanitized;
}

std::vector<const CoreIrPhiInst *> collect_block_phis(
    const CoreIrBasicBlock &basic_block) {
    std::vector<const CoreIrPhiInst *> phis;
    for (const auto &instruction : basic_block.get_instructions()) {
        if (instruction == nullptr || instruction->get_opcode() != CoreIrOpcode::Phi) {
            break;
        }
        phis.push_back(static_cast<const CoreIrPhiInst *>(instruction.get()));
    }
    return phis;
}

bool append_predecessor_once(
    std::unordered_map<const CoreIrBasicBlock *,
                       std::vector<const CoreIrBasicBlock *>> &predecessors,
    const CoreIrBasicBlock *successor, const CoreIrBasicBlock *predecessor,
    const AArch64PhiPlanContext &context) {
    if (successor == nullptr || predecessor == nullptr) {
        context.report_error(
            "encountered null CFG edge while planning AArch64 phi lowering");
        return false;
    }
    auto &incoming = predecessors[successor];
    if (std::find(incoming.begin(), incoming.end(), predecessor) == incoming.end()) {
        incoming.push_back(predecessor);
    }
    return true;
}

bool collect_block_predecessors(
    const CoreIrFunction &function,
    std::unordered_map<const CoreIrBasicBlock *,
                       std::vector<const CoreIrBasicBlock *>> &predecessors,
    const AArch64PhiPlanContext &context) {
    for (const auto &basic_block : function.get_basic_blocks()) {
        if (basic_block == nullptr || basic_block->get_instructions().empty()) {
            continue;
        }
        const CoreIrInstruction *terminator =
            basic_block->get_instructions().back().get();
        if (terminator == nullptr || !terminator->get_is_terminator()) {
            context.report_error(
                "encountered basic block without terminator while planning "
                "AArch64 phi lowering");
            return false;
        }
        switch (terminator->get_opcode()) {
        case CoreIrOpcode::Jump:
            if (!append_predecessor_once(
                    predecessors,
                    static_cast<const CoreIrJumpInst *>(terminator)->get_target_block(),
                    basic_block.get(), context)) {
                return false;
            }
            break;
        case CoreIrOpcode::CondJump: {
            const auto *cond_jump =
                static_cast<const CoreIrCondJumpInst *>(terminator);
            if (!append_predecessor_once(predecessors, cond_jump->get_true_block(),
                                         basic_block.get(), context) ||
                !append_predecessor_once(predecessors, cond_jump->get_false_block(),
                                         basic_block.get(), context)) {
                return false;
            }
            break;
        }
        case CoreIrOpcode::Return:
            break;
        default:
            break;
        }
    }
    return true;
}

const CoreIrValue *find_phi_incoming_value(const CoreIrPhiInst &phi,
                                           const CoreIrBasicBlock *predecessor) {
    for (std::size_t index = 0; index < phi.get_incoming_count(); ++index) {
        if (phi.get_incoming_block(index) == predecessor) {
            return phi.get_incoming_value(index);
        }
    }
    return nullptr;
}

} // namespace

std::size_t AArch64PhiEdgeKeyHash::operator()(
    const AArch64PhiEdgeKey &key) const noexcept {
    const std::size_t lhs = std::hash<const CoreIrBasicBlock *>{}(key.predecessor);
    const std::size_t rhs = std::hash<const CoreIrBasicBlock *>{}(key.successor);
    return lhs ^ (rhs + 0x9e3779b97f4a7c15ULL + (lhs << 6U) + (lhs >> 2U));
}

bool build_phi_edge_plans(
    const CoreIrFunction &function, const AArch64PhiPlanContext &context,
    std::unordered_map<AArch64PhiEdgeKey, std::string, AArch64PhiEdgeKeyHash>
        &phi_edge_labels,
    std::vector<AArch64PhiEdgePlan> &phi_edge_plans) {
    std::unordered_map<const CoreIrBasicBlock *, std::vector<const CoreIrBasicBlock *>>
        predecessors;
    if (!collect_block_predecessors(function, predecessors, context)) {
        return false;
    }

    for (const auto &basic_block : function.get_basic_blocks()) {
        if (basic_block == nullptr) {
            continue;
        }
        const std::vector<const CoreIrPhiInst *> phis =
            collect_block_phis(*basic_block);
        if (phis.empty()) {
            continue;
        }

        const auto predecessor_it = predecessors.find(basic_block.get());
        if (predecessor_it == predecessors.end()) {
            context.report_error(
                "encountered phi block without predecessors in the AArch64 "
                "native backend");
            return false;
        }

        for (const CoreIrBasicBlock *predecessor : predecessor_it->second) {
            AArch64PhiEdgePlan plan;
            plan.edge = AArch64PhiEdgeKey{predecessor, basic_block.get()};
            plan.edge_label = context.block_label(predecessor) + "_to_" +
                              sanitize_label_fragment(basic_block->get_name()) +
                              "_phi";
            for (const CoreIrPhiInst *phi : phis) {
                if (phi == nullptr) {
                    continue;
                }
                if (is_aggregate_type(phi->get_type())) {
                    context.report_error(
                        "aggregate phi lowering is not supported by the current "
                        "AArch64 native backend");
                    return false;
                }
                const CoreIrValue *incoming =
                    find_phi_incoming_value(*phi, predecessor);
                if (incoming == nullptr) {
                    context.report_error(
                        "phi block is missing an incoming value for one of its "
                        "predecessors in the AArch64 native backend");
                    return false;
                }
                plan.copies.push_back(AArch64PhiCopyOp{phi, incoming});
            }
            phi_edge_labels.emplace(plan.edge, plan.edge_label);
            phi_edge_plans.push_back(std::move(plan));
        }
    }
    return true;
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
