#include "backend/asm_gen/aarch64/passes/aarch64_post_ra_peephole_pass.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <optional>
#include <limits>
#include <string_view>

#include "backend/asm_gen/aarch64/model/aarch64_target_constraints.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

namespace sysycc {

namespace {

struct PlainCopyInfo {
    AArch64VirtualReg dst;
    AArch64VirtualReg src;
};

struct PlainPhysicalCopyInfo {
    unsigned dst_reg = 0;
    unsigned src_reg = 0;
    AArch64RegBank bank = AArch64RegBank::General;
    AArch64VirtualRegKind reg_kind = AArch64VirtualRegKind::General64;
};

struct MultiplyInfo {
    AArch64VirtualReg dst;
    AArch64VirtualReg lhs;
    AArch64VirtualReg rhs;
};

struct MoveWideZeroInfo {
    AArch64VirtualReg dst;
};

struct AccumulateAddInfo {
    AArch64VirtualReg dst;
    AArch64VirtualReg addend;
    AArch64VirtualReg accumulation;
};

struct PlainAddInfo {
    AArch64VirtualReg dst;
    AArch64VirtualReg lhs;
    AArch64VirtualReg rhs;
};

struct AssignedVectorRegInfo {
    unsigned reg_number = 0;
    unsigned lane_count = 0;
    char element_kind = 'b';
    std::optional<unsigned> lane_index;
    bool is_def = false;
};

struct FrameSlotVirtualLoadInfo {
    AArch64VirtualReg dst;
    long long offset = 0;
};

struct FrameSlotPhysicalLoadInfo {
    unsigned dst_reg = 0;
    AArch64VirtualRegKind kind = AArch64VirtualRegKind::General32;
    long long offset = 0;
};

struct FrameSlotLoadCandidate {
    enum class ValueKind : unsigned char {
        Virtual,
        Physical,
    };

    ValueKind value_kind = ValueKind::Virtual;
    AArch64VirtualReg virtual_dst;
    unsigned physical_dst = 0;
    AArch64VirtualRegKind reg_kind = AArch64VirtualRegKind::General32;
    long long offset = 0;
};

struct BlockTerminatorInfo {
    std::size_t first_terminator_index = 0;
    std::optional<std::string> conditional_target;
    std::optional<std::string> unconditional_target;
};

bool is_general_kind(AArch64VirtualRegKind kind) {
    return kind == AArch64VirtualRegKind::General32 ||
           kind == AArch64VirtualRegKind::General64;
}

bool is_conditional_branch_instruction(const AArch64MachineInstr &instruction) {
    if (instruction.get_opcode() == AArch64MachineOpcode::BranchConditional ||
        instruction.get_opcode() == AArch64MachineOpcode::CompareBranchZero ||
        instruction.get_opcode() == AArch64MachineOpcode::CompareBranchNonZero) {
        return true;
    }
    const std::string &mnemonic = instruction.get_mnemonic();
    return mnemonic.size() > 2 && mnemonic[0] == 'b' && mnemonic[1] == '.';
}

bool same_virtual_reg(const AArch64VirtualReg &lhs, const AArch64VirtualReg &rhs) {
    return lhs.get_id() == rhs.get_id() && lhs.get_kind() == rhs.get_kind();
}

std::unordered_map<std::size_t, std::size_t>
collect_virtual_use_counts(const AArch64MachineFunction &function);

bool extract_plain_virtual_copy(const AArch64MachineInstr &instruction,
                                PlainCopyInfo &copy) {
    const std::string &mnemonic = instruction.get_mnemonic();
    if ((mnemonic != "mov" && mnemonic != "fmov") ||
        instruction.get_operands().size() != 2) {
        return false;
    }
    const auto *dst_operand = instruction.get_operands()[0].get_virtual_reg_operand();
    const auto *src_operand = instruction.get_operands()[1].get_virtual_reg_operand();
    if (dst_operand == nullptr || src_operand == nullptr || !dst_operand->is_def ||
        src_operand->is_def) {
        return false;
    }

    const AArch64VirtualReg &dst = dst_operand->reg;
    const AArch64VirtualReg &src = src_operand->reg;
    if (!dst.is_valid() || !src.is_valid() || dst.get_kind() != src.get_kind()) {
        return false;
    }
    if (mnemonic == "mov" && !dst.is_general()) {
        return false;
    }
    if (mnemonic == "fmov" && !dst.is_floating_point()) {
        return false;
    }

    copy = PlainCopyInfo{dst, src};
    return true;
}

bool extract_plain_physical_copy(const AArch64MachineInstr &instruction,
                                 PlainPhysicalCopyInfo &copy) {
    const std::string &mnemonic = instruction.get_mnemonic();
    if ((mnemonic != "mov" && mnemonic != "fmov") ||
        instruction.get_operands().size() != 2) {
        return false;
    }
    const auto *dst_operand = instruction.get_operands()[0].get_physical_reg_operand();
    const auto *src_operand = instruction.get_operands()[1].get_physical_reg_operand();
    if (dst_operand == nullptr || src_operand == nullptr ||
        dst_operand->kind != src_operand->kind) {
        return false;
    }

    const AArch64RegBank bank =
        is_general_kind(dst_operand->kind) ? AArch64RegBank::General
                                           : AArch64RegBank::FloatingPoint;
    if ((mnemonic == "mov" && bank != AArch64RegBank::General) ||
        (mnemonic == "fmov" && bank != AArch64RegBank::FloatingPoint)) {
        return false;
    }

    copy = PlainPhysicalCopyInfo{dst_operand->reg_number, src_operand->reg_number, bank,
                                 dst_operand->kind};
    return true;
}

std::optional<PlainPhysicalCopyInfo>
extract_assigned_plain_copy(const AArch64MachineFunction &function,
                            const AArch64MachineInstr &instruction) {
    const std::string &mnemonic = instruction.get_mnemonic();
    if ((mnemonic != "mov" && mnemonic != "fmov") ||
        instruction.get_operands().size() != 2) {
        return std::nullopt;
    }

    const auto resolve_operand =
        [&function](const AArch64MachineOperand &operand,
                    bool expect_def) -> std::optional<PlainPhysicalCopyInfo> {
        if (const auto *physical = operand.get_physical_reg_operand();
            physical != nullptr) {
            const AArch64RegBank bank =
                is_general_kind(physical->kind) ? AArch64RegBank::General
                                                : AArch64RegBank::FloatingPoint;
            return PlainPhysicalCopyInfo{physical->reg_number, physical->reg_number,
                                         bank, physical->kind};
        }
        const auto *virtual_reg = operand.get_virtual_reg_operand();
        if (virtual_reg == nullptr || virtual_reg->is_def != expect_def) {
            return std::nullopt;
        }
        const auto assigned =
            function.get_physical_reg_for_virtual(virtual_reg->reg.get_id());
        if (!assigned.has_value()) {
            return std::nullopt;
        }
        const AArch64RegBank bank =
            virtual_reg->reg.is_general() ? AArch64RegBank::General
                                          : AArch64RegBank::FloatingPoint;
        return PlainPhysicalCopyInfo{*assigned, *assigned, bank,
                                     virtual_reg->reg.get_kind()};
    };

    const auto dst = resolve_operand(instruction.get_operands()[0], true);
    const auto src = resolve_operand(instruction.get_operands()[1], false);
    if (!dst.has_value() || !src.has_value() || dst->bank != src->bank) {
        return std::nullopt;
    }
    if ((mnemonic == "mov" && dst->bank != AArch64RegBank::General) ||
        (mnemonic == "fmov" && dst->bank != AArch64RegBank::FloatingPoint)) {
        return std::nullopt;
    }
    return PlainPhysicalCopyInfo{dst->dst_reg, src->dst_reg, dst->bank,
                                 dst->reg_kind};
}

std::optional<AssignedVectorRegInfo>
resolve_assigned_vector_reg(const AArch64MachineFunction &function,
                            const AArch64MachineOperand &operand) {
    const auto *vector = operand.get_vector_reg_operand();
    if (vector == nullptr) {
        return std::nullopt;
    }

    unsigned physical_reg = vector->physical_reg;
    if (vector->base_kind == AArch64MachineVectorRegOperand::BaseKind::VirtualReg) {
        const auto assigned =
            function.get_physical_reg_for_virtual(vector->reg.get_id());
        if (!assigned.has_value()) {
            return std::nullopt;
        }
        physical_reg = *assigned;
    }

    return AssignedVectorRegInfo{physical_reg, vector->lane_count,
                                 vector->element_kind, vector->lane_index,
                                 vector->is_def};
}

bool same_vector_location(const AssignedVectorRegInfo &lhs,
                          const AssignedVectorRegInfo &rhs) {
    return lhs.reg_number == rhs.reg_number && lhs.lane_count == rhs.lane_count &&
           lhs.element_kind == rhs.element_kind &&
           lhs.lane_index == rhs.lane_index;
}

bool is_redundant_vector_copy(const AArch64MachineFunction &function,
                              const AArch64MachineInstr &instruction) {
    if (instruction.get_mnemonic() != "mov" ||
        instruction.get_operands().size() != 2) {
        return false;
    }

    const auto dst = resolve_assigned_vector_reg(function, instruction.get_operands()[0]);
    const auto src = resolve_assigned_vector_reg(function, instruction.get_operands()[1]);
    if (!dst.has_value() || !src.has_value() || !dst->is_def || src->is_def) {
        return false;
    }
    return same_vector_location(*dst, *src);
}

bool extract_full_vector_copy(const AArch64MachineFunction &function,
                              const AArch64MachineInstr &instruction,
                              AssignedVectorRegInfo &dst,
                              AssignedVectorRegInfo &src) {
    if (instruction.get_mnemonic() != "mov" ||
        instruction.get_operands().size() != 2) {
        return false;
    }
    const auto resolved_dst =
        resolve_assigned_vector_reg(function, instruction.get_operands()[0]);
    const auto resolved_src =
        resolve_assigned_vector_reg(function, instruction.get_operands()[1]);
    if (!resolved_dst.has_value() || !resolved_src.has_value() ||
        !resolved_dst->is_def || resolved_src->is_def ||
        resolved_dst->lane_index.has_value() ||
        resolved_src->lane_index.has_value() ||
        resolved_dst->lane_count != resolved_src->lane_count ||
        resolved_dst->element_kind != resolved_src->element_kind) {
        return false;
    }
    dst = *resolved_dst;
    src = *resolved_src;
    return true;
}

bool is_phi_edge_block_label(const std::string &label) {
    return label.size() >= 4 &&
           label.compare(label.size() - 4, 4, "_phi") == 0;
}

bool is_simple_edge_copy_instruction(const AArch64MachineInstr &instruction) {
    const std::string &mnemonic = instruction.get_mnemonic();
    return (mnemonic == "mov" || mnemonic == "fmov") &&
           instruction.get_operands().size() == 2;
}

bool same_physical_copy_location(const PlainPhysicalCopyInfo &lhs,
                                 const PlainPhysicalCopyInfo &rhs) {
    return lhs.dst_reg == rhs.dst_reg && lhs.src_reg == rhs.src_reg &&
           lhs.bank == rhs.bank && lhs.reg_kind == rhs.reg_kind;
}

bool reschedule_phi_edge_copy_run(std::vector<AArch64MachineInstr> &instructions,
                                  const AArch64MachineFunction &function,
                                  std::size_t begin_index,
                                  std::size_t end_index) {
    struct PendingCopy {
        AArch64MachineInstr instruction;
        PlainPhysicalCopyInfo info;
    };

    std::vector<PendingCopy> pending;
    pending.reserve(end_index - begin_index);
    for (std::size_t index = begin_index; index < end_index; ++index) {
        const auto copy = extract_assigned_plain_copy(function, instructions[index]);
        if (!copy.has_value()) {
            return false;
        }
        if (copy->dst_reg == copy->src_reg) {
            continue;
        }
        pending.push_back(PendingCopy{instructions[index], *copy});
    }

    std::vector<PlainPhysicalCopyInfo> original_copies;
    original_copies.reserve(end_index - begin_index);
    for (std::size_t index = begin_index; index < end_index; ++index) {
        const auto copy = extract_assigned_plain_copy(function, instructions[index]);
        if (copy.has_value() && copy->dst_reg != copy->src_reg) {
            original_copies.push_back(*copy);
        }
    }

    if (pending.size() < 2) {
        if (pending.size() == original_copies.size()) {
            bool changed = false;
            for (std::size_t index = 0; index < pending.size(); ++index) {
                if (!same_physical_copy_location(pending[index].info,
                                                 original_copies[index])) {
                    changed = true;
                    break;
                }
            }
            if (!changed) {
                return false;
            }
        }
        std::vector<AArch64MachineInstr> replacement;
        replacement.reserve(pending.size());
        for (PendingCopy &copy : pending) {
            replacement.push_back(std::move(copy.instruction));
        }
        instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(begin_index),
                           instructions.begin() +
                               static_cast<std::ptrdiff_t>(end_index));
        instructions.insert(
            instructions.begin() + static_cast<std::ptrdiff_t>(begin_index),
            std::make_move_iterator(replacement.begin()),
            std::make_move_iterator(replacement.end()));
        return true;
    }

    std::vector<AArch64MachineInstr> scheduled;
    scheduled.reserve(pending.size());
    while (!pending.empty()) {
        bool progressed = false;
        for (auto it = pending.begin(); it != pending.end(); ++it) {
            bool blocked = false;
            for (const PendingCopy &other : pending) {
                if (&other == &*it) {
                    continue;
                }
                if (other.info.bank == it->info.bank &&
                    other.info.src_reg == it->info.dst_reg) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) {
                continue;
            }
            scheduled.push_back(std::move(it->instruction));
            pending.erase(it);
            progressed = true;
            break;
        }
        if (!progressed) {
            return false;
        }
    }

    bool changed = scheduled.size() != original_copies.size();
    for (std::size_t index = 0; !changed && index < scheduled.size(); ++index) {
        const auto scheduled_copy =
            extract_assigned_plain_copy(function, scheduled[index]);
        if (!scheduled_copy.has_value() ||
            !same_physical_copy_location(*scheduled_copy,
                                         original_copies[index])) {
            changed = true;
        }
    }
    if (!changed) {
        return false;
    }

    instructions.erase(instructions.begin() +
                           static_cast<std::ptrdiff_t>(begin_index),
                       instructions.begin() +
                           static_cast<std::ptrdiff_t>(end_index));
    instructions.insert(
        instructions.begin() + static_cast<std::ptrdiff_t>(begin_index),
        std::make_move_iterator(scheduled.begin()),
        std::make_move_iterator(scheduled.end()));
    return true;
}

bool repair_parallel_phi_edge_copies(AArch64MachineFunction &function) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        if (!is_phi_edge_block_label(block.get_label())) {
            continue;
        }
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t index = 0; index < instructions.size();) {
            PlainPhysicalCopyInfo copy;
            if (!extract_assigned_plain_copy(function, instructions[index]).has_value()) {
                ++index;
                continue;
            }
            const std::size_t run_begin = index;
            while (index < instructions.size() &&
                   extract_assigned_plain_copy(function, instructions[index])
                       .has_value()) {
                ++index;
            }
            if (reschedule_phi_edge_copy_run(instructions, function, run_begin,
                                             index)) {
                return true;
            }
            index = run_begin + 1;
        }
    }
    return false;
}

