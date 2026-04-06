#include "backend/asm_gen/aarch64/passes/aarch64_register_allocation_pass.hpp"

#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

constexpr unsigned kCallerSavedAllocatablePhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::X9),
    static_cast<unsigned>(AArch64PhysicalReg::X10),
    static_cast<unsigned>(AArch64PhysicalReg::X11),
    static_cast<unsigned>(AArch64PhysicalReg::X12),
    static_cast<unsigned>(AArch64PhysicalReg::X13),
    static_cast<unsigned>(AArch64PhysicalReg::X14),
    static_cast<unsigned>(AArch64PhysicalReg::X15),
};
constexpr unsigned kCalleeSavedAllocatablePhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::X19),
    static_cast<unsigned>(AArch64PhysicalReg::X20),
    static_cast<unsigned>(AArch64PhysicalReg::X21),
    static_cast<unsigned>(AArch64PhysicalReg::X22),
    static_cast<unsigned>(AArch64PhysicalReg::X23),
};
constexpr unsigned kCallerSavedAllocatableFloatPhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::V16),
    static_cast<unsigned>(AArch64PhysicalReg::V17),
    static_cast<unsigned>(AArch64PhysicalReg::V18),
    static_cast<unsigned>(AArch64PhysicalReg::V19),
    static_cast<unsigned>(AArch64PhysicalReg::V20),
    static_cast<unsigned>(AArch64PhysicalReg::V21),
    static_cast<unsigned>(AArch64PhysicalReg::V22),
    static_cast<unsigned>(AArch64PhysicalReg::V23),
    static_cast<unsigned>(AArch64PhysicalReg::V24),
    static_cast<unsigned>(AArch64PhysicalReg::V25),
    static_cast<unsigned>(AArch64PhysicalReg::V26),
    static_cast<unsigned>(AArch64PhysicalReg::V27),
};
constexpr unsigned kCalleeSavedAllocatableFloatPhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::V8),
    static_cast<unsigned>(AArch64PhysicalReg::V9),
    static_cast<unsigned>(AArch64PhysicalReg::V10),
    static_cast<unsigned>(AArch64PhysicalReg::V11),
    static_cast<unsigned>(AArch64PhysicalReg::V12),
    static_cast<unsigned>(AArch64PhysicalReg::V13),
    static_cast<unsigned>(AArch64PhysicalReg::V14),
    static_cast<unsigned>(AArch64PhysicalReg::V15),
};

