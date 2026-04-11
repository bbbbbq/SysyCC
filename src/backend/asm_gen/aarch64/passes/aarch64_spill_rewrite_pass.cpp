#include "backend/asm_gen/aarch64/passes/aarch64_spill_rewrite_pass.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_target_constraints.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_frame_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

enum class AArch64SpillOperandRole : unsigned char {
    ValueUse = 1U << 0U,
    ValueDef = 1U << 1U,
    AddressBaseUse = 1U << 2U,
    AddressBaseDef = 1U << 3U,
};

using AArch64SpillOperandRoles = unsigned char;

struct AArch64SpillRewriteOperand {
    std::size_t virtual_reg_id = 0;
    AArch64VirtualRegKind kind = AArch64VirtualRegKind::General32;
    AArch64SpillOperandRoles roles = 0;
};

struct AArch64ScratchAssignment {
    std::size_t virtual_reg_id = 0;
    unsigned physical_reg = 0;
    AArch64VirtualRegKind kind = AArch64VirtualRegKind::General32;
    bool borrowed = false;
};

struct AArch64SpillRewriteStep {
    enum class Kind : unsigned char {
        LoadUse,
        EmitInstruction,
        StoreDef,
    };

    Kind kind = Kind::EmitInstruction;
    std::size_t virtual_reg_id = 0;
    unsigned physical_reg = 0;
    AArch64VirtualRegKind virtual_reg_kind = AArch64VirtualRegKind::General32;
    std::optional<AArch64MachineInstr> instruction;
};

struct AArch64InstructionRewritePlan {
    enum class AddressScratchStrategy : unsigned char {
        NotNeeded,
        Shared,
        SharedAfterRepair,
        StrictReserved,
    };

    enum class AddressScratchFailureStage : unsigned char {
        None,
        SharedAssignment,
        SharedRepair,
        StrictAssignment,
    };

    std::vector<AArch64SpillRewriteOperand> operands;
    std::vector<AArch64ScratchAssignment> assignments;
    bool needs_address_scratch = false;
    bool attempted_strict_address_retry = false;
    AddressScratchStrategy address_scratch_strategy =
        AddressScratchStrategy::NotNeeded;
    AddressScratchFailureStage address_scratch_failure_stage =
        AddressScratchFailureStage::None;
    std::vector<AArch64SpillRewriteStep> steps;
    std::string failure_reason;
};

struct AArch64BorrowedScratchState {
    unsigned physical_reg = 0;
    AArch64VirtualRegKind save_kind = AArch64VirtualRegKind::General64;
};

struct AArch64ScratchPool {
    std::vector<unsigned> regs;
    std::unordered_set<unsigned> borrowed_regs;
};

bool has_role(AArch64SpillOperandRoles roles, AArch64SpillOperandRole role) {
    return (roles & static_cast<AArch64SpillOperandRoles>(role)) != 0;
}

void add_role(AArch64SpillOperandRoles &roles, AArch64SpillOperandRole role) {
    roles |= static_cast<AArch64SpillOperandRoles>(role);
}

bool spill_slot_requires_address_scratch(std::size_t offset) {
    return offset > 255;
}

bool spill_operand_has_any_use(const AArch64SpillRewriteOperand &operand) {
    return has_role(operand.roles, AArch64SpillOperandRole::ValueUse) ||
           has_role(operand.roles, AArch64SpillOperandRole::AddressBaseUse);
}

bool spill_operand_has_any_def(const AArch64SpillRewriteOperand &operand) {
    return has_role(operand.roles, AArch64SpillOperandRole::ValueDef) ||
           has_role(operand.roles, AArch64SpillOperandRole::AddressBaseDef);
}

bool spill_operand_is_floating(const AArch64SpillRewriteOperand &operand) {
    return AArch64VirtualReg(operand.virtual_reg_id, operand.kind).is_floating_point();
}

bool memory_address_writes_back_base(
    const AArch64MachineMemoryAddressOperand &memory) {
    return memory.address_mode !=
           AArch64MachineMemoryAddressOperand::AddressMode::Offset;
}

bool is_spilled_virtual_reg(const AArch64VirtualReg &reg,
                            const AArch64MachineFunction &function) {
    return !function.get_physical_reg_for_virtual(reg.get_id()).has_value() &&
           function.get_frame_info().get_virtual_reg_spill_offset(reg.get_id()).has_value();
}

AArch64SpillRewriteOperand *
find_or_append_spill_operand(AArch64InstructionRewritePlan &plan,
                             std::unordered_map<std::size_t, std::size_t> &operand_indices,
                             const AArch64VirtualReg &reg) {
    const auto existing = operand_indices.find(reg.get_id());
    if (existing != operand_indices.end()) {
        return &plan.operands[existing->second];
    }
    operand_indices.emplace(reg.get_id(), plan.operands.size());
    plan.operands.push_back(AArch64SpillRewriteOperand{
        reg.get_id(),
        reg.get_kind(),
        0});
    return &plan.operands.back();
}