bool extract_integer_multiply(const AArch64MachineInstr &instruction,
                              MultiplyInfo &info) {
    if (instruction.get_opcode() != AArch64MachineOpcode::Mul ||
        instruction.get_operands().size() != 3) {
        return false;
    }
    const auto *dst_operand = instruction.get_operands()[0].get_virtual_reg_operand();
    const auto *lhs_operand = instruction.get_operands()[1].get_virtual_reg_operand();
    const auto *rhs_operand = instruction.get_operands()[2].get_virtual_reg_operand();
    if (dst_operand == nullptr || lhs_operand == nullptr || rhs_operand == nullptr ||
        !dst_operand->is_def || lhs_operand->is_def || rhs_operand->is_def ||
        !dst_operand->reg.is_general() || !lhs_operand->reg.is_general() ||
        !rhs_operand->reg.is_general()) {
        return false;
    }
    info = MultiplyInfo{dst_operand->reg, lhs_operand->reg, rhs_operand->reg};
    return true;
}

bool extract_move_wide_zero(const AArch64MachineInstr &instruction,
                            MoveWideZeroInfo &info) {
    if (instruction.get_opcode() != AArch64MachineOpcode::MoveWideZero ||
        instruction.get_operands().empty()) {
        return false;
    }
    const auto *dst_operand = instruction.get_operands()[0].get_virtual_reg_operand();
    if (dst_operand == nullptr || !dst_operand->is_def || !dst_operand->reg.is_general()) {
        return false;
    }
    info = MoveWideZeroInfo{dst_operand->reg};
    return true;
}

bool extract_accumulate_add(const AArch64MachineInstr &instruction,
                            AccumulateAddInfo &info) {
    if (instruction.get_opcode() != AArch64MachineOpcode::Add ||
        instruction.get_operands().size() != 3) {
        return false;
    }
    const auto *dst_operand = instruction.get_operands()[0].get_virtual_reg_operand();
    const auto *lhs_operand = instruction.get_operands()[1].get_virtual_reg_operand();
    const auto *rhs_operand = instruction.get_operands()[2].get_virtual_reg_operand();
    if (dst_operand == nullptr || lhs_operand == nullptr || rhs_operand == nullptr ||
        !dst_operand->is_def || lhs_operand->is_def || rhs_operand->is_def ||
        !dst_operand->reg.is_general() || !lhs_operand->reg.is_general() ||
        !rhs_operand->reg.is_general()) {
        return false;
    }
    if (!same_virtual_reg(dst_operand->reg, lhs_operand->reg)) {
        return false;
    }
    info = AccumulateAddInfo{dst_operand->reg, lhs_operand->reg, rhs_operand->reg};
    return true;
}

bool extract_plain_add(const AArch64MachineInstr &instruction, PlainAddInfo &info) {
    if (instruction.get_opcode() != AArch64MachineOpcode::Add ||
        instruction.get_operands().size() != 3) {
        return false;
    }
    const auto *dst_operand = instruction.get_operands()[0].get_virtual_reg_operand();
    const auto *lhs_operand = instruction.get_operands()[1].get_virtual_reg_operand();
    const auto *rhs_operand = instruction.get_operands()[2].get_virtual_reg_operand();
    if (dst_operand == nullptr || lhs_operand == nullptr || rhs_operand == nullptr ||
        !dst_operand->is_def || lhs_operand->is_def || rhs_operand->is_def ||
        !dst_operand->reg.is_general() || !lhs_operand->reg.is_general() ||
        !rhs_operand->reg.is_general()) {
        return false;
    }
    info = PlainAddInfo{dst_operand->reg, lhs_operand->reg, rhs_operand->reg};
    return true;
}

bool extract_frame_slot_virtual_load(const AArch64MachineInstr &instruction,
                                     FrameSlotVirtualLoadInfo &info) {
    const auto is_load_mnemonic = [&instruction]() {
        const std::string &mnemonic = instruction.get_mnemonic();
        return mnemonic == "ldr" || mnemonic == "ldur" || mnemonic == "ldrb" ||
               mnemonic == "ldurb" || mnemonic == "ldrh" || mnemonic == "ldurh";
    };
    switch (instruction.get_opcode()) {
    case AArch64MachineOpcode::Load:
    case AArch64MachineOpcode::LoadByte:
    case AArch64MachineOpcode::LoadHalf:
    case AArch64MachineOpcode::LoadUnscaled:
    case AArch64MachineOpcode::LoadByteUnscaled:
    case AArch64MachineOpcode::LoadHalfUnscaled:
        break;
    default:
        if (!is_load_mnemonic()) {
            return false;
        }
        break;
    }
    if (instruction.get_operands().size() != 2) {
        return false;
    }
    const auto *dst_operand = instruction.get_operands()[0].get_virtual_reg_operand();
    const auto *memory = instruction.get_operands()[1].get_memory_address_operand();
    if (dst_operand == nullptr || memory == nullptr ||
        !dst_operand->is_def || !dst_operand->reg.is_general() ||
        memory->base_kind !=
            AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg ||
        memory->physical_reg != static_cast<unsigned>(AArch64PhysicalReg::X29)) {
        return false;
    }
    const auto immediate_offset = memory->get_immediate_offset();
    if (!immediate_offset.has_value()) {
        return false;
    }
    info = FrameSlotVirtualLoadInfo{dst_operand->reg, *immediate_offset};
    return true;
}

bool extract_frame_slot_physical_load(const AArch64MachineInstr &instruction,
                                      FrameSlotPhysicalLoadInfo &info) {
    const auto is_load_mnemonic = [&instruction]() {
        const std::string &mnemonic = instruction.get_mnemonic();
        return mnemonic == "ldr" || mnemonic == "ldur" || mnemonic == "ldrb" ||
               mnemonic == "ldurb" || mnemonic == "ldrh" || mnemonic == "ldurh";
    };
    switch (instruction.get_opcode()) {
    case AArch64MachineOpcode::Load:
    case AArch64MachineOpcode::LoadByte:
    case AArch64MachineOpcode::LoadHalf:
    case AArch64MachineOpcode::LoadUnscaled:
    case AArch64MachineOpcode::LoadByteUnscaled:
    case AArch64MachineOpcode::LoadHalfUnscaled:
        break;
    default:
        if (!is_load_mnemonic()) {
            return false;
        }
        break;
    }
    if (instruction.get_operands().size() != 2) {
        return false;
    }
    const auto *dst_operand = instruction.get_operands()[0].get_physical_reg_operand();
    const auto *memory = instruction.get_operands()[1].get_memory_address_operand();
    if (dst_operand == nullptr || memory == nullptr ||
        !is_general_kind(dst_operand->kind) ||
        memory->base_kind !=
            AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg ||
        memory->physical_reg != static_cast<unsigned>(AArch64PhysicalReg::X29)) {
        return false;
    }
    const auto immediate_offset = memory->get_immediate_offset();
    if (!immediate_offset.has_value()) {
        return false;
    }
    info = FrameSlotPhysicalLoadInfo{dst_operand->reg_number, dst_operand->kind,
                                     *immediate_offset};
    return true;
}

bool extract_frame_slot_load_candidate(const AArch64MachineInstr &instruction,
                                       FrameSlotLoadCandidate &candidate) {
    FrameSlotVirtualLoadInfo virtual_load;
    if (extract_frame_slot_virtual_load(instruction, virtual_load)) {
        candidate = FrameSlotLoadCandidate{
            .value_kind = FrameSlotLoadCandidate::ValueKind::Virtual,
            .virtual_dst = virtual_load.dst,
            .reg_kind = virtual_load.dst.get_kind(),
            .offset = virtual_load.offset,
        };
        return true;
    }

    FrameSlotPhysicalLoadInfo physical_load;
    if (extract_frame_slot_physical_load(instruction, physical_load)) {
        candidate = FrameSlotLoadCandidate{
            .value_kind = FrameSlotLoadCandidate::ValueKind::Physical,
            .physical_dst = physical_load.dst_reg,
            .reg_kind = physical_load.kind,
            .offset = physical_load.offset,
        };
        return true;
    }

    return false;
}

bool uses_virtual_reg(const AArch64MachineOperand &operand,
                      const AArch64VirtualReg &reg) {
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        return !virtual_reg->is_def && same_virtual_reg(virtual_reg->reg, reg);
    }
    if (const auto *memory = operand.get_memory_address_operand();
        memory != nullptr &&
        memory->base_kind ==
            AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg) {
        return same_virtual_reg(memory->virtual_reg, reg);
    }
    return false;
}

bool defines_virtual_reg(const AArch64MachineInstr &instruction,
                         const AArch64VirtualReg &reg) {
    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        const auto *virtual_reg = operand.get_virtual_reg_operand();
        if (virtual_reg == nullptr || !virtual_reg->is_def) {
            continue;
        }
        if (same_virtual_reg(virtual_reg->reg, reg)) {
            return true;
        }
    }
    return false;
}

std::size_t count_instruction_uses(const AArch64MachineInstr &instruction,
                                   const AArch64VirtualReg &reg) {
    std::size_t count = 0;
    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        if (uses_virtual_reg(operand, reg)) {
            ++count;
        }
    }
    return count;
}

bool instruction_touches_virtual_reg(const AArch64MachineInstr &instruction,
                                     const AArch64VirtualReg &reg) {
    return defines_virtual_reg(instruction, reg) ||
           count_instruction_uses(instruction, reg) != 0;
}

std::size_t count_instruction_uses_physical_reg(const AArch64MachineInstr &instruction,
                                                unsigned reg_number,
                                                AArch64RegBank bank) {
    std::size_t count = 0;
    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        if (const auto *physical = operand.get_physical_reg_operand();
            physical != nullptr && physical->reg_number == reg_number &&
            ((bank == AArch64RegBank::General && is_general_kind(physical->kind)) ||
             (bank == AArch64RegBank::FloatingPoint &&
              !is_general_kind(physical->kind)))) {
            ++count;
            continue;
        }
        if (const auto *memory = operand.get_memory_address_operand();
            memory != nullptr &&
            memory->base_kind ==
                AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg &&
            memory->physical_reg == reg_number && bank == AArch64RegBank::General) {
            ++count;
            continue;
        }
        if (const auto *memory = operand.get_memory_address_operand();
            memory != nullptr && bank == AArch64RegBank::General) {
            const auto *register_offset = memory->get_register_offset();
            if (register_offset != nullptr &&
                register_offset->reg_number == reg_number) {
                ++count;
            }
        }
    }
    return count;
}

bool physical_operand_index_is_explicit_def(const AArch64MachineInstr &instruction,
                                            std::size_t index) {
    switch (instruction.get_opcode()) {
    case AArch64MachineOpcode::MoveWideZero:
    case AArch64MachineOpcode::MoveWideKeep:
    case AArch64MachineOpcode::Move:
    case AArch64MachineOpcode::Add:
    case AArch64MachineOpcode::Sub:
    case AArch64MachineOpcode::And:
    case AArch64MachineOpcode::Orr:
    case AArch64MachineOpcode::Eor:
    case AArch64MachineOpcode::Mul:
    case AArch64MachineOpcode::MultiplyAdd:
    case AArch64MachineOpcode::MultiplySubtract:
    case AArch64MachineOpcode::SignedDiv:
    case AArch64MachineOpcode::UnsignedDiv:
    case AArch64MachineOpcode::ShiftLeft:
    case AArch64MachineOpcode::ShiftRightLogical:
    case AArch64MachineOpcode::ShiftRightArithmetic:
    case AArch64MachineOpcode::ConditionalSelect:
    case AArch64MachineOpcode::ConditionalSet:
    case AArch64MachineOpcode::Adrp:
    case AArch64MachineOpcode::Load:
    case AArch64MachineOpcode::LoadByte:
    case AArch64MachineOpcode::LoadHalf:
    case AArch64MachineOpcode::LoadUnscaled:
    case AArch64MachineOpcode::LoadByteUnscaled:
    case AArch64MachineOpcode::LoadHalfUnscaled:
    case AArch64MachineOpcode::FloatMove:
    case AArch64MachineOpcode::FloatAdd:
    case AArch64MachineOpcode::FloatSub:
    case AArch64MachineOpcode::FloatMul:
    case AArch64MachineOpcode::FloatMulAdd:
    case AArch64MachineOpcode::FloatDiv:
    case AArch64MachineOpcode::SignedIntToFloat:
    case AArch64MachineOpcode::UnsignedIntToFloat:
    case AArch64MachineOpcode::FloatToSignedInt:
    case AArch64MachineOpcode::FloatToUnsignedInt:
    case AArch64MachineOpcode::FloatConvert:
        return index == 0;
    case AArch64MachineOpcode::LoadPair:
        return index < 2;
    default:
        break;
    }

    const std::string &mnemonic = instruction.get_mnemonic();
    return (mnemonic == "mov" || mnemonic == "fmov" || mnemonic == "movz" ||
            mnemonic == "movk" || mnemonic == "madd" || mnemonic == "msub" ||
            mnemonic == "smaddl" || mnemonic == "umaddl" || mnemonic == "csel" ||
            mnemonic == "cset" || mnemonic == "adrp" || mnemonic == "uxtw" ||
            mnemonic == "sxtw") &&
           index == 0;
}

std::size_t count_instruction_source_uses_physical_reg(
    const AArch64MachineInstr &instruction, unsigned reg_number, AArch64RegBank bank) {
    std::size_t count = 0;
    const std::vector<AArch64MachineOperand> &operands = instruction.get_operands();
    for (std::size_t index = 0; index < operands.size(); ++index) {
        const AArch64MachineOperand &operand = operands[index];
        if (const auto *physical = operand.get_physical_reg_operand();
            physical != nullptr && physical->reg_number == reg_number &&
            ((bank == AArch64RegBank::General && is_general_kind(physical->kind)) ||
             (bank == AArch64RegBank::FloatingPoint &&
              !is_general_kind(physical->kind))) &&
            !physical_operand_index_is_explicit_def(instruction, index)) {
            ++count;
            continue;
        }
        if (const auto *memory = operand.get_memory_address_operand();
            memory != nullptr &&
            memory->base_kind ==
                AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg &&
            memory->physical_reg == reg_number && bank == AArch64RegBank::General) {
            ++count;
            continue;
        }
        if (const auto *memory = operand.get_memory_address_operand();
            memory != nullptr && bank == AArch64RegBank::General) {
            const auto *register_offset = memory->get_register_offset();
            if (register_offset != nullptr &&
                register_offset->reg_number == reg_number) {
                ++count;
            }
        }
    }
    return count;
}

bool instruction_explicitly_defines_physical_reg(const AArch64MachineInstr &instruction,
                                                 unsigned reg_number,
                                                 AArch64RegBank bank) {
    const std::vector<AArch64MachineOperand> &operands = instruction.get_operands();
    for (std::size_t index = 0; index < operands.size(); ++index) {
        const auto *physical = operands[index].get_physical_reg_operand();
        if (physical == nullptr || physical->reg_number != reg_number ||
            !physical_operand_index_is_explicit_def(instruction, index)) {
            continue;
        }
        if ((bank == AArch64RegBank::General && is_general_kind(physical->kind)) ||
            (bank == AArch64RegBank::FloatingPoint &&
             !is_general_kind(physical->kind))) {
            return true;
        }
    }
    return false;
}

bool instruction_mentions_physical_reg(const AArch64MachineInstr &instruction,
                                       unsigned reg_number, AArch64RegBank bank) {
    return instruction_explicitly_defines_physical_reg(instruction, reg_number, bank) ||
           count_instruction_source_uses_physical_reg(instruction, reg_number, bank) != 0;
}