std::size_t align_to(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

std::size_t virtual_reg_size(AArch64VirtualRegKind kind) {
    switch (kind) {
    case AArch64VirtualRegKind::General32:
    case AArch64VirtualRegKind::Float32:
        return 4;
    case AArch64VirtualRegKind::General64:
    case AArch64VirtualRegKind::Float64:
        return 8;
    case AArch64VirtualRegKind::Float16:
        return 2;
    case AArch64VirtualRegKind::Float128:
        return 16;
    }
    return 4;
}

bool is_live_across_call_callee_saved_capable(AArch64VirtualRegKind kind) {
    return kind != AArch64VirtualRegKind::Float128;
}

bool is_callee_saved_allocatable_physical_reg(unsigned reg) {
    return std::find(std::begin(kCalleeSavedAllocatablePhysicalRegs),
                     std::end(kCalleeSavedAllocatablePhysicalRegs),
                     reg) != std::end(kCalleeSavedAllocatablePhysicalRegs);
}

bool is_callee_saved_allocatable_float_physical_reg(unsigned reg) {
    return std::find(std::begin(kCalleeSavedAllocatableFloatPhysicalRegs),
                     std::end(kCalleeSavedAllocatableFloatPhysicalRegs),
                     reg) != std::end(kCalleeSavedAllocatableFloatPhysicalRegs);
}

struct AArch64InstructionLiveness {
    std::unordered_set<std::size_t> defs;
    std::unordered_set<std::size_t> uses;
};

struct AArch64BlockLiveness {
    std::vector<AArch64InstructionLiveness> instructions;
    std::unordered_set<std::size_t> defs;
    std::unordered_set<std::size_t> uses;
    std::unordered_set<std::size_t> live_in;
    std::unordered_set<std::size_t> live_out;
    std::vector<std::size_t> successors;
};

class AArch64LivenessInfo {
  private:
    std::vector<AArch64BlockLiveness> blocks_;
    std::unordered_set<std::size_t> live_across_call_;

  public:
    AArch64LivenessInfo(std::vector<AArch64BlockLiveness> blocks,
                        std::unordered_set<std::size_t> live_across_call)
        : blocks_(std::move(blocks)),
          live_across_call_(std::move(live_across_call)) {}

    const std::vector<AArch64BlockLiveness> &get_blocks() const noexcept {
        return blocks_;
    }

    const std::unordered_set<std::size_t> &get_live_across_call() const noexcept {
        return live_across_call_;
    }
};

class AArch64InterferenceGraph {
  private:
    std::unordered_map<std::size_t, std::unordered_set<std::size_t>> adjacency_;

  public:
    void add_node(std::size_t virtual_reg_id) { adjacency_[virtual_reg_id]; }

    void add_edge(std::size_t lhs, std::size_t rhs) {
        if (lhs == rhs) {
            return;
        }
        adjacency_[lhs].insert(rhs);
        adjacency_[rhs].insert(lhs);
    }

    void add_clique(const std::unordered_set<std::size_t> &live_set) {
        if (live_set.size() < 2) {
            for (std::size_t virtual_reg_id : live_set) {
                add_node(virtual_reg_id);
            }
            return;
        }
        std::vector<std::size_t> nodes(live_set.begin(), live_set.end());
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            for (std::size_t other_index = index + 1; other_index < nodes.size();
                 ++other_index) {
                add_edge(nodes[index], nodes[other_index]);
            }
        }
    }

    const std::unordered_map<std::size_t, std::unordered_set<std::size_t>> &
    get_adjacency() const noexcept {
        return adjacency_;
    }

    const std::unordered_set<std::size_t> &
    get_neighbors(std::size_t virtual_reg_id) const noexcept {
        static const std::unordered_set<std::size_t> empty;
        const auto it = adjacency_.find(virtual_reg_id);
        if (it == adjacency_.end()) {
            return empty;
        }
        return it->second;
    }
};

AArch64InstructionLiveness
collect_instruction_defs_uses(const AArch64MachineInstr &instruction) {
    AArch64InstructionLiveness liveness;
    for (std::size_t id : instruction.get_explicit_defs()) {
        liveness.defs.insert(id);
    }
    for (std::size_t id : instruction.get_explicit_uses()) {
        liveness.uses.insert(id);
    }
    for (std::size_t id : instruction.get_implicit_defs()) {
        liveness.defs.insert(id);
    }
    for (std::size_t id : instruction.get_implicit_uses()) {
        liveness.uses.insert(id);
    }
    return liveness;
}

std::vector<std::size_t> collect_block_successors(
    const AArch64MachineFunction &function,
    const std::unordered_map<std::string, std::size_t> &label_to_index,
    std::size_t block_index) {
    const auto &blocks = function.get_blocks();
    std::vector<std::size_t> successors;
    if (block_index >= blocks.size()) {
        return successors;
    }

    const auto &instructions = blocks[block_index].get_instructions();
    if (instructions.empty()) {
        if (block_index + 1 < blocks.size()) {
            successors.push_back(block_index + 1);
        }
        return successors;
    }

    const AArch64MachineInstr &last = instructions.back();
    if (last.get_mnemonic() == "ret") {
        return successors;
    }

    if (instructions.size() >= 2) {
        const AArch64MachineInstr &second_last = instructions[instructions.size() - 2];
        if (second_last.get_mnemonic() == "cbnz" && last.get_mnemonic() == "b") {
            if (second_last.get_operands().size() >= 2) {
                const auto it =
                    label_to_index.find(second_last.get_operands()[1].get_text());
                if (it != label_to_index.end()) {
                    successors.push_back(it->second);
                }
            }
            if (!last.get_operands().empty()) {
                const auto it = label_to_index.find(last.get_operands()[0].get_text());
                if (it != label_to_index.end()) {
                    successors.push_back(it->second);
                }
            }
            return successors;
        }
    }

    if (last.get_mnemonic() == "cbnz") {
        if (last.get_operands().size() >= 2) {
            const auto it = label_to_index.find(last.get_operands()[1].get_text());
            if (it != label_to_index.end()) {
                successors.push_back(it->second);
            }
        }
        if (block_index + 1 < blocks.size()) {
            successors.push_back(block_index + 1);
        }
        return successors;
    }

    if (last.get_mnemonic() == "b") {
        if (!last.get_operands().empty()) {
            const auto it = label_to_index.find(last.get_operands()[0].get_text());
            if (it != label_to_index.end()) {
                successors.push_back(it->second);
            }
        }
        return successors;
    }

    if (block_index + 1 < blocks.size()) {
        successors.push_back(block_index + 1);
    }
    return successors;
}