void record_spilled_operand_roles(
    AArch64InstructionRewritePlan &plan,
    std::unordered_map<std::size_t, std::size_t> &operand_indices,
    const AArch64MachineOperand &operand, const AArch64MachineFunction &function) {
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        if (!is_spilled_virtual_reg(virtual_reg->reg, function)) {
            return;
        }
        AArch64SpillRewriteOperand *entry =
            find_or_append_spill_operand(plan, operand_indices, virtual_reg->reg);
        add_role(entry->roles, virtual_reg->is_def ? AArch64SpillOperandRole::ValueDef
                                                   : AArch64SpillOperandRole::ValueUse);
        return;
    }

    const auto *memory = operand.get_memory_address_operand();
    if (memory == nullptr || memory->base_kind !=
                                AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg ||
        !is_spilled_virtual_reg(memory->virtual_reg, function)) {
        return;
    }

    AArch64SpillRewriteOperand *entry =
        find_or_append_spill_operand(plan, operand_indices, memory->virtual_reg);
    add_role(entry->roles, AArch64SpillOperandRole::AddressBaseUse);
    if (memory_address_writes_back_base(*memory)) {
        add_role(entry->roles, AArch64SpillOperandRole::AddressBaseDef);
    }
}

const AArch64ScratchAssignment *
find_spill_assignment(const AArch64InstructionRewritePlan &plan,
                      std::size_t virtual_reg_id) {
    for (const AArch64ScratchAssignment &assignment : plan.assignments) {
        if (assignment.virtual_reg_id == virtual_reg_id) {
            return &assignment;
        }
    }
    return nullptr;
}

const AArch64SpillRewriteOperand *
find_spill_operand(const AArch64InstructionRewritePlan &plan,
                   std::size_t virtual_reg_id) {
    for (const AArch64SpillRewriteOperand &operand : plan.operands) {
        if (operand.virtual_reg_id == virtual_reg_id) {
            return &operand;
        }
    }
    return nullptr;
}

bool spill_operand_can_share_address_scratch(
    const AArch64SpillRewriteOperand &operand,
    const AArch64MachineFunction &function) {
    if (!spill_operand_has_any_def(operand)) {
        return true;
    }
    const auto maybe_offset =
        function.get_frame_info().get_virtual_reg_spill_offset(operand.virtual_reg_id);
    return !maybe_offset.has_value() || !spill_slot_requires_address_scratch(*maybe_offset);
}

AArch64MachineOperand rewrite_spilled_operand(
    const AArch64MachineOperand &operand,
    const std::unordered_map<std::size_t, unsigned> &mapping,
    const AArch64MachineFunction &function) {
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        if (function.get_physical_reg_for_virtual(virtual_reg->reg.get_id()).has_value()) {
            return operand;
        }
        const auto it = mapping.find(virtual_reg->reg.get_id());
        if (it == mapping.end()) {
            return operand;
        }
        return AArch64MachineOperand::physical_reg(it->second,
                                                   virtual_reg->reg.get_kind());
    }

    if (const auto *memory = operand.get_memory_address_operand(); memory != nullptr) {
        if (memory->base_kind !=
            AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg) {
            return operand;
        }
        if (function
                .get_physical_reg_for_virtual(memory->virtual_reg.get_id())
                .has_value()) {
            return operand;
        }
        const auto it = mapping.find(memory->virtual_reg.get_id());
        if (it == mapping.end()) {
            return operand;
        }
        if (const std::optional<long long> immediate_offset =
                memory->get_immediate_offset();
            immediate_offset.has_value()) {
            return AArch64MachineOperand::memory_address_physical_reg(
                it->second, *immediate_offset, memory->address_mode);
        }
        if (const AArch64MachineSymbolReference *symbolic_offset =
                memory->get_symbolic_offset();
            symbolic_offset != nullptr) {
            return AArch64MachineOperand::memory_address_physical_reg(
                it->second, *symbolic_offset, memory->address_mode);
        }
        return AArch64MachineOperand::memory_address_physical_reg(
            it->second, std::nullopt, memory->address_mode);
    }

    return operand;
}