bool operands_render_equal(const AArch64MachineInstr &lhs,
                           const AArch64MachineInstr &rhs,
                           const AArch64MachineFunction &function) {
    if (lhs.get_operands().size() != rhs.get_operands().size()) {
        return false;
    }
    for (std::size_t index = 0; index < lhs.get_operands().size(); ++index) {
        if (render_machine_operand_for_asm(lhs.get_operands()[index], function) !=
            render_machine_operand_for_asm(rhs.get_operands()[index], function)) {
            return false;
        }
    }
    return true;
}

bool is_redundant_vector_setup_candidate(const AArch64MachineInstr &instruction) {
    if ((instruction.get_mnemonic() != "dup" &&
         instruction.get_mnemonic() != "movi") ||
        instruction.get_operands().empty()) {
        return false;
    }
    const auto *immediate = instruction.get_operands().front().get_immediate_operand();
    return immediate != nullptr && !immediate->asm_text.empty() &&
           immediate->asm_text.front() == 'v';
}

bool is_store_using_v0(const AArch64MachineInstr &instruction) {
    if (instruction.get_mnemonic() != "str" || instruction.get_operands().size() != 2) {
        return false;
    }
    const auto *physical = instruction.get_operands()[0].get_physical_reg_operand();
    return physical != nullptr &&
           physical->reg_number == static_cast<unsigned>(AArch64PhysicalReg::V0);
}

struct FrameTempAddressSetupInfo {
    unsigned temp_reg = 0;
    long long frame_offset = 0;
};

bool instruction_has_dynamic_stack_pointer_usage(
    const AArch64MachineInstr &instruction) {
    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        if (operand.get_stack_pointer_operand() != nullptr) {
            return true;
        }
        const auto *memory = operand.get_memory_address_operand();
        if (memory != nullptr &&
            memory->base_kind ==
                AArch64MachineMemoryAddressOperand::BaseKind::StackPointer &&
            memory->address_mode !=
                AArch64MachineMemoryAddressOperand::AddressMode::Offset) {
            return true;
        }
    }
    return false;
}

bool block_has_dynamic_stack_pointer_usage(const AArch64MachineBlock &block) {
    for (const AArch64MachineInstr &instruction : block.get_instructions()) {
        if (instruction_has_dynamic_stack_pointer_usage(instruction)) {
            return true;
        }
    }
    return false;
}

std::size_t projected_frame_size(const AArch64MachineFunction &function) {
    const auto align_to = [](std::size_t value, std::size_t alignment) {
        if (alignment == 0) {
            return value;
        }
        const std::size_t remainder = value % alignment;
        return remainder == 0 ? value : value + (alignment - remainder);
    };

    std::size_t local_size = function.get_frame_info().get_local_size();
    for (unsigned reg : function.get_frame_info().get_saved_physical_regs()) {
        if (function.get_frame_info().has_saved_physical_reg_offset(reg)) {
            continue;
        }
        local_size = align_to(local_size, 8);
        local_size += 8;
    }
    return align_to(local_size, 16);
}