AArch64LivenessInfo build_liveness_info(const AArch64MachineFunction &function) {
    std::unordered_map<std::string, std::size_t> label_to_index;
    std::vector<AArch64BlockLiveness> block_info(function.get_blocks().size());
    for (std::size_t block_index = 0; block_index < function.get_blocks().size();
         ++block_index) {
        label_to_index.emplace(function.get_blocks()[block_index].get_label(),
                               block_index);
    }

    for (std::size_t block_index = 0; block_index < function.get_blocks().size();
         ++block_index) {
        const AArch64MachineBlock &block = function.get_blocks()[block_index];
        AArch64BlockLiveness &info = block_info[block_index];
        for (const AArch64MachineInstr &instruction : block.get_instructions()) {
            AArch64InstructionLiveness instruction_liveness =
                collect_instruction_defs_uses(instruction);
            for (std::size_t virtual_reg_id : instruction_liveness.uses) {
                if (info.defs.find(virtual_reg_id) == info.defs.end()) {
                    info.uses.insert(virtual_reg_id);
                }
            }
            info.defs.insert(instruction_liveness.defs.begin(),
                             instruction_liveness.defs.end());
            info.instructions.push_back(std::move(instruction_liveness));
        }
        info.successors = collect_block_successors(function, label_to_index, block_index);
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t reverse_index = block_info.size(); reverse_index > 0;
             --reverse_index) {
            AArch64BlockLiveness &info = block_info[reverse_index - 1];
            std::unordered_set<std::size_t> new_live_out;
            for (std::size_t successor : info.successors) {
                const AArch64BlockLiveness &successor_info = block_info[successor];
                new_live_out.insert(successor_info.live_in.begin(),
                                    successor_info.live_in.end());
            }
            std::unordered_set<std::size_t> new_live_in = info.uses;
            for (std::size_t vreg : new_live_out) {
                if (info.defs.find(vreg) == info.defs.end()) {
                    new_live_in.insert(vreg);
                }
            }
            if (new_live_in != info.live_in || new_live_out != info.live_out) {
                info.live_in = std::move(new_live_in);
                info.live_out = std::move(new_live_out);
                changed = true;
            }
        }
    }
    std::unordered_set<std::size_t> live_across_call;
    for (std::size_t block_index = 0; block_index < block_info.size(); ++block_index) {
        const AArch64BlockLiveness &info = block_info[block_index];
        std::unordered_set<std::size_t> live = info.live_out;
        const AArch64MachineBlock &block = function.get_blocks()[block_index];
        for (std::size_t instruction_index = info.instructions.size();
             instruction_index > 0; --instruction_index) {
            const AArch64InstructionLiveness &instruction_liveness =
                info.instructions[instruction_index - 1];
            const AArch64MachineInstr &instruction =
                block.get_instructions()[instruction_index - 1];
            if (instruction.get_flags().is_call) {
                live_across_call.insert(live.begin(), live.end());
            }
            for (std::size_t def : instruction_liveness.defs) {
                live.erase(def);
            }
            live.insert(instruction_liveness.uses.begin(),
                        instruction_liveness.uses.end());
        }
    }
    return AArch64LivenessInfo(std::move(block_info), std::move(live_across_call));
}