std::string build_spill_assignment_error(const AArch64MachineInstr &instruction,
                                         std::size_t general_role_refs,
                                         std::size_t float_role_refs,
                                         std::size_t general_scratch_regs,
                                         std::size_t float_scratch_regs,
                                         bool needs_address_scratch,
                                         const AArch64InstructionRewritePlan &plan) {
    std::ostringstream message;
    message << "unsupported spill split shape for AArch64 instruction '"
            << instruction.get_mnemonic() << "'";
    bool emitted_clause = false;
    if (general_role_refs != 0) {
        message << " with " << general_role_refs << " general role reference";
        if (general_role_refs != 1) {
            message << "s";
        }
        emitted_clause = true;
    }
    if (float_role_refs != 0) {
        if (emitted_clause) {
            message << " and ";
        }
        message << float_role_refs << " floating role reference";
        if (float_role_refs != 1) {
            message << "s";
        }
        emitted_clause = true;
    }
    if (!emitted_clause) {
        message << " in the current spill rewriter";
    }
    message << " (available general scratch=" << general_scratch_regs
            << ", floating scratch=" << float_scratch_regs;
    if (instruction.get_flags().is_call ||
        instruction.get_call_clobber_mask().has_value()) {
        message << ", call-like";
    }
    if (needs_address_scratch) {
        message << ", needs address scratch";
        switch (plan.address_scratch_strategy) {
        case AArch64InstructionRewritePlan::AddressScratchStrategy::NotNeeded:
            break;
        case AArch64InstructionRewritePlan::AddressScratchStrategy::Shared:
            message << ", address strategy=shared";
            break;
        case AArch64InstructionRewritePlan::AddressScratchStrategy::SharedAfterRepair:
            message << ", address strategy=shared-repaired";
            break;
        case AArch64InstructionRewritePlan::AddressScratchStrategy::StrictReserved:
            message << ", address strategy=strict-reserved";
            break;
        }
        if (plan.attempted_strict_address_retry) {
            message << ", strict retry attempted";
        }
        switch (plan.address_scratch_failure_stage) {
        case AArch64InstructionRewritePlan::AddressScratchFailureStage::None:
            break;
        case AArch64InstructionRewritePlan::AddressScratchFailureStage::
            SharedAssignment:
            message << ", failure stage=shared-assign";
            break;
        case AArch64InstructionRewritePlan::AddressScratchFailureStage::SharedRepair:
            message << ", failure stage=shared-repair";
            break;
        case AArch64InstructionRewritePlan::AddressScratchFailureStage::StrictAssignment:
            message << ", failure stage=strict-assign";
            break;
        }
    }
    message << ")";
    return message.str();
}

bool assign_spill_bank_operands(
    AArch64InstructionRewritePlan &plan,
    const std::vector<const AArch64SpillRewriteOperand *> &use_operands,
    const std::vector<const AArch64SpillRewriteOperand *> &def_operands,
    const std::vector<const AArch64SpillRewriteOperand *> &aliasable_use_operands,
    const AArch64ScratchPool &scratch_pool) {
    std::size_t next_scratch = 0;
    std::unordered_set<std::size_t> consumed_alias_sources;

    auto assign_fresh_scratch =
        [&](const AArch64SpillRewriteOperand &operand) -> bool {
        if (find_spill_assignment(plan, operand.virtual_reg_id) != nullptr) {
            return true;
        }
        if (next_scratch >= scratch_pool.regs.size()) {
            return false;
        }
        const unsigned physical_reg = scratch_pool.regs[next_scratch++];
        plan.assignments.push_back(AArch64ScratchAssignment{
            operand.virtual_reg_id,
            physical_reg,
            operand.kind,
            scratch_pool.borrowed_regs.find(physical_reg) !=
                scratch_pool.borrowed_regs.end()});
        return true;
    };

    for (const AArch64SpillRewriteOperand *operand : use_operands) {
        if (!assign_fresh_scratch(*operand)) {
            return false;
        }
    }

    for (const AArch64SpillRewriteOperand *operand : def_operands) {
        if (find_spill_assignment(plan, operand->virtual_reg_id) != nullptr) {
            continue;
        }
        if (assign_fresh_scratch(*operand)) {
            continue;
        }

        const AArch64SpillRewriteOperand *alias_source = nullptr;
        for (const AArch64SpillRewriteOperand *candidate : aliasable_use_operands) {
            if (consumed_alias_sources.find(candidate->virtual_reg_id) !=
                consumed_alias_sources.end()) {
                continue;
            }
            if (find_spill_assignment(plan, candidate->virtual_reg_id) == nullptr) {
                continue;
            }
            alias_source = candidate;
            break;
        }

        if (alias_source == nullptr) {
            return false;
        }

        consumed_alias_sources.insert(alias_source->virtual_reg_id);
        const AArch64ScratchAssignment *alias_assignment =
            find_spill_assignment(plan, alias_source->virtual_reg_id);
        if (alias_assignment == nullptr) {
            return false;
        }

        plan.assignments.push_back(AArch64ScratchAssignment{
            operand->virtual_reg_id,
            alias_assignment->physical_reg,
            operand->kind,
            alias_assignment->borrowed});
    }

    return true;
}

bool is_dedicated_general_spill_scratch(unsigned reg) {
    return std::find(kAArch64SpillScratchGeneralPhysicalRegs.begin(),
                     kAArch64SpillScratchGeneralPhysicalRegs.end(),
                     reg) != kAArch64SpillScratchGeneralPhysicalRegs.end() ||
           reg == kAArch64SpillAddressPhysicalReg;
}

bool is_dedicated_float_spill_scratch(unsigned reg) {
    return std::find(kAArch64SpillScratchFloatPhysicalRegs.begin(),
                     kAArch64SpillScratchFloatPhysicalRegs.end(),
                     reg) != kAArch64SpillScratchFloatPhysicalRegs.end();
}