std::optional<long long> parse_hash_immediate(const AArch64MachineOperand &operand) {
    const auto *immediate = operand.get_immediate_operand();
    if (immediate == nullptr || immediate->asm_text.empty() ||
        immediate->asm_text.front() != '#') {
        return std::nullopt;
    }
    try {
        std::size_t parsed = 0;
        const long long value =
            std::stoll(immediate->asm_text.substr(1), &parsed, 10);
        if (parsed + 1 != immediate->asm_text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

struct AssignedGeneralMachineRegInfo {
    unsigned reg_number = 0;
    AArch64VirtualRegKind reg_kind = AArch64VirtualRegKind::General64;
};

struct IndexedAddressAddInfo {
    AArch64VirtualReg temp_virtual;
    unsigned temp_reg = 0;
    unsigned base_reg = 0;
    unsigned index_reg = 0;
    unsigned shift_amount = 0;
};

std::optional<AssignedGeneralMachineRegInfo>
resolve_assigned_general_machine_reg(const AArch64MachineFunction &function,
                                     const AArch64MachineOperand &operand) {
    if (const auto *physical = operand.get_physical_reg_operand();
        physical != nullptr && is_general_kind(physical->kind)) {
        return AssignedGeneralMachineRegInfo{physical->reg_number, physical->kind};
    }

    const auto *virtual_reg = operand.get_virtual_reg_operand();
    if (virtual_reg == nullptr || !virtual_reg->reg.is_general()) {
        return std::nullopt;
    }
    const auto physical_reg =
        function.get_physical_reg_for_virtual(virtual_reg->reg.get_id());
    if (!physical_reg.has_value()) {
        return std::nullopt;
    }
    return AssignedGeneralMachineRegInfo{*physical_reg, virtual_reg->reg.get_kind()};
}

bool extract_frame_temp_address_setup(const AArch64MachineInstr &instruction,
                                      FrameTempAddressSetupInfo &info) {
    if (instruction.get_opcode() != AArch64MachineOpcode::Sub ||
        instruction.get_operands().size() != 3) {
        return false;
    }
    const auto *dst = instruction.get_operands()[0].get_physical_reg_operand();
    const auto *base = instruction.get_operands()[1].get_physical_reg_operand();
    const std::optional<long long> offset =
        parse_hash_immediate(instruction.get_operands()[2]);
    if (dst == nullptr || base == nullptr || !offset.has_value() ||
        dst->reg_number == static_cast<unsigned>(AArch64PhysicalReg::X29) ||
        dst->kind != AArch64VirtualRegKind::General64 ||
        base->reg_number != static_cast<unsigned>(AArch64PhysicalReg::X29) ||
        base->kind != AArch64VirtualRegKind::General64 || *offset <= 255) {
        return false;
    }
    info = FrameTempAddressSetupInfo{dst->reg_number, *offset};
    return true;
}

std::optional<std::size_t>
memory_operand_index_for_base(const AArch64MachineInstr &instruction, unsigned reg) {
    const std::vector<AArch64MachineOperand> &operands = instruction.get_operands();
    for (std::size_t index = 0; index < operands.size(); ++index) {
        const auto *memory = operands[index].get_memory_address_operand();
        if (memory == nullptr ||
            memory->base_kind !=
                AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg ||
            memory->physical_reg != reg ||
            memory->address_mode !=
                AArch64MachineMemoryAddressOperand::AddressMode::Offset) {
            continue;
        }
        return index;
    }
    return std::nullopt;
}

bool instruction_uses_physical_reg_outside_memory(const AArch64MachineInstr &instruction,
                                                  unsigned reg) {
    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        const auto *physical = operand.get_physical_reg_operand();
        if (physical != nullptr && physical->reg_number == reg) {
            return true;
        }
    }
    return false;
}

std::optional<std::size_t>
memory_access_size_for_instruction(const AArch64MachineInstr &instruction) {
    switch (instruction.get_opcode()) {
    case AArch64MachineOpcode::LoadByte:
    case AArch64MachineOpcode::StoreByte:
    case AArch64MachineOpcode::LoadByteUnscaled:
    case AArch64MachineOpcode::StoreByteUnscaled:
        return 1;
    case AArch64MachineOpcode::LoadHalf:
    case AArch64MachineOpcode::StoreHalf:
    case AArch64MachineOpcode::LoadHalfUnscaled:
    case AArch64MachineOpcode::StoreHalfUnscaled:
        return 2;
    case AArch64MachineOpcode::Load:
    case AArch64MachineOpcode::Store:
    case AArch64MachineOpcode::LoadUnscaled:
    case AArch64MachineOpcode::StoreUnscaled:
        break;
    default:
        return std::nullopt;
    }

    if (instruction.get_operands().empty()) {
        return std::nullopt;
    }
    if (const auto *physical = instruction.get_operands()[0].get_physical_reg_operand();
        physical != nullptr) {
        switch (physical->kind) {
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
    }
    if (const auto *virtual_reg = instruction.get_operands()[0].get_virtual_reg_operand();
        virtual_reg != nullptr) {
        switch (virtual_reg->reg.get_kind()) {
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
    }
    return std::nullopt;
}

bool memory_opcode_accepts_unsigned_sp_offset(const AArch64MachineInstr &instruction,
                                              long long offset) {
    if (offset < 0) {
        return false;
    }
    const std::optional<std::size_t> access_size =
        memory_access_size_for_instruction(instruction);
    if (!access_size.has_value()) {
        return false;
    }

    switch (instruction.get_opcode()) {
    case AArch64MachineOpcode::LoadUnscaled:
    case AArch64MachineOpcode::StoreUnscaled:
    case AArch64MachineOpcode::LoadByteUnscaled:
    case AArch64MachineOpcode::StoreByteUnscaled:
    case AArch64MachineOpcode::LoadHalfUnscaled:
    case AArch64MachineOpcode::StoreHalfUnscaled:
        return offset <= 255;
    case AArch64MachineOpcode::Load:
    case AArch64MachineOpcode::Store:
    case AArch64MachineOpcode::LoadByte:
    case AArch64MachineOpcode::StoreByte:
    case AArch64MachineOpcode::LoadHalf:
    case AArch64MachineOpcode::StoreHalf:
        if ((offset % static_cast<long long>(*access_size)) != 0) {
            return false;
        }
        return (offset / static_cast<long long>(*access_size)) <= 4095;
    default:
        return false;
    }
}

std::optional<unsigned> scaled_register_shift_for_memory_access(
    const AArch64MachineInstr &instruction) {
    switch (instruction.get_opcode()) {
    case AArch64MachineOpcode::Load:
    case AArch64MachineOpcode::Store:
    case AArch64MachineOpcode::LoadByte:
    case AArch64MachineOpcode::StoreByte:
    case AArch64MachineOpcode::LoadHalf:
    case AArch64MachineOpcode::StoreHalf:
        break;
    default:
        return std::nullopt;
    }
    if (instruction.get_operands().empty()) {
        return std::nullopt;
    }
    if (const auto *physical = instruction.get_operands()[0].get_physical_reg_operand();
        physical != nullptr && !is_general_kind(physical->kind)) {
        return std::nullopt;
    }
    if (const auto *virtual_reg =
            instruction.get_operands()[0].get_virtual_reg_operand();
        virtual_reg != nullptr && !virtual_reg->reg.is_general()) {
        return std::nullopt;
    }
    const std::optional<std::size_t> access_size =
        memory_access_size_for_instruction(instruction);
    if (!access_size.has_value()) {
        return std::nullopt;
    }
    switch (*access_size) {
    case 1:
        return 0;
    case 2:
        return 1;
    case 4:
        return 2;
    case 8:
        return 3;
    default:
        return std::nullopt;
    }
}

bool extract_indexed_address_add(const AArch64MachineFunction &function,
                                 const AArch64MachineInstr &instruction,
                                 IndexedAddressAddInfo &info) {
    if (instruction.get_opcode() != AArch64MachineOpcode::Add ||
        instruction.get_operands().size() != 4) {
        return false;
    }
    const auto *dst_virtual =
        instruction.get_operands()[0].get_virtual_reg_operand();
    if (dst_virtual == nullptr || !dst_virtual->is_def ||
        !dst_virtual->reg.is_general()) {
        return false;
    }
    const auto dst = resolve_assigned_general_machine_reg(
        function, instruction.get_operands()[0]);
    const auto base = resolve_assigned_general_machine_reg(
        function, instruction.get_operands()[1]);
    const auto index = resolve_assigned_general_machine_reg(
        function, instruction.get_operands()[2]);
    const auto *shift = instruction.get_operands()[3].get_shift_operand();
    if (!dst.has_value() || !base.has_value() || !index.has_value() ||
        shift == nullptr || dst->reg_kind != AArch64VirtualRegKind::General64 ||
        base->reg_kind != AArch64VirtualRegKind::General64 ||
        index->reg_kind != AArch64VirtualRegKind::General64 ||
        shift->kind != AArch64ShiftKind::Lsl ||
        is_float_physical_reg(dst->reg_number) ||
        is_float_physical_reg(base->reg_number) ||
        is_float_physical_reg(index->reg_number)) {
        return false;
    }
    info = IndexedAddressAddInfo{dst_virtual->reg, dst->reg_number,
                                 base->reg_number, index->reg_number,
                                 shift->amount};
    return true;
}

bool load_store_uses_zero_offset_temp_base(const AArch64MachineInstr &instruction,
                                           const AArch64VirtualReg &temp_virtual,
                                           unsigned temp_reg,
                                           std::size_t &memory_operand_index) {
    const std::optional<unsigned> required_shift =
        scaled_register_shift_for_memory_access(instruction);
    if (!required_shift.has_value()) {
        return false;
    }
    const std::vector<AArch64MachineOperand> &operands = instruction.get_operands();
    for (std::size_t index = 0; index < operands.size(); ++index) {
        const auto *memory = operands[index].get_memory_address_operand();
        if (memory == nullptr ||
            memory->address_mode !=
                AArch64MachineMemoryAddressOperand::AddressMode::Offset ||
            memory->get_symbolic_offset() != nullptr ||
            memory->get_register_offset() != nullptr ||
            memory->get_immediate_offset().value_or(0) != 0) {
            continue;
        }
        const bool base_matches =
            (memory->base_kind ==
                 AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg &&
             same_virtual_reg(memory->virtual_reg, temp_virtual)) ||
            (memory->base_kind ==
                 AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg &&
             memory->physical_reg == temp_reg);
        if (!base_matches) {
            continue;
        }
        memory_operand_index = index;
        return true;
    }
    return false;
}

struct FixedFrameSlotAddress {
    enum class BaseKind : unsigned char {
        FramePointer,
        StackPointer,
    };

    BaseKind base_kind = BaseKind::FramePointer;
    long long offset = 0;
};

bool extract_fixed_frame_slot_general64_load(const AArch64MachineInstr &instruction,
                                             FixedFrameSlotAddress &slot,
                                             unsigned &dst_reg) {
    if ((instruction.get_opcode() != AArch64MachineOpcode::Load &&
         instruction.get_opcode() != AArch64MachineOpcode::LoadUnscaled) ||
        instruction.get_operands().size() != 2) {
        return false;
    }
    const auto *dst = instruction.get_operands()[0].get_physical_reg_operand();
    const auto *memory = instruction.get_operands()[1].get_memory_address_operand();
    if (dst == nullptr || memory == nullptr ||
        dst->kind != AArch64VirtualRegKind::General64 ||
        memory->address_mode !=
            AArch64MachineMemoryAddressOperand::AddressMode::Offset) {
        return false;
    }
    if (memory->base_kind ==
        AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg) {
        if (memory->physical_reg != static_cast<unsigned>(AArch64PhysicalReg::X29)) {
            return false;
        }
        slot = FixedFrameSlotAddress{FixedFrameSlotAddress::BaseKind::FramePointer,
                                     memory->get_immediate_offset().value_or(0)};
    } else if (memory->base_kind ==
               AArch64MachineMemoryAddressOperand::BaseKind::StackPointer) {
        slot = FixedFrameSlotAddress{FixedFrameSlotAddress::BaseKind::StackPointer,
                                     memory->get_immediate_offset().value_or(0)};
    } else {
        return false;
    }
    dst_reg = dst->reg_number;
    return true;
}


bool extract_fixed_frame_slot_general_store(const AArch64MachineInstr &instruction,
                                            FixedFrameSlotAddress &slot,
                                            unsigned &src_reg,
                                            AArch64VirtualRegKind &src_kind) {
    if ((instruction.get_opcode() != AArch64MachineOpcode::Store &&
         instruction.get_opcode() != AArch64MachineOpcode::StoreUnscaled &&
         instruction.get_opcode() != AArch64MachineOpcode::StoreByte &&
         instruction.get_opcode() != AArch64MachineOpcode::StoreByteUnscaled &&
         instruction.get_opcode() != AArch64MachineOpcode::StoreHalf &&
         instruction.get_opcode() != AArch64MachineOpcode::StoreHalfUnscaled) ||
        instruction.get_operands().size() != 2) {
        return false;
    }
    const auto *src = instruction.get_operands()[0].get_physical_reg_operand();
    const auto *memory = instruction.get_operands()[1].get_memory_address_operand();
    if (src == nullptr || memory == nullptr || !is_general_kind(src->kind) ||
        memory->address_mode !=
            AArch64MachineMemoryAddressOperand::AddressMode::Offset) {
        return false;
    }
    if (memory->base_kind ==
        AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg) {
        if (memory->physical_reg != static_cast<unsigned>(AArch64PhysicalReg::X29)) {
            return false;
        }
        slot = FixedFrameSlotAddress{FixedFrameSlotAddress::BaseKind::FramePointer,
                                     memory->get_immediate_offset().value_or(0)};
    } else if (memory->base_kind ==
               AArch64MachineMemoryAddressOperand::BaseKind::StackPointer) {
        slot = FixedFrameSlotAddress{FixedFrameSlotAddress::BaseKind::StackPointer,
                                     memory->get_immediate_offset().value_or(0)};
    } else {
        return false;
    }
    src_reg = src->reg_number;
    src_kind = src->kind;
    return true;
}

bool same_fixed_frame_slot(const FixedFrameSlotAddress &lhs,
                           const FixedFrameSlotAddress &rhs) {
    return lhs.base_kind == rhs.base_kind && lhs.offset == rhs.offset;
}

struct FixedFrameSlotFloatLoadInfo {
    FixedFrameSlotAddress slot;
    unsigned dst_reg = 0;
    AArch64VirtualRegKind kind = AArch64VirtualRegKind::Float128;
};

bool extract_fixed_frame_slot_float_load(const AArch64MachineFunction &function,
                                         const AArch64MachineInstr &instruction,
                                         FixedFrameSlotFloatLoadInfo &info) {
    if ((instruction.get_opcode() != AArch64MachineOpcode::Load &&
         instruction.get_opcode() != AArch64MachineOpcode::LoadUnscaled) ||
        instruction.get_operands().size() != 2) {
        return false;
    }

    unsigned dst_reg = 0;
    AArch64VirtualRegKind dst_kind = AArch64VirtualRegKind::Float128;
    if (const auto *physical =
            instruction.get_operands()[0].get_physical_reg_operand();
        physical != nullptr) {
        if (is_general_kind(physical->kind)) {
            return false;
        }
        dst_reg = physical->reg_number;
        dst_kind = physical->kind;
    } else if (const auto *vector =
                   instruction.get_operands()[0].get_vector_reg_operand();
               vector != nullptr && vector->is_def) {
        const auto assigned =
            resolve_assigned_vector_reg(function, instruction.get_operands()[0]);
        if (!assigned.has_value()) {
            return false;
        }
        dst_reg = assigned->reg_number;
        dst_kind = AArch64VirtualRegKind::Float128;
    } else {
        return false;
    }

    const auto *memory = instruction.get_operands()[1].get_memory_address_operand();
    if (memory == nullptr ||
        memory->address_mode !=
            AArch64MachineMemoryAddressOperand::AddressMode::Offset ||
        memory->get_symbolic_offset() != nullptr ||
        memory->get_register_offset() != nullptr) {
        return false;
    }
    const auto immediate_offset = memory->get_immediate_offset();
    if (!immediate_offset.has_value()) {
        return false;
    }
    if (memory->base_kind ==
        AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg) {
        if (memory->physical_reg != static_cast<unsigned>(AArch64PhysicalReg::X29)) {
            return false;
        }
        info = FixedFrameSlotFloatLoadInfo{
            FixedFrameSlotAddress{FixedFrameSlotAddress::BaseKind::FramePointer,
                                  *immediate_offset},
            dst_reg, dst_kind};
        return true;
    }
    return false;
}

bool instruction_defines_float_physical_reg(
    const AArch64MachineFunction &function, const AArch64MachineInstr &instruction,
    unsigned reg_number) {
    const std::vector<AArch64MachineOperand> &operands = instruction.get_operands();
    for (std::size_t index = 0; index < operands.size(); ++index) {
        const AArch64MachineOperand &operand = operands[index];
        if (const auto *physical = operand.get_physical_reg_operand();
            physical != nullptr && physical->reg_number == reg_number &&
            !is_general_kind(physical->kind) &&
            physical_operand_index_is_explicit_def(instruction, index)) {
            return true;
        }
        if (const auto *vector = operand.get_vector_reg_operand();
            vector != nullptr && vector->is_def) {
            const auto assigned = resolve_assigned_vector_reg(function, operand);
            if (assigned.has_value() && assigned->reg_number == reg_number) {
                return true;
            }
        }
    }
    return false;
}

struct MaterializedMoveWideConstantInfo {
    unsigned reg_number = 0;
    AArch64VirtualRegKind reg_kind = AArch64VirtualRegKind::General64;
    std::uint64_t value = 0;
    std::size_t length = 0;
};

std::optional<unsigned long long>
parse_unsigned_hash_immediate(const AArch64MachineOperand &operand) {
    const auto *immediate = operand.get_immediate_operand();
    if (immediate == nullptr || immediate->asm_text.empty() ||
        immediate->asm_text.front() != '#') {
        return std::nullopt;
    }
    try {
        std::size_t parsed = 0;
        const unsigned long long value =
            std::stoull(immediate->asm_text.substr(1), &parsed, 10);
        if (parsed + 1 != immediate->asm_text.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

unsigned shift_amount_for_move_wide(const AArch64MachineInstr &instruction) {
    const auto &operands = instruction.get_operands();
    std::size_t shift_index = 2;
    if (instruction.get_opcode() == AArch64MachineOpcode::MoveWideKeep &&
        operands.size() >= 4 && operands[3].get_shift_operand() != nullptr) {
        shift_index = 3;
    }
    if (operands.size() <= shift_index) {
        return 0;
    }
    const auto *shift = operands[shift_index].get_shift_operand();
    if (shift == nullptr || shift->kind != AArch64ShiftKind::Lsl) {
        return 0;
    }
    return shift->amount;
}

bool extract_materialized_move_wide_constant(
    const AArch64MachineFunction &function,
    const std::vector<AArch64MachineInstr> &instructions, std::size_t start_index,
    MaterializedMoveWideConstantInfo &info) {
    if (start_index >= instructions.size()) {
        return false;
    }
    const AArch64MachineInstr &first = instructions[start_index];
    if (first.get_opcode() != AArch64MachineOpcode::MoveWideZero ||
        first.get_operands().size() < 2) {
        return false;
    }
    const auto dst =
        resolve_assigned_general_machine_reg(function, first.get_operands()[0]);
    const auto imm0 = parse_unsigned_hash_immediate(first.get_operands()[1]);
    if (!dst.has_value() || !imm0.has_value() || *imm0 > 0xFFFFULL) {
        return false;
    }

    std::uint64_t value = (*imm0) << shift_amount_for_move_wide(first);
    std::size_t length = 1;
    while (start_index + length < instructions.size()) {
        const AArch64MachineInstr &piece = instructions[start_index + length];
        if (piece.get_opcode() != AArch64MachineOpcode::MoveWideKeep ||
            piece.get_operands().size() < 2) {
            break;
        }
        const std::size_t piece_imm_index =
            piece.get_operands().size() >= 3 &&
                    piece.get_operands()[2].get_immediate_operand() != nullptr
                ? 2U
                : 1U;
        const auto piece_dst =
            resolve_assigned_general_machine_reg(function, piece.get_operands()[0]);
        const auto piece_imm =
            parse_unsigned_hash_immediate(piece.get_operands()[piece_imm_index]);
        if (!piece_dst.has_value() || piece_imm == std::nullopt ||
            piece_dst->reg_number != dst->reg_number ||
            piece_dst->reg_kind != dst->reg_kind ||
            *piece_imm > 0xFFFFULL) {
            break;
        }
        const unsigned shift = shift_amount_for_move_wide(piece);
        if (shift >= 64 || (shift % 16) != 0) {
            break;
        }
        value &= ~(0xFFFFULL << shift);
        value |= ((*piece_imm) & 0xFFFFULL) << shift;
        ++length;
    }

    if (dst->reg_kind == AArch64VirtualRegKind::General32) {
        value &= 0xFFFFFFFFULL;
    }
    info = MaterializedMoveWideConstantInfo{dst->reg_number, dst->reg_kind, value,
                                            length};
    return true;
}

std::optional<std::pair<std::uint64_t, bool>>
encodeable_add_sub_immediate(std::int64_t signed_value) {
    const bool use_sub = signed_value < 0;
    const std::uint64_t magnitude = static_cast<std::uint64_t>(
        use_sub ? -signed_value : signed_value);
    if (magnitude <= 4095ULL) {
        return std::pair<std::uint64_t, bool>{magnitude, false};
    }
    if ((magnitude % 4096ULL) == 0) {
        const std::uint64_t shifted = magnitude / 4096ULL;
        if (shifted <= 4095ULL) {
            return std::pair<std::uint64_t, bool>{shifted, true};
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t>
add_sub_immediate_magnitude(const AArch64MachineInstr &instruction) {
    if (instruction.get_operands().size() != 3 &&
        instruction.get_operands().size() != 4) {
        return std::nullopt;
    }
    const std::optional<long long> parsed =
        parse_hash_immediate(instruction.get_operands()[2]);
    if (!parsed.has_value() || *parsed < 0) {
        return std::nullopt;
    }
    std::uint64_t value = static_cast<std::uint64_t>(*parsed);
    if (instruction.get_operands().size() == 4) {
        const auto *shift = instruction.get_operands()[3].get_shift_operand();
        if (shift == nullptr || shift->kind != AArch64ShiftKind::Lsl ||
            shift->amount >= 64U) {
            return std::nullopt;
        }
        value <<= shift->amount;
    }
    return value;
}

std::optional<AArch64VirtualRegKind>
general_operand_kind(const AArch64MachineOperand &operand,
                     const AArch64MachineFunction &function) {
    if (const auto *physical = operand.get_physical_reg_operand();
        physical != nullptr && is_general_kind(physical->kind)) {
        return physical->kind;
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr && virtual_reg->reg.is_general()) {
        const std::optional<unsigned> assigned =
            function.get_physical_reg_for_virtual(virtual_reg->reg.get_id());
        if (!assigned.has_value()) {
            return std::nullopt;
        }
        return virtual_reg->reg.get_kind();
    }
    return std::nullopt;
}

AArch64MachineOperand
general_operand_as_use(const AArch64MachineOperand &operand) {
    if (const auto *physical = operand.get_physical_reg_operand();
        physical != nullptr) {
        return AArch64MachineOperand::physical_reg(physical->reg_number,
                                                   physical->kind);
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        return AArch64MachineOperand::use_virtual_reg(virtual_reg->reg);
    }
    return operand;
}

std::vector<AArch64MachineInstr>
materialize_physical_integer_constant(const AArch64MachineOperand &dst_operand,
                                      AArch64VirtualRegKind kind,
                                      std::uint64_t value,
                                      const AArch64MachineInstr &source) {
    const bool use_64bit = uses_general_64bit_register(kind);
    const std::uint64_t mask = use_64bit ? ~0ULL : 0xFFFFFFFFULL;
    value &= mask;

    std::vector<AArch64MachineInstr> result;
    if (value == 0) {
        result.emplace_back(AArch64MachineOpcode::Move,
                            std::vector<AArch64MachineOperand>{
                                dst_operand, zero_register_operand(use_64bit)},
                            source.get_flags(), source.get_debug_location(),
                            source.get_implicit_defs(),
                            source.get_implicit_uses(),
                            source.get_call_clobber_mask());
        return result;
    }

    const unsigned pieces = use_64bit ? 4U : 2U;
    bool emitted_first_piece = false;
    const AArch64MachineOperand dst_use = general_operand_as_use(dst_operand);
    for (unsigned piece = 0; piece < pieces; ++piece) {
        const std::uint16_t imm16 =
            static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
        if (!emitted_first_piece) {
            result.emplace_back(
                AArch64MachineOpcode::MoveWideZero,
                std::vector<AArch64MachineOperand>{
                    dst_operand,
                    AArch64MachineOperand::immediate("#" +
                                                     std::to_string(imm16)),
                    shift_operand("lsl", piece * 16U)},
                source.get_flags(), source.get_debug_location(),
                source.get_implicit_defs(), source.get_implicit_uses(),
                source.get_call_clobber_mask());
            emitted_first_piece = true;
            continue;
        }
        if (imm16 == 0) {
            continue;
        }
        result.emplace_back(
            AArch64MachineOpcode::MoveWideKeep,
            std::vector<AArch64MachineOperand>{
                dst_operand, dst_use,
                AArch64MachineOperand::immediate("#" + std::to_string(imm16)),
                shift_operand("lsl", piece * 16U)},
            source.get_flags(), source.get_debug_location(),
            source.get_implicit_defs(), source.get_implicit_uses(),
            source.get_call_clobber_mask());
    }
    return result;
}

bool legalize_zero_register_add_sub_immediates(AArch64MachineFunction &function) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t index = 0; index < instructions.size(); ++index) {
            const AArch64MachineInstr &instruction = instructions[index];
            if ((instruction.get_opcode() != AArch64MachineOpcode::Add &&
                 instruction.get_opcode() != AArch64MachineOpcode::Sub) ||
                instruction.get_operands().size() < 3 ||
                !instruction.get_operands()[1].is_zero_register()) {
                continue;
            }

            const std::optional<std::uint64_t> magnitude =
                add_sub_immediate_magnitude(instruction);
            const std::optional<AArch64VirtualRegKind> kind =
                general_operand_kind(instruction.get_operands()[0], function);
            if (!magnitude.has_value() || !kind.has_value()) {
                continue;
            }

            const std::uint64_t value =
                instruction.get_opcode() == AArch64MachineOpcode::Sub
                    ? static_cast<std::uint64_t>(0) - *magnitude
                    : *magnitude;
            std::vector<AArch64MachineInstr> replacement =
                materialize_physical_integer_constant(instruction.get_operands()[0],
                                                      *kind, value, instruction);
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(index));
            instructions.insert(instructions.begin() +
                                    static_cast<std::ptrdiff_t>(index),
                                replacement.begin(), replacement.end());
            return true;
        }
    }
    return false;
}

bool is_uxtw_same_reg_instruction(const AArch64MachineInstr &instruction,
                                  unsigned &reg_number) {
    if (instruction.get_mnemonic() != "uxtw" ||
        instruction.get_operands().size() != 2) {
        return false;
    }
    const auto *dst = instruction.get_operands()[0].get_physical_reg_operand();
    const auto *src = instruction.get_operands()[1].get_physical_reg_operand();
    if (dst == nullptr || src == nullptr ||
        dst->kind != AArch64VirtualRegKind::General64 ||
        src->kind != AArch64VirtualRegKind::General32 ||
        dst->reg_number != src->reg_number) {
        return false;
    }
    reg_number = dst->reg_number;
    return true;
}

bool remove_redundant_zero_extend_slot_store(AArch64MachineFunction &function) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t index = 0; index + 2 < instructions.size(); ++index) {
            FixedFrameSlotAddress first_slot;
            FixedFrameSlotAddress second_slot;
            unsigned first_src = 0;
            unsigned second_src = 0;
            unsigned extend_reg = 0;
            AArch64VirtualRegKind first_kind = AArch64VirtualRegKind::General32;
            AArch64VirtualRegKind second_kind = AArch64VirtualRegKind::General32;
            if (!extract_fixed_frame_slot_general_store(instructions[index], first_slot,
                                                        first_src, first_kind) ||
                first_kind != AArch64VirtualRegKind::General32 ||
                !is_uxtw_same_reg_instruction(instructions[index + 1], extend_reg) ||
                extend_reg != first_src ||
                !extract_fixed_frame_slot_general_store(instructions[index + 2],
                                                        second_slot, second_src,
                                                        second_kind) ||
                second_kind != AArch64VirtualRegKind::General64 ||
                second_src != extend_reg ||
                !same_fixed_frame_slot(first_slot, second_slot)) {
                continue;
            }
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(index));
            return true;
        }
    }
    return false;
}

bool collapse_repeated_frame_slot_mask_store_clusters(AArch64MachineFunction &function) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t index = 0; index + 5 < instructions.size(); ++index) {
            FixedFrameSlotAddress source_slot;
            unsigned load_reg = 0;
            unsigned mask_reg = 0;
            const auto *mask_imm = instructions[index + 1].get_operands().size() == 3
                                       ? instructions[index + 1]
                                             .get_operands()[2]
                                             .get_immediate_operand()
                                       : nullptr;
            FixedFrameSlotAddress first_store_slot;
            unsigned first_store_src = 0;
            AArch64VirtualRegKind first_store_kind = AArch64VirtualRegKind::General32;
            if (!extract_fixed_frame_slot_general64_load(instructions[index], source_slot,
                                                         load_reg) ||
                instructions[index + 1].get_opcode() != AArch64MachineOpcode::And ||
                instructions[index + 1].get_operands().size() != 3 ||
                mask_imm == nullptr ||
                !extract_fixed_frame_slot_general_store(instructions[index + 2],
                                                       first_store_slot,
                                                       first_store_src,
                                                       first_store_kind) ||
                first_store_kind != AArch64VirtualRegKind::General64) {
                continue;
            }

            const auto *mask_dst =
                instructions[index + 1].get_operands()[0].get_physical_reg_operand();
            const auto *mask_src =
                instructions[index + 1].get_operands()[1].get_physical_reg_operand();
            if (mask_dst == nullptr || mask_src == nullptr ||
                mask_dst->kind != AArch64VirtualRegKind::General64 ||
                mask_src->kind != AArch64VirtualRegKind::General64 ||
                mask_src->reg_number != load_reg ||
                first_store_src != mask_dst->reg_number) {
                continue;
            }
            mask_reg = mask_dst->reg_number;

            std::size_t group_count = 1;
            while (index + group_count * 3 + 2 < instructions.size()) {
                const std::size_t group_index = index + group_count * 3;
                FixedFrameSlotAddress group_load_slot;
                unsigned group_load_reg = 0;
                FixedFrameSlotAddress group_store_slot;
                unsigned group_store_src = 0;
                AArch64VirtualRegKind group_store_kind =
                    AArch64VirtualRegKind::General32;
                const auto *group_mask_imm =
                    instructions[group_index + 1].get_operands().size() == 3
                        ? instructions[group_index + 1]
                              .get_operands()[2]
                              .get_immediate_operand()
                        : nullptr;
                if (!extract_fixed_frame_slot_general64_load(instructions[group_index],
                                                             group_load_slot,
                                                             group_load_reg) ||
                    !same_fixed_frame_slot(group_load_slot, source_slot) ||
                    group_load_reg != load_reg ||
                    instructions[group_index + 1].get_opcode() !=
                        AArch64MachineOpcode::And ||
                    instructions[group_index + 1].get_operands().size() != 3 ||
                    group_mask_imm == nullptr ||
                    group_mask_imm->asm_text != mask_imm->asm_text ||
                    !extract_fixed_frame_slot_general_store(
                        instructions[group_index + 2], group_store_slot,
                        group_store_src, group_store_kind) ||
                    group_store_kind != AArch64VirtualRegKind::General64) {
                    break;
                }
                const auto *group_mask_dst = instructions[group_index + 1]
                                                 .get_operands()[0]
                                                 .get_physical_reg_operand();
                const auto *group_mask_src = instructions[group_index + 1]
                                                 .get_operands()[1]
                                                 .get_physical_reg_operand();
                if (group_mask_dst == nullptr || group_mask_src == nullptr ||
                    group_mask_dst->kind != AArch64VirtualRegKind::General64 ||
                    group_mask_src->kind != AArch64VirtualRegKind::General64 ||
                    group_mask_dst->reg_number != mask_reg ||
                    group_mask_src->reg_number != load_reg ||
                    group_store_src != mask_reg) {
                    break;
                }
                ++group_count;
            }

            if (group_count < 2) {
                continue;
            }
            for (std::size_t group = group_count; group-- > 1;) {
                const std::size_t group_index = index + group * 3;
                instructions.erase(
                    instructions.begin() +
                        static_cast<std::ptrdiff_t>(group_index),
                    instructions.begin() +
                        static_cast<std::ptrdiff_t>(group_index + 2));
            }
            return true;
        }
    }
    return false;
}

