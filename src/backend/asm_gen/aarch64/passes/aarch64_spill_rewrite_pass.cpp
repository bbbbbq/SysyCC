#include "backend/asm_gen/aarch64/passes/aarch64_spill_rewrite_pass.hpp"

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
    std::vector<AArch64SpillRewriteOperand> operands;
    std::vector<AArch64ScratchAssignment> assignments;
    bool needs_address_scratch = false;
    std::vector<AArch64SpillRewriteStep> steps;
    std::string failure_reason;
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
                                         std::size_t float_role_refs) {
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
    return message.str();
}

bool assign_spill_bank_operands(
    AArch64InstructionRewritePlan &plan,
    const std::vector<const AArch64SpillRewriteOperand *> &use_operands,
    const std::vector<const AArch64SpillRewriteOperand *> &def_operands,
    const std::vector<const AArch64SpillRewriteOperand *> &aliasable_use_operands,
    const std::vector<unsigned> &scratch_regs) {
    std::size_t next_scratch = 0;
    std::unordered_set<std::size_t> consumed_alias_sources;

    auto assign_fresh_scratch =
        [&](const AArch64SpillRewriteOperand &operand) -> bool {
        if (find_spill_assignment(plan, operand.virtual_reg_id) != nullptr) {
            return true;
        }
        if (next_scratch >= scratch_regs.size()) {
            return false;
        }
        plan.assignments.push_back(AArch64ScratchAssignment{
            operand.virtual_reg_id,
            scratch_regs[next_scratch++],
            operand.kind});
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
            operand->kind});
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

std::vector<unsigned>
build_general_spill_scratch_pool(const AArch64MachineFunction &function) {
    std::vector<unsigned> regs(kAArch64SpillScratchGeneralPhysicalRegs.begin(),
                               kAArch64SpillScratchGeneralPhysicalRegs.end());
    std::set<unsigned> allocated;
    for (const auto &[id, physical_reg] : function.get_virtual_reg_allocation()) {
        (void)id;
        allocated.insert(physical_reg);
    }
    for (unsigned reg : kAArch64CallerSavedAllocatableGeneralPhysicalRegs) {
        if (allocated.find(reg) == allocated.end()) {
            regs.push_back(reg);
        }
    }
    for (unsigned reg : kAArch64CalleeSavedAllocatableGeneralPhysicalRegs) {
        if (allocated.find(reg) == allocated.end()) {
            regs.push_back(reg);
        }
    }
    return regs;
}

std::vector<unsigned>
build_float_spill_scratch_pool(const AArch64MachineFunction &function) {
    std::vector<unsigned> regs(kAArch64SpillScratchFloatPhysicalRegs.begin(),
                               kAArch64SpillScratchFloatPhysicalRegs.end());
    std::set<unsigned> allocated;
    for (const auto &[id, physical_reg] : function.get_virtual_reg_allocation()) {
        (void)id;
        allocated.insert(physical_reg);
    }
    for (unsigned reg : kAArch64CallerSavedAllocatableFloatPhysicalRegs) {
        if (allocated.find(reg) == allocated.end()) {
            regs.push_back(reg);
        }
    }
    for (unsigned reg : kAArch64CalleeSavedAllocatableFloatPhysicalRegs) {
        if (allocated.find(reg) == allocated.end()) {
            regs.push_back(reg);
        }
    }
    return regs;
}

AArch64InstructionRewritePlan
build_instruction_rewrite_plan(const AArch64MachineInstr &instruction,
                               const AArch64MachineFunction &function,
                               const std::vector<unsigned> &general_scratch_regs,
                               const std::vector<unsigned> &float_scratch_regs) {
    AArch64InstructionRewritePlan plan;
    std::unordered_map<std::size_t, std::size_t> operand_indices;

    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        record_spilled_operand_roles(plan, operand_indices, operand, function);
    }

    std::vector<const AArch64SpillRewriteOperand *> general_use_operands;
    std::vector<const AArch64SpillRewriteOperand *> general_def_operands;
    std::vector<const AArch64SpillRewriteOperand *> general_value_alias_uses;
    std::vector<const AArch64SpillRewriteOperand *> general_address_alias_uses;
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
        if (!is_float && !has_def && !has_value_use && has_address_use) {
            general_address_alias_uses.push_back(&operand);
        }
    }

    // Reuse value-use scratch registers before falling back to address-base aliases.
    std::vector<const AArch64SpillRewriteOperand *> general_alias_uses =
        general_value_alias_uses;
    general_alias_uses.insert(general_alias_uses.end(),
                              general_address_alias_uses.begin(),
                              general_address_alias_uses.end());

    const bool assigned_general = assign_spill_bank_operands(
        plan, general_use_operands, general_def_operands, general_alias_uses,
        general_scratch_regs);
    const bool assigned_float = assign_spill_bank_operands(
        plan, float_use_operands, float_def_operands, float_alias_uses,
        float_scratch_regs);

    if (!assigned_general || !assigned_float) {
        const std::size_t general_roles =
            general_use_operands.size() + general_def_operands.size();
        const std::size_t float_roles =
            float_use_operands.size() + float_def_operands.size();
        plan.failure_reason = build_spill_assignment_error(
            instruction, general_roles, float_roles);
        return plan;
    }

    std::unordered_map<std::size_t, unsigned> spill_mapping;
    for (const AArch64ScratchAssignment &assignment : plan.assignments) {
        spill_mapping.emplace(assignment.virtual_reg_id, assignment.physical_reg);
    }

    for (const AArch64SpillRewriteOperand &operand : plan.operands) {
        if (!spill_operand_has_any_use(operand)) {
            continue;
        }
        const AArch64ScratchAssignment *assignment =
            find_spill_assignment(plan, operand.virtual_reg_id);
        if (assignment == nullptr) {
            continue;
        }
        plan.steps.push_back(AArch64SpillRewriteStep{
            AArch64SpillRewriteStep::Kind::LoadUse,
            operand.virtual_reg_id,
            assignment->physical_reg,
            operand.kind,
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

    for (const AArch64SpillRewriteOperand &operand : plan.operands) {
        if (!spill_operand_has_any_def(operand)) {
            continue;
        }
        const AArch64ScratchAssignment *assignment =
            find_spill_assignment(plan, operand.virtual_reg_id);
        if (assignment == nullptr) {
            continue;
        }
        plan.steps.push_back(AArch64SpillRewriteStep{
            AArch64SpillRewriteStep::Kind::StoreDef,
            operand.virtual_reg_id,
            assignment->physical_reg,
            operand.kind,
            std::nullopt});
    }

    return plan;
}

void add_backend_error(DiagnosticEngine &diagnostic_engine,
                       const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
}

} // namespace

bool AArch64SpillRewritePass::run(AArch64MachineFunction &function,
                                  DiagnosticEngine &diagnostic_engine) const {
    const std::vector<unsigned> general_scratch_regs =
        build_general_spill_scratch_pool(function);
    const std::vector<unsigned> float_scratch_regs =
        build_float_spill_scratch_pool(function);
    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> rewritten;
        for (AArch64MachineInstr &instruction : block.get_instructions()) {
            AArch64InstructionRewritePlan rewrite_plan =
                build_instruction_rewrite_plan(instruction, function,
                                               general_scratch_regs,
                                               float_scratch_regs);
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
        }
        block.get_instructions() = std::move(rewritten);
    }
    return true;
}

} // namespace sysycc