bool physical_reg_group_can_share_address_scratch(
    const AArch64InstructionRewritePlan &plan, const AArch64MachineFunction &function,
    unsigned physical_reg) {
    for (const AArch64ScratchAssignment &assignment : plan.assignments) {
        if (assignment.physical_reg != physical_reg) {
            continue;
        }
        const AArch64SpillRewriteOperand *operand =
            find_spill_operand(plan, assignment.virtual_reg_id);
        if (operand == nullptr) {
            continue;
        }
        if (!spill_operand_can_share_address_scratch(*operand, function)) {
            return false;
        }
    }
    return true;
}

bool swap_spill_assignment_groups(AArch64InstructionRewritePlan &plan,
                                  unsigned lhs_reg, unsigned rhs_reg) {
    std::optional<bool> lhs_borrowed;
    std::optional<bool> rhs_borrowed;
    for (const AArch64ScratchAssignment &assignment : plan.assignments) {
        if (assignment.physical_reg == lhs_reg && !lhs_borrowed.has_value()) {
            lhs_borrowed = assignment.borrowed;
        } else if (assignment.physical_reg == rhs_reg && !rhs_borrowed.has_value()) {
            rhs_borrowed = assignment.borrowed;
        }
    }
    if (!lhs_borrowed.has_value()) {
        return false;
    }
    if (!rhs_borrowed.has_value()) {
        rhs_borrowed = false;
    }

    for (AArch64ScratchAssignment &assignment : plan.assignments) {
        if (assignment.physical_reg == lhs_reg) {
            assignment.physical_reg = rhs_reg;
            assignment.borrowed = *rhs_borrowed;
        } else if (assignment.physical_reg == rhs_reg) {
            assignment.physical_reg = lhs_reg;
            assignment.borrowed = *lhs_borrowed;
        }
    }
    return true;
}

bool repair_address_scratch_group(AArch64InstructionRewritePlan &plan,
                                  const AArch64MachineFunction &function) {
    if (!plan.needs_address_scratch) {
        return true;
    }
    if (physical_reg_group_can_share_address_scratch(
            plan, function, kAArch64SpillAddressPhysicalReg)) {
        return true;
    }

    std::unordered_set<unsigned> visited;
    for (const AArch64ScratchAssignment &assignment : plan.assignments) {
        const unsigned candidate_reg = assignment.physical_reg;
        if (candidate_reg == kAArch64SpillAddressPhysicalReg ||
            is_float_physical_reg(candidate_reg) ||
            visited.find(candidate_reg) != visited.end()) {
            continue;
        }
        visited.insert(candidate_reg);
        if (!physical_reg_group_can_share_address_scratch(plan, function,
                                                          candidate_reg)) {
            continue;
        }
        if (!swap_spill_assignment_groups(plan, kAArch64SpillAddressPhysicalReg,
                                          candidate_reg)) {
            continue;
        }
        if (physical_reg_group_can_share_address_scratch(
                plan, function, kAArch64SpillAddressPhysicalReg)) {
            return true;
        }
        swap_spill_assignment_groups(plan, kAArch64SpillAddressPhysicalReg,
                                     candidate_reg);
    }
    return false;
}

bool instruction_acts_like_call(const AArch64MachineInstr &instruction) {
    return instruction.get_flags().is_call ||
           instruction.get_call_clobber_mask().has_value();
}

void record_instruction_used_physical_regs(
    const AArch64MachineOperand &operand, const AArch64MachineFunction &function,
    std::unordered_set<unsigned> &general_regs,
    std::unordered_set<unsigned> &float_regs) {
    if (const auto *physical_reg = operand.get_physical_reg_operand();
        physical_reg != nullptr) {
        if (is_float_physical_reg(physical_reg->reg_number)) {
            float_regs.insert(physical_reg->reg_number);
        } else {
            general_regs.insert(physical_reg->reg_number);
        }
        return;
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        const auto physical_reg =
            function.get_physical_reg_for_virtual(virtual_reg->reg.get_id());
        if (!physical_reg.has_value()) {
            return;
        }
        if (is_float_physical_reg(*physical_reg)) {
            float_regs.insert(*physical_reg);
        } else {
            general_regs.insert(*physical_reg);
        }
        return;
    }
    if (const auto *memory = operand.get_memory_address_operand(); memory != nullptr) {
        if (memory->base_kind ==
            AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg) {
            if (is_float_physical_reg(memory->physical_reg)) {
                float_regs.insert(memory->physical_reg);
            } else {
                general_regs.insert(memory->physical_reg);
            }
            return;
        }
        if (memory->base_kind ==
            AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg) {
            const auto physical_reg =
                function.get_physical_reg_for_virtual(memory->virtual_reg.get_id());
            if (!physical_reg.has_value()) {
                return;
            }
            if (is_float_physical_reg(*physical_reg)) {
                float_regs.insert(*physical_reg);
            } else {
                general_regs.insert(*physical_reg);
            }
        }
    }
}