bool fold_move_wide_constant_adds(AArch64MachineFunction &function) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t index = 0; index < instructions.size(); ++index) {
            MaterializedMoveWideConstantInfo constant;
            if (!extract_materialized_move_wide_constant(function, instructions, index,
                                                        constant)) {
                continue;
            }
            const std::size_t add_index = index + constant.length;
            if (add_index >= instructions.size()) {
                continue;
            }
            const AArch64MachineInstr &add_instruction = instructions[add_index];
            if (add_instruction.get_opcode() != AArch64MachineOpcode::Add ||
                add_instruction.get_operands().size() != 3) {
                continue;
            }
            const auto dst = resolve_assigned_general_machine_reg(
                function, add_instruction.get_operands()[0]);
            const auto lhs = resolve_assigned_general_machine_reg(
                function, add_instruction.get_operands()[1]);
            const auto rhs = resolve_assigned_general_machine_reg(
                function, add_instruction.get_operands()[2]);
            if (!dst.has_value() || !lhs.has_value() || !rhs.has_value()) {
                continue;
            }

            if (dst->reg_kind != constant.reg_kind ||
                dst->reg_number != constant.reg_number) {
                continue;
            }
            std::size_t base_operand_index = 0;
            if (lhs->reg_number == constant.reg_number &&
                lhs->reg_kind == constant.reg_kind &&
                rhs->reg_number != constant.reg_number) {
                base_operand_index = 2;
            } else if (rhs->reg_number == constant.reg_number &&
                       rhs->reg_kind == constant.reg_kind &&
                       lhs->reg_number != constant.reg_number) {
                base_operand_index = 1;
            } else {
                continue;
            }

            const std::int64_t signed_value =
                constant.reg_kind == AArch64VirtualRegKind::General32
                    ? static_cast<std::int32_t>(constant.value & 0xFFFFFFFFULL)
                    : static_cast<std::int64_t>(constant.value);
            const auto immediate = encodeable_add_sub_immediate(signed_value);
            if (!immediate.has_value()) {
                continue;
            }
            const AArch64MachineOperand &base_operand =
                add_instruction.get_operands()[base_operand_index];
            if (signed_value <= 0 && base_operand.is_zero_register()) {
                continue;
            }

            std::vector<AArch64MachineOperand> replacement_operands{
                add_instruction.get_operands()[0],
                base_operand,
                AArch64MachineOperand::immediate(
                    "#" + std::to_string(immediate->first))};
            if (immediate->second) {
                replacement_operands.push_back(
                    AArch64MachineOperand::shift(AArch64ShiftKind::Lsl, 12));
            }

            instructions[add_index] = AArch64MachineInstr(
                signed_value < 0 ? AArch64MachineOpcode::Sub
                                 : AArch64MachineOpcode::Add,
                std::move(replacement_operands), add_instruction.get_flags(),
                add_instruction.get_debug_location(),
                add_instruction.get_implicit_defs(),
                add_instruction.get_implicit_uses(),
                add_instruction.get_call_clobber_mask());
            instructions.erase(
                instructions.begin() + static_cast<std::ptrdiff_t>(index),
                instructions.begin() + static_cast<std::ptrdiff_t>(add_index));
            return true;
        }
    }
    return false;
}

bool canonicalize_frame_temp_memory_accesses(AArch64MachineFunction &function) {
    const long long frame_size = static_cast<long long>(projected_frame_size(function));
    if (frame_size == 0) {
        return false;
    }

    bool changed = false;
    for (AArch64MachineBlock &block : function.get_blocks()) {
        if (block_has_dynamic_stack_pointer_usage(block)) {
            continue;
        }
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t index = 0; index + 1 < instructions.size();) {
            FrameTempAddressSetupInfo setup;
            if (!extract_frame_temp_address_setup(instructions[index], setup)) {
                ++index;
                continue;
            }
            const std::optional<std::size_t> memory_index =
                memory_operand_index_for_base(instructions[index + 1], setup.temp_reg);
            if (!memory_index.has_value() ||
                instruction_uses_physical_reg_outside_memory(instructions[index + 1],
                                                             setup.temp_reg)) {
                ++index;
                continue;
            }

            const auto *memory =
                instructions[index + 1].get_operands()[*memory_index].get_memory_address_operand();
            if (memory == nullptr) {
                ++index;
                continue;
            }
            const long long base_adjust = memory->get_immediate_offset().value_or(0);
            const long long sp_offset = frame_size - setup.frame_offset + base_adjust;
            if (!memory_opcode_accepts_unsigned_sp_offset(instructions[index + 1],
                                                          sp_offset)) {
                ++index;
                continue;
            }

            std::vector<AArch64MachineOperand> rewritten_operands =
                instructions[index + 1].get_operands();
            rewritten_operands[*memory_index] =
                AArch64MachineOperand::memory_address_stack_pointer(
                    sp_offset, true, memory->address_mode);
            instructions[index + 1] = AArch64MachineInstr(
                instructions[index + 1].get_opcode(), std::move(rewritten_operands),
                instructions[index + 1].get_flags(),
                instructions[index + 1].get_debug_location(),
                instructions[index + 1].get_implicit_defs(),
                instructions[index + 1].get_implicit_uses(),
                instructions[index + 1].get_call_clobber_mask());
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(index));
            changed = true;
            ++index;
        }
    }
    return changed;
}

bool canonicalize_materialized_frame_temp_memory_accesses(
    AArch64MachineFunction &function) {
    const long long frame_size = static_cast<long long>(projected_frame_size(function));
    if (frame_size == 0) {
        return false;
    }

    bool changed = false;
    for (AArch64MachineBlock &block : function.get_blocks()) {
        if (block_has_dynamic_stack_pointer_usage(block)) {
            continue;
        }
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t index = 0; index + 2 < instructions.size();) {
            MaterializedMoveWideConstantInfo constant;
            if (!extract_materialized_move_wide_constant(function, instructions, index,
                                                        constant)) {
                ++index;
                continue;
            }

            const std::size_t sub_index = index + constant.length;
            const std::size_t memory_index = sub_index + 1;
            if (memory_index >= instructions.size()) {
                ++index;
                continue;
            }

            const AArch64MachineInstr &sub_instruction = instructions[sub_index];
            if (sub_instruction.get_opcode() != AArch64MachineOpcode::Sub ||
                sub_instruction.get_operands().size() != 3) {
                ++index;
                continue;
            }
            const auto dst = resolve_assigned_general_machine_reg(
                function, sub_instruction.get_operands()[0]);
            const auto base = resolve_assigned_general_machine_reg(
                function, sub_instruction.get_operands()[1]);
            const auto rhs = resolve_assigned_general_machine_reg(
                function, sub_instruction.get_operands()[2]);
            if (!dst.has_value() || !base.has_value() || !rhs.has_value() ||
                dst->reg_number != constant.reg_number ||
                dst->reg_kind != constant.reg_kind ||
                rhs->reg_number != constant.reg_number ||
                rhs->reg_kind != constant.reg_kind ||
                base->reg_number != static_cast<unsigned>(AArch64PhysicalReg::X29) ||
                base->reg_kind != AArch64VirtualRegKind::General64 ||
                constant.reg_kind != AArch64VirtualRegKind::General64 ||
                constant.value > static_cast<std::uint64_t>(
                                     std::numeric_limits<long long>::max())) {
                ++index;
                continue;
            }

            const std::optional<std::size_t> memory_operand_index =
                memory_operand_index_for_base(instructions[memory_index],
                                             constant.reg_number);
            if (!memory_operand_index.has_value() ||
                instruction_uses_physical_reg_outside_memory(
                    instructions[memory_index], constant.reg_number)) {
                ++index;
                continue;
            }
            const auto *memory = instructions[memory_index]
                                     .get_operands()[*memory_operand_index]
                                     .get_memory_address_operand();
            if (memory == nullptr) {
                ++index;
                continue;
            }

            const long long frame_offset = static_cast<long long>(constant.value);
            const long long base_adjust = memory->get_immediate_offset().value_or(0);
            const long long sp_offset = frame_size - frame_offset + base_adjust;
            if (!memory_opcode_accepts_unsigned_sp_offset(instructions[memory_index],
                                                          sp_offset)) {
                ++index;
                continue;
            }

            std::vector<AArch64MachineOperand> rewritten_operands =
                instructions[memory_index].get_operands();
            rewritten_operands[*memory_operand_index] =
                AArch64MachineOperand::memory_address_stack_pointer(
                    sp_offset, true, memory->address_mode);
            instructions[memory_index] = AArch64MachineInstr(
                instructions[memory_index].get_opcode(),
                std::move(rewritten_operands),
                instructions[memory_index].get_flags(),
                instructions[memory_index].get_debug_location(),
                instructions[memory_index].get_implicit_defs(),
                instructions[memory_index].get_implicit_uses(),
                instructions[memory_index].get_call_clobber_mask());
            instructions.erase(
                instructions.begin() + static_cast<std::ptrdiff_t>(index),
                instructions.begin() + static_cast<std::ptrdiff_t>(memory_index));
            changed = true;
            ++index;
        }
    }
    return changed;
}


AArch64MachineOperand rebuild_memory_base_use(
    const AArch64MachineMemoryAddressOperand &memory,
    const AArch64VirtualReg &replacement_reg) {
    switch (memory.base_kind) {
    case AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg:
        if (const auto immediate_offset = memory.get_immediate_offset();
            immediate_offset.has_value()) {
            return AArch64MachineOperand::memory_address_virtual_reg(
                replacement_reg, *immediate_offset, memory.address_mode);
        }
        if (const auto *symbolic_offset = memory.get_symbolic_offset();
            symbolic_offset != nullptr) {
            return AArch64MachineOperand::memory_address_virtual_reg(
                replacement_reg, *symbolic_offset, memory.address_mode);
        }
        return AArch64MachineOperand::memory_address_virtual_reg(
            replacement_reg, std::nullopt, memory.address_mode);
    case AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg:
    case AArch64MachineMemoryAddressOperand::BaseKind::StackPointer:
        break;
    }
    return AArch64MachineOperand::memory_address_virtual_reg(
        replacement_reg, std::nullopt, memory.address_mode);
}