AArch64InterferenceGraph build_interference_graph(
    const AArch64MachineFunction &function,
    const AArch64LivenessInfo &liveness_info) {
    AArch64InterferenceGraph graph;
    for (const auto &[virtual_reg_id, _] : function.get_virtual_reg_kinds()) {
        graph.add_node(virtual_reg_id);
    }

    const auto add_bank_cliques = [&graph, &function](
                                      const std::unordered_set<std::size_t> &live_set) {
        std::unordered_set<std::size_t> general_live;
        std::unordered_set<std::size_t> float_live;
        for (std::size_t id : live_set) {
            if (AArch64VirtualReg(id, function.get_virtual_reg_kind(id)).is_floating_point()) {
                float_live.insert(id);
            } else {
                general_live.insert(id);
            }
        }
        graph.add_clique(general_live);
        graph.add_clique(float_live);
    };

    const auto &block_info = liveness_info.get_blocks();
    for (std::size_t block_index = 0; block_index < block_info.size(); ++block_index) {
        const AArch64BlockLiveness &info = block_info[block_index];
        std::unordered_set<std::size_t> live = info.live_out;
        add_bank_cliques(live);

        for (std::size_t instruction_index = info.instructions.size();
             instruction_index > 0; --instruction_index) {
            const AArch64InstructionLiveness &instruction_liveness =
                info.instructions[instruction_index - 1];
            for (std::size_t def : instruction_liveness.defs) {
                graph.add_node(def);
                for (std::size_t live_virtual_reg : live) {
                    if (AArch64VirtualReg(def, function.get_virtual_reg_kind(def))
                            .get_bank() !=
                        AArch64VirtualReg(
                            live_virtual_reg,
                            function.get_virtual_reg_kind(live_virtual_reg))
                            .get_bank()) {
                        continue;
                    }
                    graph.add_edge(def, live_virtual_reg);
                }
            }
            for (std::size_t def : instruction_liveness.defs) {
                live.erase(def);
            }
            live.insert(instruction_liveness.uses.begin(),
                        instruction_liveness.uses.end());
            add_bank_cliques(live);
        }
        add_bank_cliques(info.live_in);
    }

    return graph;
}

void add_backend_error(DiagnosticEngine &diagnostic_engine,
                       const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
}

} // namespace