AArch64ScratchPool build_general_spill_scratch_pool(
    const AArch64MachineInstr &instruction,
    const AArch64MachineFunction &function,
    const std::unordered_set<unsigned> &instruction_general_regs,
    bool needs_address_scratch, bool allow_address_reg_sharing) {
    AArch64ScratchPool pool;
    std::set<unsigned> allocated;
    for (const auto &[id, physical_reg] : function.get_virtual_reg_allocation()) {
        (void)id;
        allocated.insert(physical_reg);
    }

    auto append_if_available = [&](unsigned reg, bool borrowed) {
        if (instruction_general_regs.find(reg) != instruction_general_regs.end()) {
            return;
        }
        if (std::find(pool.regs.begin(), pool.regs.end(), reg) != pool.regs.end()) {
            return;
        }
        pool.regs.push_back(reg);
        if (borrowed) {
            pool.borrowed_regs.insert(reg);
        }
    };

    for (unsigned reg : kAArch64SpillScratchGeneralPhysicalRegs) {
        append_if_available(reg, false);
    }
    if (!needs_address_scratch ||
        (needs_address_scratch && allow_address_reg_sharing)) {
        append_if_available(kAArch64SpillAddressPhysicalReg, false);
    }
    for (unsigned reg : kAArch64CallerSavedAllocatableGeneralPhysicalRegs) {
        if (allocated.find(reg) == allocated.end()) {
            append_if_available(reg, false);
        }
    }
    for (unsigned reg : kAArch64CalleeSavedAllocatableGeneralPhysicalRegs) {
        if (allocated.find(reg) == allocated.end()) {
            append_if_available(reg, false);
        }
    }
    if (!instruction_acts_like_call(instruction)) {
        for (unsigned reg : kAArch64FixedBorrowableGeneralPhysicalRegs) {
            append_if_available(reg, true);
        }
    }
    for (unsigned reg : kAArch64CallerSavedAllocatableGeneralPhysicalRegs) {
        if (allocated.find(reg) != allocated.end()) {
            append_if_available(reg, true);
        }
    }
    for (unsigned reg : kAArch64CalleeSavedAllocatableGeneralPhysicalRegs) {
        if (allocated.find(reg) != allocated.end()) {
            append_if_available(reg, true);
        }
    }
    return pool;
}

AArch64ScratchPool build_float_spill_scratch_pool(
    const AArch64MachineInstr &instruction,
    const AArch64MachineFunction &function,
    const std::unordered_set<unsigned> &instruction_float_regs) {
    AArch64ScratchPool pool;
    std::set<unsigned> allocated;
    for (const auto &[id, physical_reg] : function.get_virtual_reg_allocation()) {
        (void)id;
        allocated.insert(physical_reg);
    }

    auto append_if_available = [&](unsigned reg, bool borrowed) {
        if (instruction_float_regs.find(reg) != instruction_float_regs.end()) {
            return;
        }
        if (std::find(pool.regs.begin(), pool.regs.end(), reg) != pool.regs.end()) {
            return;
        }
        pool.regs.push_back(reg);
        if (borrowed) {
            pool.borrowed_regs.insert(reg);
        }
    };

    for (unsigned reg : kAArch64SpillScratchFloatPhysicalRegs) {
        append_if_available(reg, false);
    }
    for (unsigned reg : kAArch64CallerSavedAllocatableFloatPhysicalRegs) {
        if (allocated.find(reg) == allocated.end()) {
            append_if_available(reg, false);
        }
    }
    for (unsigned reg : kAArch64CalleeSavedAllocatableFloatPhysicalRegs) {
        if (allocated.find(reg) == allocated.end()) {
            append_if_available(reg, false);
        }
    }
    if (!instruction_acts_like_call(instruction)) {
        for (unsigned reg : kAArch64FixedBorrowableFloatPhysicalRegs) {
            append_if_available(reg, true);
        }
    }
    for (unsigned reg : kAArch64CallerSavedAllocatableFloatPhysicalRegs) {
        if (allocated.find(reg) != allocated.end()) {
            append_if_available(reg, true);
        }
    }
    for (unsigned reg : kAArch64CalleeSavedAllocatableFloatPhysicalRegs) {
        if (allocated.find(reg) != allocated.end()) {
            append_if_available(reg, true);
        }
    }
    return pool;
}