std::optional<AArch64MachineInstr>
rewrite_instruction_use(const AArch64MachineInstr &instruction,
                        const AArch64VirtualReg &from,
                        const AArch64VirtualReg &to) {
    bool changed = false;
    std::vector<AArch64MachineOperand> rewritten_operands =
        instruction.get_operands();
    for (AArch64MachineOperand &operand : rewritten_operands) {
        if (const auto *virtual_reg = operand.get_virtual_reg_operand();
            virtual_reg != nullptr && !virtual_reg->is_def &&
            same_virtual_reg(virtual_reg->reg, from)) {
            operand = AArch64MachineOperand::use_virtual_reg(to);
            changed = true;
            continue;
        }
        if (const auto *memory = operand.get_memory_address_operand();
            memory != nullptr &&
            memory->base_kind ==
                AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg &&
            same_virtual_reg(memory->virtual_reg, from)) {
            operand = rebuild_memory_base_use(*memory, to);
            changed = true;
        }
    }

    if (!changed) {
        return std::nullopt;
    }
    return AArch64MachineInstr(instruction.get_mnemonic(),
                               std::move(rewritten_operands),
                               instruction.get_flags(),
                               instruction.get_debug_location(),
                               instruction.get_implicit_defs(),
                               instruction.get_implicit_uses(),
                               instruction.get_call_clobber_mask());
}

std::optional<AArch64MachineInstr>
rewrite_instruction_physical_use(const AArch64MachineInstr &instruction,
                                 unsigned from_reg, unsigned to_reg) {
    bool changed = false;
    std::vector<AArch64MachineOperand> rewritten_operands = instruction.get_operands();
    for (AArch64MachineOperand &operand : rewritten_operands) {
        if (const auto *physical = operand.get_physical_reg_operand();
            physical != nullptr && physical->reg_number == from_reg) {
            operand = AArch64MachineOperand::physical_reg(to_reg, physical->kind);
            changed = true;
            continue;
        }
        if (const auto *memory = operand.get_memory_address_operand();
            memory != nullptr &&
            memory->base_kind ==
                AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg &&
            memory->physical_reg == from_reg) {
            if (const auto immediate_offset = memory->get_immediate_offset();
                immediate_offset.has_value()) {
                operand = AArch64MachineOperand::memory_address_physical_reg(
                    to_reg, *immediate_offset, memory->address_mode);
            } else if (const auto *symbolic_offset = memory->get_symbolic_offset();
                       symbolic_offset != nullptr) {
                operand = AArch64MachineOperand::memory_address_physical_reg(
                    to_reg, *symbolic_offset, memory->address_mode);
            } else {
                operand = AArch64MachineOperand::memory_address_physical_reg(
                    to_reg, std::nullopt, memory->address_mode);
            }
            changed = true;
        }
    }

    if (!changed) {
        return std::nullopt;
    }
    return AArch64MachineInstr(instruction.get_mnemonic(),
                               std::move(rewritten_operands),
                               instruction.get_flags(),
                               instruction.get_debug_location(),
                               instruction.get_implicit_defs(),
                               instruction.get_implicit_uses(),
                               instruction.get_call_clobber_mask());
}

std::optional<AArch64MachineInstr>
rewrite_instruction_source_physical_use(const AArch64MachineInstr &instruction,
                                        unsigned from_reg, unsigned to_reg) {
    bool changed = false;
    std::vector<AArch64MachineOperand> rewritten_operands = instruction.get_operands();
    for (std::size_t index = 0; index < rewritten_operands.size(); ++index) {
        AArch64MachineOperand &operand = rewritten_operands[index];
        if (const auto *physical = operand.get_physical_reg_operand();
            physical != nullptr && physical->reg_number == from_reg &&
            !physical_operand_index_is_explicit_def(instruction, index)) {
            operand = AArch64MachineOperand::physical_reg(to_reg, physical->kind);
            changed = true;
            continue;
        }
        if (const auto *memory = operand.get_memory_address_operand();
            memory != nullptr &&
            memory->base_kind ==
                AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg &&
            memory->physical_reg == from_reg) {
            if (const auto immediate_offset = memory->get_immediate_offset();
                immediate_offset.has_value()) {
                operand = AArch64MachineOperand::memory_address_physical_reg(
                    to_reg, *immediate_offset, memory->address_mode);
            } else if (const auto *symbolic_offset = memory->get_symbolic_offset();
                       symbolic_offset != nullptr) {
                operand = AArch64MachineOperand::memory_address_physical_reg(
                    to_reg, *symbolic_offset, memory->address_mode);
            } else {
                operand = AArch64MachineOperand::memory_address_physical_reg(
                    to_reg, std::nullopt, memory->address_mode);
            }
            changed = true;
        }
    }

    if (!changed) {
        return std::nullopt;
    }
    return AArch64MachineInstr(instruction.get_mnemonic(),
                               std::move(rewritten_operands),
                               instruction.get_flags(),
                               instruction.get_debug_location(),
                               instruction.get_implicit_defs(),
                               instruction.get_implicit_uses(),
                               instruction.get_call_clobber_mask());
}

std::optional<AArch64MachineInstr>
rewrite_instruction_virtual_use_to_physical(const AArch64MachineInstr &instruction,
                                            const AArch64VirtualReg &from,
                                            unsigned to_reg) {
    bool changed = false;
    std::vector<AArch64MachineOperand> rewritten_operands = instruction.get_operands();
    for (AArch64MachineOperand &operand : rewritten_operands) {
        if (const auto *virtual_reg = operand.get_virtual_reg_operand();
            virtual_reg != nullptr && !virtual_reg->is_def &&
            same_virtual_reg(virtual_reg->reg, from)) {
            operand =
                AArch64MachineOperand::physical_reg(to_reg, virtual_reg->reg.get_kind());
            changed = true;
            continue;
        }
        if (const auto *memory = operand.get_memory_address_operand();
            memory != nullptr &&
            memory->base_kind ==
                AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg &&
            same_virtual_reg(memory->virtual_reg, from)) {
            if (const auto immediate_offset = memory->get_immediate_offset();
                immediate_offset.has_value()) {
                operand = AArch64MachineOperand::memory_address_physical_reg(
                    to_reg, *immediate_offset, memory->address_mode);
            } else if (const auto *symbolic_offset = memory->get_symbolic_offset();
                       symbolic_offset != nullptr) {
                operand = AArch64MachineOperand::memory_address_physical_reg(
                    to_reg, *symbolic_offset, memory->address_mode);
            } else {
                operand = AArch64MachineOperand::memory_address_physical_reg(
                    to_reg, std::nullopt, memory->address_mode);
            }
            changed = true;
        }
    }

    if (!changed) {
        return std::nullopt;
    }
    return AArch64MachineInstr(instruction.get_mnemonic(),
                               std::move(rewritten_operands),
                               instruction.get_flags(),
                               instruction.get_debug_location(),
                               instruction.get_implicit_defs(),
                               instruction.get_implicit_uses(),
                               instruction.get_call_clobber_mask());
}

std::unordered_map<std::size_t, std::size_t>
collect_virtual_use_counts(const AArch64MachineFunction &function) {
    std::unordered_map<std::size_t, std::size_t> counts;
    for (const AArch64MachineBlock &block : function.get_blocks()) {
        for (const AArch64MachineInstr &instruction : block.get_instructions()) {
            for (const AArch64MachineOperand &operand : instruction.get_operands()) {
                if (const auto *virtual_reg = operand.get_virtual_reg_operand();
                    virtual_reg != nullptr && !virtual_reg->is_def) {
                    ++counts[virtual_reg->reg.get_id()];
                }
                if (const auto *memory = operand.get_memory_address_operand();
                    memory != nullptr &&
                    memory->base_kind ==
                        AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg) {
                    ++counts[memory->virtual_reg.get_id()];
                }
            }
        }
    }
    return counts;
}

std::unordered_set<unsigned>
collect_used_general_physical_regs(const AArch64MachineFunction &function) {
    std::unordered_set<unsigned> regs;
    for (const AArch64MachineBlock &block : function.get_blocks()) {
        for (const AArch64MachineInstr &instruction : block.get_instructions()) {
            for (const AArch64MachineOperand &operand : instruction.get_operands()) {
                if (const auto *physical = operand.get_physical_reg_operand();
                    physical != nullptr && is_general_kind(physical->kind)) {
                    regs.insert(physical->reg_number);
                }
                if (const auto *memory = operand.get_memory_address_operand();
                    memory != nullptr &&
                    memory->base_kind ==
                        AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg) {
                    regs.insert(memory->physical_reg);
                }
            }
        }
    }
    return regs;
}

std::optional<std::size_t>
find_block_index_by_label(const AArch64MachineFunction &function,
                          std::string_view label) {
    const auto &blocks = function.get_blocks();
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index].get_label() == label) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::string>
extract_single_branch_target_label(const AArch64MachineInstr &instruction) {
    switch (instruction.get_opcode()) {
    case AArch64MachineOpcode::Branch:
        if (!instruction.get_operands().empty()) {
            if (const auto *label = instruction.get_operands()[0].get_label_operand();
                label != nullptr) {
                return label->label_text;
            }
        }
        break;
    case AArch64MachineOpcode::BranchConditional:
        if (!instruction.get_operands().empty()) {
            if (const auto *label = instruction.get_operands()[0].get_label_operand();
                label != nullptr) {
                return label->label_text;
            }
        }
        break;
    case AArch64MachineOpcode::CompareBranchZero:
    case AArch64MachineOpcode::CompareBranchNonZero:
        if (instruction.get_operands().size() > 1) {
            if (const auto *label = instruction.get_operands()[1].get_label_operand();
                label != nullptr) {
                return label->label_text;
            }
        }
        break;
    default:
        break;
    }
    return std::nullopt;
}

BlockTerminatorInfo analyze_block_terminators(const AArch64MachineBlock &block) {
    const auto &instructions = block.get_instructions();
    BlockTerminatorInfo info{.first_terminator_index = instructions.size()};
    if (instructions.empty()) {
        return info;
    }

    const AArch64MachineInstr &last = instructions.back();
    if (instructions.size() >= 2) {
        const AArch64MachineInstr &second_last = instructions[instructions.size() - 2];
        if (is_conditional_branch_instruction(second_last) &&
            last.get_opcode() == AArch64MachineOpcode::Branch) {
            info.first_terminator_index = instructions.size() - 2;
            info.conditional_target = extract_single_branch_target_label(second_last);
            info.unconditional_target = extract_single_branch_target_label(last);
            return info;
        }
    }

    if (is_conditional_branch_instruction(last)) {
        info.first_terminator_index = instructions.size() - 1;
        info.conditional_target = extract_single_branch_target_label(last);
        return info;
    }

    if (last.get_opcode() == AArch64MachineOpcode::Branch) {
        info.first_terminator_index = instructions.size() - 1;
        info.unconditional_target = extract_single_branch_target_label(last);
        return info;
    }

    return info;
}

std::vector<std::vector<std::size_t>>
collect_all_block_predecessors(const AArch64MachineFunction &function,
                               std::vector<BlockTerminatorInfo> &terminators) {
    const auto &blocks = function.get_blocks();
    terminators.clear();
    terminators.reserve(blocks.size());

    std::unordered_map<std::string, std::size_t> label_to_index;
    label_to_index.reserve(blocks.size());
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        label_to_index.emplace(blocks[index].get_label(), index);
        terminators.push_back(analyze_block_terminators(blocks[index]));
    }

    std::vector<std::vector<std::size_t>> predecessors(blocks.size());
    const auto add_edge = [&](std::size_t predecessor,
                              const std::optional<std::string> &target) {
        if (!target.has_value()) {
            return;
        }
        const auto it = label_to_index.find(*target);
        if (it == label_to_index.end()) {
            return;
        }
        auto &block_predecessors = predecessors[it->second];
        if (!block_predecessors.empty() &&
            block_predecessors.back() == predecessor) {
            return;
        }
        block_predecessors.push_back(predecessor);
    };

    for (std::size_t index = 0; index < terminators.size(); ++index) {
        add_edge(index, terminators[index].conditional_target);
        add_edge(index, terminators[index].unconditional_target);
    }
    return predecessors;
}

bool terminator_targets_block(const BlockTerminatorInfo &info,
                              std::string_view label) {
    return (info.conditional_target.has_value() && *info.conditional_target == label) ||
           (info.unconditional_target.has_value() &&
            *info.unconditional_target == label);
}

bool thread_unconditional_phi_edge_blocks(AArch64MachineFunction &function) {
    auto &blocks = function.get_blocks();
    std::vector<BlockTerminatorInfo> terminators;
    const std::vector<std::vector<std::size_t>> predecessors_by_block =
        collect_all_block_predecessors(function, terminators);
    std::vector<bool> modified_blocks(blocks.size(), false);
    bool changed = false;

    for (std::size_t block_index = 1; block_index < blocks.size(); ++block_index) {
        AArch64MachineBlock &edge_block = blocks[block_index];
        if (modified_blocks[block_index]) {
            continue;
        }
        if (!is_phi_edge_block_label(edge_block.get_label())) {
            continue;
        }
        const auto &edge_instructions = edge_block.get_instructions();
        if (edge_instructions.empty() ||
            edge_instructions.back().get_opcode() != AArch64MachineOpcode::Branch) {
            continue;
        }
        const auto target_label =
            extract_single_branch_target_label(edge_instructions.back());
        if (!target_label.has_value()) {
            continue;
        }
        bool all_simple_copies = true;
        for (std::size_t index = 0; index + 1 < edge_instructions.size(); ++index) {
            if (!is_simple_edge_copy_instruction(edge_instructions[index])) {
                all_simple_copies = false;
                break;
            }
        }
        if (!all_simple_copies) {
            continue;
        }

        const std::vector<std::size_t> &predecessors =
            predecessors_by_block[block_index];
        if (predecessors.size() != 1) {
            continue;
        }
        const std::size_t predecessor_index = predecessors.front();
        if (predecessor_index >= blocks.size() || predecessor_index == block_index ||
            modified_blocks[predecessor_index]) {
            continue;
        }
        AArch64MachineBlock &predecessor = blocks[predecessor_index];
        auto &predecessor_instructions = predecessor.get_instructions();
        if (predecessor_instructions.empty()) {
            continue;
        }
        const BlockTerminatorInfo predecessor_terminator =
            terminators[predecessor_index];
        if (predecessor_terminator.conditional_target.has_value() ||
            predecessor_terminator.unconditional_target != edge_block.get_label() ||
            predecessor_terminator.first_terminator_index + 1 !=
                predecessor_instructions.size()) {
            continue;
        }

        predecessor_instructions.erase(
            predecessor_instructions.begin() +
            static_cast<std::ptrdiff_t>(
                predecessor_terminator.first_terminator_index));
        predecessor_instructions.insert(
            predecessor_instructions.end(), edge_instructions.begin(),
            edge_instructions.end() - 1);
        predecessor_instructions.push_back(AArch64MachineInstr(
            AArch64MachineOpcode::Branch,
            {AArch64MachineOperand::label(*target_label)}));
        modified_blocks[predecessor_index] = true;
        changed = true;
    }
    return changed;
}

bool instruction_writes_frame_offset(const AArch64MachineInstr &instruction,
                                     long long offset) {
    const auto is_store_mnemonic = [&instruction]() {
        const std::string &mnemonic = instruction.get_mnemonic();
        return mnemonic == "str" || mnemonic == "stur" || mnemonic == "strb" ||
               mnemonic == "sturb" || mnemonic == "strh" || mnemonic == "sturh" ||
               mnemonic == "stp";
    };
    switch (instruction.get_opcode()) {
    case AArch64MachineOpcode::Store:
    case AArch64MachineOpcode::StoreByte:
    case AArch64MachineOpcode::StoreHalf:
    case AArch64MachineOpcode::StoreUnscaled:
    case AArch64MachineOpcode::StoreByteUnscaled:
    case AArch64MachineOpcode::StoreHalfUnscaled:
    case AArch64MachineOpcode::StorePair:
        break;
    default:
        if (!is_store_mnemonic()) {
            return false;
        }
        break;
    }

    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        const auto *memory = operand.get_memory_address_operand();
        if (memory == nullptr ||
            memory->base_kind !=
                AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg ||
            memory->physical_reg != static_cast<unsigned>(AArch64PhysicalReg::X29)) {
            continue;
        }
        const auto immediate_offset = memory->get_immediate_offset();
        if (immediate_offset.has_value() && *immediate_offset == offset) {
            return true;
        }
    }
    return false;
}