bool AArch64RegisterAllocationPass::run(AArch64MachineFunction &function,
                                        DiagnosticEngine &diagnostic_engine) const {
    const AArch64LivenessInfo liveness = build_liveness_info(function);
    const AArch64InterferenceGraph graph =
        build_interference_graph(function, liveness);
    std::unordered_map<std::size_t, std::unordered_set<std::size_t>> working_graph =
        graph.get_adjacency();

    struct SimplifyNode {
        std::size_t virtual_reg_id = 0;
        std::size_t degree = 0;
        bool live_across_call = false;
    };

    std::vector<SimplifyNode> simplify_stack;
    while (!working_graph.empty()) {
        std::optional<SimplifyNode> low_degree_choice;
        std::optional<SimplifyNode> spill_choice;
        for (const auto &[virtual_reg_id, neighbors] : working_graph) {
            const AArch64VirtualRegKind kind =
                function.get_virtual_reg_kind(virtual_reg_id);
            const std::size_t color_count =
                AArch64VirtualReg(virtual_reg_id, kind).is_floating_point()
                    ? (liveness.get_live_across_call().find(virtual_reg_id) !=
                               liveness.get_live_across_call().end() &&
                           is_live_across_call_callee_saved_capable(kind)
                           ? std::size(kCalleeSavedAllocatableFloatPhysicalRegs)
                           : std::size(kCallerSavedAllocatableFloatPhysicalRegs) +
                                 std::size(kCalleeSavedAllocatableFloatPhysicalRegs))
                    : (liveness.get_live_across_call().find(virtual_reg_id) !=
                               liveness.get_live_across_call().end()
                           ? std::size(kCalleeSavedAllocatablePhysicalRegs)
                           : std::size(kCallerSavedAllocatablePhysicalRegs) +
                                 std::size(kCalleeSavedAllocatablePhysicalRegs));
            const SimplifyNode candidate{
                virtual_reg_id, neighbors.size(),
                liveness.get_live_across_call().find(virtual_reg_id) !=
                    liveness.get_live_across_call().end()};
            if (candidate.degree < color_count) {
                if (!low_degree_choice.has_value() ||
                    candidate.degree > low_degree_choice->degree ||
                    (candidate.degree == low_degree_choice->degree &&
                     candidate.virtual_reg_id < low_degree_choice->virtual_reg_id)) {
                    low_degree_choice = candidate;
                }
                continue;
            }
            if (!spill_choice.has_value() ||
                candidate.degree > spill_choice->degree ||
                (candidate.degree == spill_choice->degree &&
                 candidate.virtual_reg_id < spill_choice->virtual_reg_id)) {
                spill_choice = candidate;
            }
        }

        const SimplifyNode chosen =
            low_degree_choice.has_value() ? *low_degree_choice : *spill_choice;
        simplify_stack.push_back(chosen);

        const auto node_it = working_graph.find(chosen.virtual_reg_id);
        if (node_it == working_graph.end()) {
            continue;
        }
        const std::vector<std::size_t> neighbors(node_it->second.begin(),
                                                 node_it->second.end());
        for (std::size_t neighbor : neighbors) {
            const auto neighbor_it = working_graph.find(neighbor);
            if (neighbor_it == working_graph.end()) {
                continue;
            }
            neighbor_it->second.erase(chosen.virtual_reg_id);
        }
        working_graph.erase(node_it);
    }

    while (!simplify_stack.empty()) {
        const SimplifyNode node = simplify_stack.back();
        simplify_stack.pop_back();

        std::set<unsigned> available;
        const AArch64VirtualRegKind kind =
            function.get_virtual_reg_kind(node.virtual_reg_id);
        if (kind == AArch64VirtualRegKind::Float128) {
            const std::size_t spill_size = virtual_reg_size(kind);
            std::size_t local_size = function.get_frame_info().get_local_size();
            local_size = align_to(local_size, spill_size);
            local_size += spill_size;
            function.get_frame_info().set_virtual_reg_spill_offset(
                node.virtual_reg_id, local_size);
            function.get_frame_info().set_local_size(local_size);
            function.get_frame_info().set_frame_size(align_to(local_size, 16));
            continue;
        }
        if (AArch64VirtualReg(node.virtual_reg_id, kind).is_floating_point()) {
            if (node.live_across_call) {
                if (is_live_across_call_callee_saved_capable(kind)) {
                    available.insert(std::begin(kCalleeSavedAllocatableFloatPhysicalRegs),
                                     std::end(kCalleeSavedAllocatableFloatPhysicalRegs));
                }
            } else {
                available.insert(std::begin(kCallerSavedAllocatableFloatPhysicalRegs),
                                 std::end(kCallerSavedAllocatableFloatPhysicalRegs));
                available.insert(std::begin(kCalleeSavedAllocatableFloatPhysicalRegs),
                                 std::end(kCalleeSavedAllocatableFloatPhysicalRegs));
            }
        } else if (node.live_across_call) {
            available.insert(std::begin(kCalleeSavedAllocatablePhysicalRegs),
                             std::end(kCalleeSavedAllocatablePhysicalRegs));
        } else {
            available.insert(std::begin(kCallerSavedAllocatablePhysicalRegs),
                             std::end(kCallerSavedAllocatablePhysicalRegs));
            available.insert(std::begin(kCalleeSavedAllocatablePhysicalRegs),
                             std::end(kCalleeSavedAllocatablePhysicalRegs));
        }
        for (std::size_t neighbor : graph.get_neighbors(node.virtual_reg_id)) {
            const std::optional<unsigned> neighbor_reg =
                function.get_physical_reg_for_virtual(neighbor);
            if (neighbor_reg.has_value()) {
                available.erase(*neighbor_reg);
            }
        }

        if (!available.empty()) {
            const unsigned chosen_reg = *available.begin();
            function.set_virtual_reg_allocation(node.virtual_reg_id, chosen_reg);
            if (is_callee_saved_allocatable_physical_reg(chosen_reg) ||
                is_callee_saved_allocatable_float_physical_reg(chosen_reg)) {
                function.get_frame_info().mark_saved_physical_reg(chosen_reg);
            }
            continue;
        }

        const std::size_t spill_size =
            virtual_reg_size(function.get_virtual_reg_kind(node.virtual_reg_id));
        const std::size_t spill_alignment = spill_size;
        std::size_t local_size = function.get_frame_info().get_local_size();
        local_size = align_to(local_size, spill_alignment);
        local_size += spill_size;
        function.get_frame_info().set_virtual_reg_spill_offset(node.virtual_reg_id,
                                                               local_size);
        function.get_frame_info().set_local_size(local_size);
        function.get_frame_info().set_frame_size(align_to(local_size, 16));
    }

    return true;
}

} // namespace sysycc