AArch64InstructionRewritePlan
build_instruction_rewrite_plan(const AArch64MachineInstr &instruction,
                               const AArch64MachineFunction &function) {
    AArch64InstructionRewritePlan plan;
    std::unordered_map<std::size_t, std::size_t> operand_indices;
    std::unordered_set<unsigned> instruction_general_regs;
    std::unordered_set<unsigned> instruction_float_regs;

    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        record_spilled_operand_roles(plan, operand_indices, operand, function);
        record_instruction_used_physical_regs(
            operand, function, instruction_general_regs, instruction_float_regs);
    }

    std::vector<const AArch64SpillRewriteOperand *> general_use_operands;
    std::vector<const AArch64SpillRewriteOperand *> general_def_operands;
    std::vector<const AArch64SpillRewriteOperand *> general_value_alias_uses;
    std::vector<const AArch64SpillRewriteOperand *> float_use_operands;
    std::vector<const AArch64SpillRewriteOperand *> float_def_operands;
    std::vector<const AArch64SpillRewriteOperand *> float_alias_uses;

    for (const AArch64SpillRewriteOperand &operand : plan.operands) {
        const auto maybe_offset =
            function.get_frame_info().get_virtual_reg_spill_offset(operand.virtual_reg_id);
        if (maybe_offset.has_value() && spill_slot_requires_address_scratch(*maybe_offset)) {
            plan.needs_address_scratch = true;
        }

        const bool is_float = spill_operand_is_floating(operand);
        const bool has_use = spill_operand_has_any_use(operand);
        const bool has_def = spill_operand_has_any_def(operand);
        const bool has_value_use =
            has_role(operand.roles, AArch64SpillOperandRole::ValueUse);
        const bool has_address_use =
            has_role(operand.roles, AArch64SpillOperandRole::AddressBaseUse);

        if (has_use) {
            if (is_float) {
                float_use_operands.push_back(&operand);
            } else {
                general_use_operands.push_back(&operand);
            }
        }
        if (has_def) {
            if (is_float) {
                float_def_operands.push_back(&operand);
            } else {
                general_def_operands.push_back(&operand);
            }
        }
        if (!has_def && has_value_use) {
            if (is_float) {
                float_alias_uses.push_back(&operand);
            } else {
                general_value_alias_uses.push_back(&operand);
            }
        }
    }

    const AArch64ScratchPool float_scratch_pool =
        build_float_spill_scratch_pool(instruction, function,
                                       instruction_float_regs);

    std::size_t general_scratch_count = 0;
    std::size_t float_scratch_count = float_scratch_pool.regs.size();
    bool assigned = false;

    auto try_assign = [&](bool allow_address_reg_sharing) -> bool {
        plan.assignments.clear();
        if (!plan.needs_address_scratch) {
            plan.address_scratch_strategy =
                AArch64InstructionRewritePlan::AddressScratchStrategy::NotNeeded;
        } else if (allow_address_reg_sharing) {
            plan.address_scratch_strategy =
                AArch64InstructionRewritePlan::AddressScratchStrategy::Shared;
        } else {
            plan.address_scratch_strategy =
                AArch64InstructionRewritePlan::AddressScratchStrategy::StrictReserved;
        }
        const AArch64ScratchPool current_general_pool =
            build_general_spill_scratch_pool(instruction, function,
                                             instruction_general_regs,
                                             plan.needs_address_scratch,
                                             allow_address_reg_sharing);
        general_scratch_count = current_general_pool.regs.size();
        float_scratch_count = float_scratch_pool.regs.size();
        const bool assigned_general = assign_spill_bank_operands(
            plan, general_use_operands, general_def_operands,
            general_value_alias_uses, current_general_pool);
        const bool assigned_float = assign_spill_bank_operands(
            plan, float_use_operands, float_def_operands, float_alias_uses,
            float_scratch_pool);
        if (!assigned_general || !assigned_float) {
            if (plan.needs_address_scratch) {
                plan.address_scratch_failure_stage =
                    allow_address_reg_sharing
                        ? AArch64InstructionRewritePlan::
                              AddressScratchFailureStage::SharedAssignment
                        : AArch64InstructionRewritePlan::
                              AddressScratchFailureStage::StrictAssignment;
            }
            return false;
        }
        if (allow_address_reg_sharing) {
            const bool shareable_before_repair =
                physical_reg_group_can_share_address_scratch(
                    plan, function, kAArch64SpillAddressPhysicalReg);
            if (!repair_address_scratch_group(plan, function)) {
                plan.address_scratch_failure_stage =
                    AArch64InstructionRewritePlan::AddressScratchFailureStage::
                        SharedRepair;
                return false;
            }
            if (!shareable_before_repair) {
                plan.address_scratch_strategy =
                    AArch64InstructionRewritePlan::AddressScratchStrategy::
                        SharedAfterRepair;
            }
        }
        plan.address_scratch_failure_stage =
            AArch64InstructionRewritePlan::AddressScratchFailureStage::None;
        return true;
    };

    // First try to share x28 as a value scratch when the address-temp users allow it.
    // If that staged assignment cannot be repaired, fall back to reserving x28 strictly
    // for large-offset frame materialization.
    assigned = try_assign(true);
    if (!assigned && plan.needs_address_scratch) {
        plan.attempted_strict_address_retry = true;
        assigned = try_assign(false);
    }

    if (!assigned) {
        const std::size_t general_roles =
            general_use_operands.size() + general_def_operands.size();
        const std::size_t float_roles =
            float_use_operands.size() + float_def_operands.size();
        plan.failure_reason = build_spill_assignment_error(
            instruction, general_roles, float_roles,
            general_scratch_count, float_scratch_count,
            plan.needs_address_scratch, plan);
        return plan;
    }

    std::unordered_map<std::size_t, unsigned> spill_mapping;
    for (const AArch64ScratchAssignment &assignment : plan.assignments) {
        spill_mapping.emplace(assignment.virtual_reg_id, assignment.physical_reg);
    }

    std::vector<const AArch64SpillRewriteOperand *> load_operands;
    std::vector<const AArch64SpillRewriteOperand *> store_operands;
    for (const AArch64SpillRewriteOperand &operand : plan.operands) {
        if (spill_operand_has_any_use(operand)) {
            load_operands.push_back(&operand);
        }
        if (spill_operand_has_any_def(operand)) {
            store_operands.push_back(&operand);
        }
    }
    std::stable_sort(load_operands.begin(), load_operands.end(),
                     [&](const AArch64SpillRewriteOperand *lhs,
                         const AArch64SpillRewriteOperand *rhs) {
                         const AArch64ScratchAssignment *lhs_assignment =
                             find_spill_assignment(plan, lhs->virtual_reg_id);
                         const AArch64ScratchAssignment *rhs_assignment =
                             find_spill_assignment(plan, rhs->virtual_reg_id);
                         const bool lhs_is_address_reg =
                             lhs_assignment != nullptr &&
                             lhs_assignment->physical_reg ==
                                 kAArch64SpillAddressPhysicalReg;
                         const bool rhs_is_address_reg =
                             rhs_assignment != nullptr &&
                             rhs_assignment->physical_reg ==
                                 kAArch64SpillAddressPhysicalReg;
                         return !lhs_is_address_reg && rhs_is_address_reg;
                     });
    std::stable_sort(store_operands.begin(), store_operands.end(),
                     [&](const AArch64SpillRewriteOperand *lhs,
                         const AArch64SpillRewriteOperand *rhs) {
                         const AArch64ScratchAssignment *lhs_assignment =
                             find_spill_assignment(plan, lhs->virtual_reg_id);
                         const AArch64ScratchAssignment *rhs_assignment =
                             find_spill_assignment(plan, rhs->virtual_reg_id);
                         const bool lhs_is_address_reg =
                             lhs_assignment != nullptr &&
                             lhs_assignment->physical_reg ==
                                 kAArch64SpillAddressPhysicalReg;
                         const bool rhs_is_address_reg =
                             rhs_assignment != nullptr &&
                             rhs_assignment->physical_reg ==
                                 kAArch64SpillAddressPhysicalReg;
                         return lhs_is_address_reg && !rhs_is_address_reg;
                     });

    for (const AArch64SpillRewriteOperand *operand : load_operands) {
        const AArch64ScratchAssignment *assignment =
            find_spill_assignment(plan, operand->virtual_reg_id);
        if (assignment == nullptr) {
            continue;
        }
        plan.steps.push_back(AArch64SpillRewriteStep{
            AArch64SpillRewriteStep::Kind::LoadUse,
            operand->virtual_reg_id,
            assignment->physical_reg,
            operand->kind,
            std::nullopt});
    }

    std::vector<AArch64MachineOperand> rewritten_operands;
    rewritten_operands.reserve(instruction.get_operands().size());
    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        rewritten_operands.push_back(
            rewrite_spilled_operand(operand, spill_mapping, function));
    }
    plan.steps.push_back(AArch64SpillRewriteStep{
        AArch64SpillRewriteStep::Kind::EmitInstruction,
        0,
        0,
        AArch64VirtualRegKind::General32,
        AArch64MachineInstr(instruction.get_mnemonic(), std::move(rewritten_operands),
                            instruction.get_flags(), instruction.get_debug_location(),
                            instruction.get_implicit_defs(),
                            instruction.get_implicit_uses(),
                            instruction.get_call_clobber_mask())});

    for (const AArch64SpillRewriteOperand *operand : store_operands) {
        const AArch64ScratchAssignment *assignment =
            find_spill_assignment(plan, operand->virtual_reg_id);
        if (assignment == nullptr) {
            continue;
        }
        plan.steps.push_back(AArch64SpillRewriteStep{
            AArch64SpillRewriteStep::Kind::StoreDef,
            operand->virtual_reg_id,
            assignment->physical_reg,
            operand->kind,
            std::nullopt});
    }

    return plan;
}