std::optional<unsigned>
choose_free_loop_cache_general_reg(const AArch64MachineFunction &function) {
    const auto used_regs = collect_used_general_physical_regs(function);
    for (unsigned reg : kAArch64SpillScratchGeneralPhysicalRegs) {
        if (used_regs.find(reg) == used_regs.end()) {
            return reg;
        }
    }
    for (unsigned reg : kAArch64CalleeSavedAllocatableGeneralPhysicalRegs) {
        if (used_regs.find(reg) == used_regs.end()) {
            return reg;
        }
    }
    return std::nullopt;
}

bool remove_redundant_copies(AArch64MachineFunction &function) {
    const auto use_counts = collect_virtual_use_counts(function);

    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t index = 0; index < instructions.size(); ++index) {
            if (is_redundant_vector_copy(function, instructions[index])) {
                instructions.erase(instructions.begin() +
                                   static_cast<std::ptrdiff_t>(index));
                return true;
            }

            PlainCopyInfo copy;
            if (!extract_plain_virtual_copy(instructions[index], copy)) {
                continue;
            }

            const auto dst_physical =
                function.get_physical_reg_for_virtual(copy.dst.get_id());
            const auto src_physical =
                function.get_physical_reg_for_virtual(copy.src.get_id());
            if (same_virtual_reg(copy.dst, copy.src) ||
                (dst_physical.has_value() && src_physical.has_value() &&
                 *dst_physical == *src_physical)) {
                instructions.erase(instructions.begin() +
                                   static_cast<std::ptrdiff_t>(index));
                return true;
            }

            // Do not rewrite a following instruction to bypass this copy after
            // register allocation. Phi edge blocks can rely on the copy value
            // remaining live across a branch even when the immediate next use
            // appears locally replaceable.
        }
    }

    return false;
}

bool fold_redefining_physical_copies(AArch64MachineFunction &function) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t index = 0; index < instructions.size(); ++index) {
            PlainPhysicalCopyInfo copy;
            if (!extract_plain_physical_copy(instructions[index], copy)) {
                continue;
            }
            if (copy.dst_reg == copy.src_reg) {
                instructions.erase(instructions.begin() +
                                   static_cast<std::ptrdiff_t>(index));
                return true;
            }

            std::size_t probe = index + 1;
            const std::size_t probe_limit =
                index + 4 < instructions.size() ? index + 4 : instructions.size();
            for (; probe < probe_limit; ++probe) {
                const AArch64MachineInstr &candidate = instructions[probe];
                const auto descriptor = describe_aarch64_machine_opcode(candidate.get_opcode());
                if (descriptor.is_branch || descriptor.is_call || descriptor.is_directive) {
                    break;
                }
                if (instruction_mentions_physical_reg(candidate, copy.src_reg,
                                                      copy.bank)) {
                    break;
                }
                if (!instruction_mentions_physical_reg(candidate, copy.dst_reg,
                                                       copy.bank)) {
                    continue;
                }
                if (!instruction_explicitly_defines_physical_reg(candidate, copy.dst_reg,
                                                                 copy.bank)) {
                    break;
                }
                if (count_instruction_source_uses_physical_reg(candidate, copy.dst_reg,
                                                               copy.bank) == 0) {
                    break;
                }
                const auto rewritten = rewrite_instruction_source_physical_use(
                    candidate, copy.dst_reg, copy.src_reg);
                if (!rewritten.has_value()) {
                    break;
                }
                instructions[probe] = *rewritten;
                instructions.erase(instructions.begin() +
                                   static_cast<std::ptrdiff_t>(index));
                return true;
            }
        }
    }
    return false;
}

bool hoist_loop_invariant_frame_slot_loads(AArch64MachineFunction &function) {
    auto &blocks = function.get_blocks();
    std::size_t instruction_count = 0;
    for (const AArch64MachineBlock &block : blocks) {
        instruction_count += block.get_instructions().size();
    }
    if (blocks.size() > 512 || instruction_count > 4096) {
        return false;
    }

    std::unordered_map<std::string, std::size_t> block_index_by_label;
    block_index_by_label.reserve(blocks.size());
    std::vector<BlockTerminatorInfo> terminators;
    terminators.reserve(blocks.size());
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        block_index_by_label.emplace(blocks[index].get_label(), index);
        terminators.push_back(analyze_block_terminators(blocks[index]));
    }

    std::vector<std::vector<std::size_t>> predecessors_by_block(blocks.size());
    for (std::size_t index = 0; index < terminators.size(); ++index) {
        const BlockTerminatorInfo &terminator = terminators[index];
        for (const std::optional<std::string> *target :
             {&terminator.conditional_target, &terminator.unconditional_target}) {
            if (!target->has_value()) {
                continue;
            }
            const auto found = block_index_by_label.find(**target);
            if (found != block_index_by_label.end()) {
                predecessors_by_block[found->second].push_back(index);
            }
        }
    }

    for (std::size_t body_index = 0; body_index < blocks.size(); ++body_index) {
        auto &body = blocks[body_index];
        auto &body_instructions = body.get_instructions();
        if (body_instructions.size() < 2) {
            continue;
        }
        const BlockTerminatorInfo &body_terminator = terminators[body_index];
        if (body_terminator.first_terminator_index >= body_instructions.size()) {
            continue;
        }
        if (!body_terminator.conditional_target.has_value() &&
            !body_terminator.unconditional_target.has_value()) {
            continue;
        }

        std::optional<std::size_t> backedge_index;
        for (const std::optional<std::string> *target :
             {&body_terminator.conditional_target, &body_terminator.unconditional_target}) {
            if (!target->has_value()) {
                continue;
            }
            const auto successor_index = block_index_by_label.find(**target);
            if (successor_index == block_index_by_label.end()) {
                continue;
            }
            const BlockTerminatorInfo &successor_terminator =
                terminators[successor_index->second];
            if (successor_terminator.conditional_target.has_value()) {
                continue;
            }
            if (successor_terminator.unconditional_target == body.get_label()) {
                backedge_index = successor_index->second;
                break;
            }
        }
        if (!backedge_index.has_value()) {
            continue;
        }

        const auto &backedge_block = blocks[*backedge_index];
        const auto &backedge_instructions = backedge_block.get_instructions();
        if (backedge_instructions.empty()) {
            continue;
        }
        std::optional<std::size_t> preheader_index;
        for (std::size_t predecessor : predecessors_by_block[body_index]) {
            if (predecessor != *backedge_index) {
                preheader_index = predecessor;
                break;
            }
        }
        if (!preheader_index.has_value()) {
            continue;
        }
        auto &preheader = blocks[*preheader_index];
        auto &preheader_instructions = preheader.get_instructions();
        if (preheader_instructions.empty()) {
            continue;
        }
        const BlockTerminatorInfo &preheader_terminator =
            terminators[*preheader_index];
        if (!terminator_targets_block(preheader_terminator, body.get_label())) {
            continue;
        }

        for (std::size_t load_index = 0;
             load_index + 1 < body_terminator.first_terminator_index;
             ++load_index) {
            FrameSlotLoadCandidate load_info;
            if (!extract_frame_slot_load_candidate(body_instructions[load_index],
                                                   load_info)) {
                continue;
            }
            if (instruction_writes_frame_offset(body_instructions[load_index + 1],
                                                load_info.offset)) {
                continue;
            }
            bool slot_written_in_loop = false;
            for (const AArch64MachineInstr &instruction : body_instructions) {
                if (instruction_writes_frame_offset(instruction, load_info.offset)) {
                    slot_written_in_loop = true;
                    break;
                }
            }
            if (slot_written_in_loop) {
                continue;
            }
            for (const AArch64MachineInstr &instruction : backedge_instructions) {
                if (instruction_writes_frame_offset(instruction, load_info.offset)) {
                    slot_written_in_loop = true;
                    break;
                }
            }
            if (slot_written_in_loop) {
                continue;
            }

            const AArch64MachineInstr &use_instruction =
                body_instructions[load_index + 1];
            const bool supported_use =
                use_instruction.get_opcode() == AArch64MachineOpcode::SignedDiv ||
                use_instruction.get_opcode() == AArch64MachineOpcode::UnsignedDiv;
            std::size_t use_count = 0;
            if (load_info.value_kind == FrameSlotLoadCandidate::ValueKind::Virtual) {
                use_count = count_instruction_uses(use_instruction, load_info.virtual_dst);
            } else {
                use_count = count_instruction_uses_physical_reg(
                    use_instruction, load_info.physical_dst, AArch64RegBank::General);
            }
            if (!supported_use || use_count != 1) {
                continue;
            }

            const auto cache_reg = choose_free_loop_cache_general_reg(function);
            if (!cache_reg.has_value()) {
                continue;
            }
            std::optional<AArch64MachineInstr> rewritten;
            if (load_info.value_kind == FrameSlotLoadCandidate::ValueKind::Virtual) {
                rewritten = rewrite_instruction_virtual_use_to_physical(
                    use_instruction, load_info.virtual_dst, *cache_reg);
            } else {
                rewritten = rewrite_instruction_physical_use(
                    use_instruction, load_info.physical_dst, *cache_reg);
            }
            if (!rewritten.has_value()) {
                continue;
            }
            function.get_frame_info().mark_saved_physical_reg(*cache_reg);
            preheader_instructions.insert(
                preheader_instructions.begin() +
                    static_cast<std::ptrdiff_t>(preheader_terminator.first_terminator_index),
                AArch64MachineInstr(
                    AArch64MachineOpcode::Load,
                    {AArch64MachineOperand::physical_reg(*cache_reg,
                                                        load_info.reg_kind),
                     AArch64MachineOperand::memory_address_physical_reg(
                         static_cast<unsigned>(AArch64PhysicalReg::X29),
                         load_info.offset)}));
            body_instructions[load_index + 1] = *rewritten;
            body_instructions.erase(body_instructions.begin() +
                                    static_cast<std::ptrdiff_t>(load_index));
            return true;
        }
    }

    return false;
}

bool fold_direct_multiply_add_accumulates(AArch64MachineFunction &function) {
    const auto use_counts = collect_virtual_use_counts(function);

    for (AArch64MachineBlock &block : function.get_blocks()) {
        auto &instructions = block.get_instructions();
        for (std::size_t add_index = 1; add_index < instructions.size(); ++add_index) {
            MultiplyInfo mul_info;
            PlainAddInfo add_info;
            if (!extract_integer_multiply(instructions[add_index - 1], mul_info) ||
                !extract_plain_add(instructions[add_index], add_info) ||
                count_instruction_uses(instructions[add_index], mul_info.dst) != 1) {
                continue;
            }

            std::size_t expected_mul_uses = 1;
            if (same_virtual_reg(mul_info.lhs, mul_info.dst)) {
                ++expected_mul_uses;
            }
            if (same_virtual_reg(mul_info.rhs, mul_info.dst)) {
                ++expected_mul_uses;
            }
            const auto use_it = use_counts.find(mul_info.dst.get_id());
            const std::size_t actual_mul_uses =
                use_it != use_counts.end() ? use_it->second : 0;
            if (actual_mul_uses != expected_mul_uses) {
                continue;
            }

            const bool mul_used_as_lhs = same_virtual_reg(add_info.lhs, mul_info.dst);
            const bool mul_used_as_rhs = same_virtual_reg(add_info.rhs, mul_info.dst);
            if (mul_used_as_lhs == mul_used_as_rhs) {
                continue;
            }
            const AArch64VirtualReg addend_reg =
                mul_used_as_lhs ? add_info.rhs : add_info.lhs;

            instructions[add_index] = AArch64MachineInstr(
                AArch64MachineOpcode::MultiplyAdd,
                {def_vreg_operand(add_info.dst), use_vreg_operand(mul_info.lhs),
                 use_vreg_operand(mul_info.rhs), use_vreg_operand(addend_reg)},
                instructions[add_index].get_flags(),
                instructions[add_index].get_debug_location(),
                instructions[add_index].get_implicit_defs(),
                instructions[add_index].get_implicit_uses(),
                instructions[add_index].get_call_clobber_mask());
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(add_index - 1));
            return true;
        }
    }

    return false;
}