void add_backend_error(DiagnosticEngine &diagnostic_engine,
                       const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
}

std::size_t borrowed_scratch_save_size(unsigned physical_reg) {
    return is_float_physical_reg(physical_reg) ? 16 : 8;
}

AArch64VirtualRegKind borrowed_scratch_save_kind(unsigned physical_reg) {
    return is_float_physical_reg(physical_reg) ? AArch64VirtualRegKind::Float128
                                               : AArch64VirtualRegKind::General64;
}

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

std::size_t ensure_borrowed_scratch_save_slot(AArch64MachineFunction &function,
                                              unsigned physical_reg) {
    AArch64FunctionFrameInfo &frame_info = function.get_frame_info();
    if (frame_info.has_borrowed_scratch_reg_offset(physical_reg)) {
        return frame_info.get_borrowed_scratch_reg_offset(physical_reg);
    }
    const std::size_t save_size = borrowed_scratch_save_size(physical_reg);
    std::size_t local_size = frame_info.get_local_size();
    local_size = align_to(local_size, save_size);
    local_size += save_size;
    frame_info.set_local_size(local_size);
    frame_info.set_borrowed_scratch_reg_offset(physical_reg, local_size);
    return local_size;
}

std::vector<AArch64BorrowedScratchState>
collect_borrowed_scratch_states(const AArch64InstructionRewritePlan &plan) {
    std::vector<AArch64BorrowedScratchState> states;
    std::unordered_set<unsigned> seen;
    for (const AArch64ScratchAssignment &assignment : plan.assignments) {
        if (!assignment.borrowed || seen.find(assignment.physical_reg) != seen.end()) {
            continue;
        }
        seen.insert(assignment.physical_reg);
        states.push_back(AArch64BorrowedScratchState{
            assignment.physical_reg, borrowed_scratch_save_kind(assignment.physical_reg)});
    }
    return states;
}

} // namespace

bool AArch64SpillRewritePass::run(AArch64MachineFunction &function,
                                  DiagnosticEngine &diagnostic_engine) const {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> rewritten;
        for (AArch64MachineInstr &instruction : block.get_instructions()) {
            AArch64InstructionRewritePlan rewrite_plan =
                build_instruction_rewrite_plan(instruction, function);
            if (!rewrite_plan.failure_reason.empty()) {
                add_backend_error(diagnostic_engine, rewrite_plan.failure_reason);
                return false;
            }

            for (const AArch64ScratchAssignment &assignment : rewrite_plan.assignments) {
                if (!is_float_physical_reg(assignment.physical_reg) &&
                    (is_dedicated_general_spill_scratch(assignment.physical_reg) ||
                     is_aarch64_callee_saved_allocatable_general_physical_reg(
                         assignment.physical_reg))) {
                    function.get_frame_info().mark_saved_physical_reg(
                        assignment.physical_reg);
                } else if (is_float_physical_reg(assignment.physical_reg) &&
                           is_aarch64_callee_saved_allocatable_float_physical_reg(
                               assignment.physical_reg)) {
                    function.get_frame_info().mark_saved_physical_reg(
                        assignment.physical_reg);
                }
            }
            if (rewrite_plan.needs_address_scratch) {
                function.get_frame_info().mark_saved_physical_reg(
                    kAArch64SpillAddressPhysicalReg);
            }

            const std::vector<AArch64BorrowedScratchState> borrowed_scratch_states =
                collect_borrowed_scratch_states(rewrite_plan);
            for (const AArch64BorrowedScratchState &state : borrowed_scratch_states) {
                const std::size_t offset =
                    ensure_borrowed_scratch_save_slot(function, state.physical_reg);
                append_physical_frame_store(
                    rewritten, state.physical_reg, state.save_kind,
                    borrowed_scratch_save_size(state.physical_reg), offset,
                    kAArch64SpillAddressPhysicalReg);
            }

            for (const AArch64SpillRewriteStep &step : rewrite_plan.steps) {
                switch (step.kind) {
                case AArch64SpillRewriteStep::Kind::LoadUse: {
                    const auto maybe_offset =
                        function.get_frame_info().get_virtual_reg_spill_offset(
                            step.virtual_reg_id);
                    if (!maybe_offset.has_value()) {
                        break;
                    }
                    append_physical_frame_load(
                        rewritten, step.physical_reg, step.virtual_reg_kind,
                        virtual_reg_size(step.virtual_reg_kind), *maybe_offset,
                        kAArch64SpillAddressPhysicalReg);
                    break;
                }
                case AArch64SpillRewriteStep::Kind::EmitInstruction:
                    if (step.instruction.has_value()) {
                        rewritten.push_back(*step.instruction);
                    }
                    break;
                case AArch64SpillRewriteStep::Kind::StoreDef: {
                    const auto maybe_offset =
                        function.get_frame_info().get_virtual_reg_spill_offset(
                            step.virtual_reg_id);
                    if (!maybe_offset.has_value()) {
                        break;
                    }
                    append_physical_frame_store(
                        rewritten, step.physical_reg, step.virtual_reg_kind,
                        virtual_reg_size(step.virtual_reg_kind), *maybe_offset,
                        kAArch64SpillAddressPhysicalReg);
                    break;
                }
                }
            }

            for (auto it = borrowed_scratch_states.rbegin();
                 it != borrowed_scratch_states.rend(); ++it) {
                const std::size_t offset =
                    function.get_frame_info().get_borrowed_scratch_reg_offset(
                        it->physical_reg);
                append_physical_frame_load(
                    rewritten, it->physical_reg, it->save_kind,
                    borrowed_scratch_save_size(it->physical_reg), offset,
                    kAArch64SpillAddressPhysicalReg);
            }
        }
        block.get_instructions() = std::move(rewritten);
    }
    return true;
}

} // namespace sysycc