bool fold_multiply_add_sequences(AArch64MachineFunction &function) {
    const auto use_counts = collect_virtual_use_counts(function);

    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> &instructions = block.get_instructions();
        for (std::size_t add_index = 4; add_index < instructions.size(); ++add_index) {
            AccumulateAddInfo add_info;
            PlainCopyInfo addend_copy;
            PlainCopyInfo multiplicand_copy;
            MoveWideZeroInfo scale_info;
            MultiplyInfo mul_info;
            if (!extract_accumulate_add(instructions[add_index], add_info) ||
                !extract_plain_virtual_copy(instructions[add_index - 4], addend_copy) ||
                !extract_plain_virtual_copy(instructions[add_index - 3],
                                            multiplicand_copy) ||
                !extract_move_wide_zero(instructions[add_index - 2], scale_info) ||
                !extract_integer_multiply(instructions[add_index - 1], mul_info)) {
                continue;
            }
            if (!same_virtual_reg(addend_copy.dst, add_info.dst) ||
                !same_virtual_reg(multiplicand_copy.dst, mul_info.dst) ||
                !same_virtual_reg(mul_info.lhs, multiplicand_copy.dst) ||
                !same_virtual_reg(mul_info.rhs, scale_info.dst) ||
                !same_virtual_reg(add_info.accumulation, mul_info.dst)) {
                continue;
            }
            const auto mul_use_it = use_counts.find(mul_info.dst.get_id());
            if (mul_use_it != use_counts.end() && mul_use_it->second != 1) {
                continue;
            }

            instructions[add_index] = AArch64MachineInstr(
                AArch64MachineOpcode::MultiplyAdd,
                {def_vreg_operand(add_info.dst),
                 use_vreg_operand(multiplicand_copy.src),
                 use_vreg_operand(scale_info.dst),
                 use_vreg_operand(addend_copy.src)},
                instructions[add_index].get_flags(),
                instructions[add_index].get_debug_location(),
                instructions[add_index].get_implicit_defs(),
                instructions[add_index].get_implicit_uses(),
                instructions[add_index].get_call_clobber_mask());
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(add_index - 1));
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(add_index - 3));
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(add_index - 4));
            return true;
        }

        for (std::size_t add_index = 0; add_index < instructions.size(); ++add_index) {
            AccumulateAddInfo add_info;
            if (!extract_accumulate_add(instructions[add_index], add_info)) {
                continue;
            }

            for (std::size_t setup_count = 0; setup_count <= 1; ++setup_count) {
                if (add_index < 2 + setup_count) {
                    continue;
                }
                const std::size_t mul_index = add_index - 1;
                const std::size_t setup_index = add_index - 2;
                const std::size_t base_move_index = add_index - 2 - setup_count;
                MultiplyInfo mul_info;
                PlainCopyInfo addend_copy;
                if (!extract_integer_multiply(instructions[mul_index], mul_info) ||
                    !extract_plain_virtual_copy(instructions[base_move_index],
                                                addend_copy) ||
                    !same_virtual_reg(addend_copy.dst, add_info.dst) ||
                    !same_virtual_reg(add_info.accumulation, mul_info.dst)) {
                    continue;
                }

                PlainCopyInfo multiplicand_copy;
                const PlainCopyInfo *multiplicand_copy_ptr = nullptr;
                if (base_move_index + 1 < mul_index &&
                    extract_plain_virtual_copy(instructions[base_move_index + 1],
                                               multiplicand_copy) &&
                    same_virtual_reg(multiplicand_copy.dst, mul_info.dst)) {
                    multiplicand_copy_ptr = &multiplicand_copy;
                }

                if (setup_count == 1) {
                    const AArch64MachineInstr &setup_instruction =
                        instructions[setup_index];
                    if (instruction_touches_virtual_reg(setup_instruction,
                                                        addend_copy.src) ||
                        instruction_touches_virtual_reg(setup_instruction,
                                                        add_info.dst) ||
                        instruction_touches_virtual_reg(setup_instruction,
                                                        mul_info.dst) ||
                        (multiplicand_copy_ptr != nullptr &&
                         instruction_touches_virtual_reg(
                             setup_instruction, multiplicand_copy_ptr->src))) {
                        continue;
                    }
                }

                AArch64VirtualReg multiplicand_reg;
                AArch64VirtualReg scale_reg;
                const bool mul_uses_dst_as_lhs =
                    same_virtual_reg(mul_info.lhs, mul_info.dst);
                const bool mul_uses_dst_as_rhs =
                    same_virtual_reg(mul_info.rhs, mul_info.dst);
                const std::size_t expected_mul_uses =
                    (mul_uses_dst_as_lhs || mul_uses_dst_as_rhs) ? 2U : 1U;
                const auto use_it = use_counts.find(mul_info.dst.get_id());
                const std::size_t actual_mul_uses =
                    use_it != use_counts.end() ? use_it->second : 0;
                if (actual_mul_uses != expected_mul_uses) {
                    continue;
                }

                if (multiplicand_copy_ptr != nullptr) {
                    if (!mul_uses_dst_as_lhs && !mul_uses_dst_as_rhs) {
                        continue;
                    }
                    multiplicand_reg = multiplicand_copy_ptr->src;
                    scale_reg = mul_uses_dst_as_lhs ? mul_info.rhs : mul_info.lhs;
                } else if (mul_uses_dst_as_lhs || mul_uses_dst_as_rhs) {
                    multiplicand_reg = mul_info.dst;
                    scale_reg = mul_uses_dst_as_lhs ? mul_info.rhs : mul_info.lhs;
                } else {
                    multiplicand_reg = mul_info.lhs;
                    scale_reg = mul_info.rhs;
                }

                instructions[add_index] = AArch64MachineInstr(
                    AArch64MachineOpcode::MultiplyAdd,
                    {def_vreg_operand(add_info.dst), use_vreg_operand(multiplicand_reg),
                     use_vreg_operand(scale_reg), use_vreg_operand(addend_copy.src)},
                    instructions[add_index].get_flags(),
                    instructions[add_index].get_debug_location(),
                    instructions[add_index].get_implicit_defs(),
                    instructions[add_index].get_implicit_uses(),
                    instructions[add_index].get_call_clobber_mask());

                instructions.erase(instructions.begin() +
                                   static_cast<std::ptrdiff_t>(mul_index));
                if (multiplicand_copy_ptr != nullptr) {
                    instructions.erase(instructions.begin() +
                                       static_cast<std::ptrdiff_t>(base_move_index + 1));
                }
                instructions.erase(instructions.begin() +
                                   static_cast<std::ptrdiff_t>(base_move_index));
                return true;
            }
        }
    }

    return false;
}

bool remove_redundant_vector_setup(AArch64MachineFunction &function) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        auto &instructions = block.get_instructions();
        for (std::size_t index = 0; index + 2 < instructions.size(); ++index) {
            const AArch64MachineInstr &first = instructions[index];
            const AArch64MachineInstr &middle = instructions[index + 1];
            const AArch64MachineInstr &second = instructions[index + 2];
            if (!is_redundant_vector_setup_candidate(first) ||
                !is_store_using_v0(middle) ||
                first.get_mnemonic() != second.get_mnemonic() ||
                !operands_render_equal(first, second, function)) {
                continue;
            }
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(index + 2));
            return true;
        }
    }
    return false;
}

bool remove_repeated_vector_scalar_setup(AArch64MachineFunction &function) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        auto &instructions = block.get_instructions();
        for (std::size_t index = 0; index + 7 < instructions.size(); ++index) {
            const AArch64MachineInstr &movz0 = instructions[index];
            const AArch64MachineInstr &movk0 = instructions[index + 1];
            const AArch64MachineInstr &dup0 = instructions[index + 2];
            const AArch64MachineInstr &store0 = instructions[index + 3];
            const AArch64MachineInstr &movz1 = instructions[index + 4];
            const AArch64MachineInstr &movk1 = instructions[index + 5];
            const AArch64MachineInstr &dup1 = instructions[index + 6];
            const AArch64MachineInstr &store1 = instructions[index + 7];
            if (movz0.get_mnemonic() != "movz" || movk0.get_mnemonic() != "movk" ||
                dup0.get_mnemonic() != "dup" || !is_store_using_v0(store0) ||
                movz1.get_mnemonic() != "movz" || movk1.get_mnemonic() != "movk" ||
                dup1.get_mnemonic() != "dup" || !is_store_using_v0(store1) ||
                !operands_render_equal(movz0, movz1, function) ||
                !operands_render_equal(movk0, movk1, function) ||
                !operands_render_equal(dup0, dup1, function)) {
                continue;
            }
            instructions.erase(instructions.begin() +
                                   static_cast<std::ptrdiff_t>(index + 4),
                               instructions.begin() +
                                   static_cast<std::ptrdiff_t>(index + 7));
            return true;
        }
    }
    return false;
}

bool fold_indexed_memory_access_address_adds(AArch64MachineFunction &function) {
    const std::unordered_map<std::size_t, std::size_t> use_counts =
        collect_virtual_use_counts(function);
    for (AArch64MachineBlock &block : function.get_blocks()) {
        auto &instructions = block.get_instructions();
        for (std::size_t index = 0; index + 1 < instructions.size(); ++index) {
            IndexedAddressAddInfo add_info;
            if (!extract_indexed_address_add(function, instructions[index],
                                             add_info)) {
                continue;
            }

            std::size_t memory_operand_index = 0;
            AArch64MachineInstr &memory_instruction = instructions[index + 1];
            if (!load_store_uses_zero_offset_temp_base(memory_instruction,
                                                       add_info.temp_virtual,
                                                       add_info.temp_reg,
                                                       memory_operand_index)) {
                continue;
            }
            const std::optional<unsigned> required_shift =
                scaled_register_shift_for_memory_access(memory_instruction);
            if (!required_shift.has_value() ||
                add_info.shift_amount != *required_shift) {
                continue;
            }
            const auto temp_use_it = use_counts.find(add_info.temp_virtual.get_id());
            if (temp_use_it == use_counts.end() || temp_use_it->second != 1) {
                continue;
            }

            memory_instruction.get_operands()[memory_operand_index] =
                AArch64MachineOperand::memory_address_physical_reg_indexed(
                    add_info.base_reg, add_info.index_reg,
                    AArch64VirtualRegKind::General64, AArch64ShiftKind::Lsl,
                    add_info.shift_amount);
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(index));
            return true;
        }
    }
    return false;
}

bool remove_redundant_frame_slot_float_reloads(AArch64MachineFunction &function) {
    struct CachedLoad {
        unsigned reg_number = 0;
        AArch64VirtualRegKind kind = AArch64VirtualRegKind::Float128;
        FixedFrameSlotAddress slot;
    };

    for (AArch64MachineBlock &block : function.get_blocks()) {
        auto &instructions = block.get_instructions();
        std::vector<CachedLoad> cached_loads;
        for (std::size_t index = 0; index < instructions.size(); ++index) {
            FixedFrameSlotFloatLoadInfo load_info;
            if (extract_fixed_frame_slot_float_load(function, instructions[index],
                                                    load_info)) {
                const auto cached = std::find_if(
                    cached_loads.begin(), cached_loads.end(),
                    [&load_info](const CachedLoad &candidate) {
                        return candidate.reg_number == load_info.dst_reg &&
                               candidate.kind == load_info.kind &&
                               same_fixed_frame_slot(candidate.slot,
                                                     load_info.slot);
                    });
                if (cached != cached_loads.end()) {
                    instructions.erase(instructions.begin() +
                                       static_cast<std::ptrdiff_t>(index));
                    return true;
                }

                cached_loads.erase(
                    std::remove_if(cached_loads.begin(), cached_loads.end(),
                                   [&load_info](const CachedLoad &candidate) {
                                       return candidate.reg_number ==
                                              load_info.dst_reg;
                                   }),
                    cached_loads.end());
                cached_loads.push_back(
                    CachedLoad{load_info.dst_reg, load_info.kind, load_info.slot});
                continue;
            }

            const auto descriptor =
                describe_aarch64_machine_opcode(instructions[index].get_opcode());
            if (descriptor.is_call || descriptor.is_branch) {
                cached_loads.clear();
                continue;
            }

            cached_loads.erase(
                std::remove_if(
                    cached_loads.begin(), cached_loads.end(),
                    [&function, &instruction = instructions[index]](
                        const CachedLoad &cached) {
                        if (instruction_defines_float_physical_reg(
                                function, instruction, cached.reg_number)) {
                            return true;
                        }
                        if (instruction_writes_frame_offset(instruction,
                                                            cached.slot.offset)) {
                            return true;
                        }
                        return false;
                    }),
                cached_loads.end());
        }
    }
    return false;
}

bool fold_vector_copy_into_edge_store(AArch64MachineFunction &function) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        if (!is_phi_edge_block_label(block.get_label())) {
            continue;
        }
        auto &instructions = block.get_instructions();
        for (std::size_t index = 0; index + 2 < instructions.size(); ++index) {
            AssignedVectorRegInfo copy_dst;
            AssignedVectorRegInfo copy_src;
            if (!extract_full_vector_copy(function, instructions[index], copy_dst,
                                          copy_src) ||
                instructions[index + 1].get_opcode() != AArch64MachineOpcode::Store ||
                instructions[index + 1].get_operands().size() != 2) {
                continue;
            }

            const auto descriptor =
                describe_aarch64_machine_opcode(instructions[index + 2].get_opcode());
            if (!descriptor.is_branch) {
                continue;
            }

            const auto *memory =
                instructions[index + 1].get_operands()[1].get_memory_address_operand();
            if (memory == nullptr) {
                continue;
            }

            bool store_uses_copy_dst = false;
            if (const auto *physical =
                    instructions[index + 1].get_operands()[0]
                        .get_physical_reg_operand();
                physical != nullptr && !is_general_kind(physical->kind) &&
                physical->reg_number == copy_dst.reg_number) {
                store_uses_copy_dst = true;
            } else if (const auto *vector =
                           instructions[index + 1].get_operands()[0]
                               .get_vector_reg_operand();
                       vector != nullptr) {
                const auto store_src = resolve_assigned_vector_reg(
                    function, instructions[index + 1].get_operands()[0]);
                store_uses_copy_dst =
                    store_src.has_value() &&
                    store_src->reg_number == copy_dst.reg_number;
            }
            if (!store_uses_copy_dst) {
                continue;
            }

            instructions[index + 1].get_operands()[0] =
                AArch64MachineOperand::physical_reg(
                    copy_src.reg_number, AArch64VirtualRegKind::Float128);
            instructions.erase(instructions.begin() +
                               static_cast<std::ptrdiff_t>(index));
            return true;
        }
    }
    return false;
}

} // namespace

void AArch64PostRaPeepholePass::run(AArch64MachineFunction &function) const {
    struct Transform {
        const char *name = nullptr;
        bool (*apply)(AArch64MachineFunction &) = nullptr;
    };

    static constexpr Transform kTransforms[] = {
        {"repair_parallel_phi_edge_copies", repair_parallel_phi_edge_copies},
        {"thread_unconditional_phi_edge_blocks",
         thread_unconditional_phi_edge_blocks},
        {"canonicalize_frame_temp_memory_accesses",
         canonicalize_frame_temp_memory_accesses},
        {"canonicalize_materialized_frame_temp_memory_accesses",
         canonicalize_materialized_frame_temp_memory_accesses},
        {"remove_redundant_zero_extend_slot_store",
         remove_redundant_zero_extend_slot_store},
        {"collapse_repeated_frame_slot_mask_store_clusters",
         collapse_repeated_frame_slot_mask_store_clusters},
        {"fold_move_wide_constant_adds", fold_move_wide_constant_adds},
        {"legalize_zero_register_add_sub_immediates",
         legalize_zero_register_add_sub_immediates},
        {"hoist_loop_invariant_frame_slot_loads",
         hoist_loop_invariant_frame_slot_loads},
        {"fold_redefining_physical_copies", fold_redefining_physical_copies},
        {"remove_redundant_copies", remove_redundant_copies},
        {"fold_direct_multiply_add_accumulates",
         fold_direct_multiply_add_accumulates},
        {"fold_multiply_add_sequences", fold_multiply_add_sequences},
        {"fold_indexed_memory_access_address_adds",
         fold_indexed_memory_access_address_adds},
        {"remove_redundant_frame_slot_float_reloads",
         remove_redundant_frame_slot_float_reloads},
        {"fold_vector_copy_into_edge_store", fold_vector_copy_into_edge_store},
        {"remove_redundant_vector_setup", remove_redundant_vector_setup},
        {"remove_repeated_vector_scalar_setup",
         remove_repeated_vector_scalar_setup},
    };

    std::size_t instruction_count = 0;
    for (const AArch64MachineBlock &block : function.get_blocks()) {
        instruction_count += block.get_instructions().size();
    }
    const bool skip_expensive_transforms =
        function.get_blocks().size() > 512 || instruction_count > 4096;
    const auto is_expensive_transform = [](const char *name) {
        const std::string_view transform_name(name == nullptr ? "" : name);
        return transform_name == "hoist_loop_invariant_frame_slot_loads" ||
               transform_name == "remove_redundant_copies" ||
               transform_name == "fold_direct_multiply_add_accumulates" ||
               transform_name == "fold_multiply_add_sequences" ||
               transform_name == "fold_indexed_memory_access_address_adds";
    };
    const std::size_t max_iterations =
        std::max<std::size_t>(1024, instruction_count * 64U);
    const bool trace_enabled =
        std::getenv("SYSYCC_AARCH64_TRACE_POST_RA_PEEPHOLE") != nullptr;

    for (std::size_t iteration = 0; iteration < max_iterations; ++iteration) {
        bool changed = false;
        for (const Transform &transform : kTransforms) {
            if (skip_expensive_transforms &&
                is_expensive_transform(transform.name)) {
                continue;
            }
            if (!transform.apply(function)) {
                continue;
            }
            if (trace_enabled) {
                std::fprintf(stderr, "aarch64-post-ra-peephole[%zu]: %s\n",
                             iteration, transform.name);
            }
            changed = true;
            break;
        }
        if (!changed) {
            return;
        }
    }

    if (trace_enabled) {
        std::fprintf(stderr,
                     "aarch64-post-ra-peephole: reached iteration cap for %s\n",
                     function.get_name().c_str());
    }
}

} // namespace sysycc
