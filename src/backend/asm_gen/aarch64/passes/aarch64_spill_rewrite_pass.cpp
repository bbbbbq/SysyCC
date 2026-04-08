#include "backend/asm_gen/aarch64/passes/aarch64_spill_rewrite_pass.hpp"

#include <optional>
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

std::vector<ParsedVirtualRegRef>
collect_spilled_virtual_refs(const AArch64MachineOperand &operand,
                             const AArch64MachineFunction &function) {
    std::vector<ParsedVirtualRegRef> spilled;
    for (const ParsedVirtualRegRef &ref : collect_virtual_reg_refs(operand)) {
        if (function.get_physical_reg_for_virtual(ref.id).has_value()) {
            continue;
        }
        if (!function.get_frame_info().get_virtual_reg_spill_offset(ref.id).has_value()) {
            continue;
        }
        spilled.push_back(ref);
    }
    return spilled;
}

struct AArch64SpillRewriteOperand {
    ParsedVirtualRegRef ref;
    bool has_use = false;
    bool has_def = false;
};

enum class AArch64SpillScratchClass : unsigned char {
    GeneralValue,
    FloatingValue,
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
    AArch64SpillScratchClass scratch_class = AArch64SpillScratchClass::GeneralValue;
    std::optional<AArch64MachineInstr> instruction;
};

struct AArch64InstructionRewritePlan {
    std::vector<AArch64SpillRewriteOperand> operands;
    std::vector<AArch64ScratchAssignment> assignments;
    bool needs_address_scratch = false;
    std::vector<AArch64SpillRewriteStep> steps;
    std::vector<AArch64SpillRewriteOperand> unassigned_operands;
};

struct AArch64InstructionSpillOccurrence {
    std::size_t operand_index = 0;
    ParsedVirtualRegRef ref;
};

enum class AArch64SpillOperandRoleKind : unsigned char {
    ValueUse,
    ValueDef,
    AddressBaseUse,
    ConditionCode,
    Label,
    Other,
};

struct AArch64InstructionSpillShape {
    enum class Kind : unsigned char {
        Unsupported,
        SingleValueDef,
        MultiValueDef,
        Compare,
        SetFromCondition,
        SelectFromCondition,
        Load,
        Store,
        BranchOnValue,
        IndirectCallTarget,
    };

    Kind kind = Kind::Unsupported;
    std::vector<std::size_t> use_operand_indices;
    std::vector<std::size_t> def_operand_indices;
};

bool spill_slot_requires_address_scratch(std::size_t offset) {
    return offset > 255;
}

AArch64InstructionRewritePlan
build_instruction_rewrite_plan(const AArch64MachineInstr &instruction,
                               const AArch64MachineFunction &function);

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

std::vector<AArch64InstructionSpillOccurrence>
collect_spill_occurrences(const AArch64MachineInstr &instruction,
                         const AArch64MachineFunction &function) {
    std::vector<AArch64InstructionSpillOccurrence> occurrences;
    const std::vector<AArch64MachineOperand> &operands = instruction.get_operands();
    for (std::size_t operand_index = 0; operand_index < operands.size(); ++operand_index) {
        const AArch64MachineOperand &operand = operands[operand_index];
        std::vector<ParsedVirtualRegRef> refs =
            collect_spilled_virtual_refs(operand, function);
        if (refs.empty()) {
            continue;
        }
        for (const ParsedVirtualRegRef &ref : refs) {
            occurrences.push_back(AArch64InstructionSpillOccurrence{
                operand_index,
                ref});
        }
    }
    return occurrences;
}

AArch64SpillOperandRoleKind
classify_spill_operand_role(const AArch64MachineOperand &operand) {
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        return virtual_reg->is_def ? AArch64SpillOperandRoleKind::ValueDef
                                   : AArch64SpillOperandRoleKind::ValueUse;
    }
    if (const auto *memory = operand.get_memory_address_operand(); memory != nullptr) {
        return memory->base_kind ==
                       AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg
                   ? AArch64SpillOperandRoleKind::AddressBaseUse
                   : AArch64SpillOperandRoleKind::Other;
    }
    if (operand.get_condition_code_operand() != nullptr) {
        return AArch64SpillOperandRoleKind::ConditionCode;
    }
    if (operand.get_label_operand() != nullptr) {
        return AArch64SpillOperandRoleKind::Label;
    }
    return AArch64SpillOperandRoleKind::Other;
}

const AArch64InstructionSpillOccurrence *
find_spill_occurrence(const std::vector<AArch64InstructionSpillOccurrence> &occurrences,
                     std::size_t operand_index) {
    for (const AArch64InstructionSpillOccurrence &occurrence : occurrences) {
        if (occurrence.operand_index == operand_index) {
            return &occurrence;
        }
    }
    return nullptr;
}

AArch64InstructionSpillShape
classify_spill_instruction_shape(const AArch64MachineInstr &instruction) {
    AArch64InstructionSpillShape shape;
    const std::vector<AArch64MachineOperand> &operands = instruction.get_operands();
    std::vector<std::size_t> value_use_indices;
    std::vector<std::size_t> value_def_indices;
    std::vector<std::size_t> address_use_indices;
    std::vector<std::size_t> condition_code_indices;
    std::vector<std::size_t> label_indices;
    std::vector<std::size_t> memory_indices;

    for (std::size_t operand_index = 0; operand_index < operands.size(); ++operand_index) {
        const AArch64MachineOperand &operand = operands[operand_index];
        switch (classify_spill_operand_role(operand)) {
        case AArch64SpillOperandRoleKind::ValueUse:
            value_use_indices.push_back(operand_index);
            break;
        case AArch64SpillOperandRoleKind::ValueDef:
            value_def_indices.push_back(operand_index);
            break;
        case AArch64SpillOperandRoleKind::AddressBaseUse:
            address_use_indices.push_back(operand_index);
            memory_indices.push_back(operand_index);
            break;
        case AArch64SpillOperandRoleKind::ConditionCode:
            condition_code_indices.push_back(operand_index);
            break;
        case AArch64SpillOperandRoleKind::Label:
            label_indices.push_back(operand_index);
            break;
        case AArch64SpillOperandRoleKind::Other:
            if (operand.get_memory_address_operand() != nullptr) {
                memory_indices.push_back(operand_index);
            }
            break;
        }
    }

    if (instruction.get_flags().is_call) {
        if (value_use_indices.size() == 1 && value_def_indices.empty() &&
            memory_indices.empty() && condition_code_indices.empty() &&
            label_indices.empty()) {
            shape.kind = AArch64InstructionSpillShape::Kind::IndirectCallTarget;
            shape.use_operand_indices = value_use_indices;
        }
        return shape;
    }

    if (!memory_indices.empty()) {
        if (memory_indices.size() != 1 || !condition_code_indices.empty() ||
            !label_indices.empty()) {
            return shape;
        }
        if (!value_def_indices.empty() && value_use_indices.empty() &&
            value_def_indices.size() <= 2) {
            shape.kind = AArch64InstructionSpillShape::Kind::Load;
            shape.use_operand_indices = address_use_indices;
            shape.def_operand_indices = value_def_indices;
            return shape;
        }
        if (value_def_indices.empty() && !value_use_indices.empty() &&
            value_use_indices.size() <= 2) {
            shape.kind = AArch64InstructionSpillShape::Kind::Store;
            shape.use_operand_indices = value_use_indices;
            shape.use_operand_indices.insert(shape.use_operand_indices.end(),
                                             address_use_indices.begin(),
                                             address_use_indices.end());
            return shape;
        }
        return shape;
    }

    if (!label_indices.empty()) {
        if (label_indices.size() == 1 && value_def_indices.empty() &&
            value_use_indices.size() == 1 && condition_code_indices.empty()) {
            shape.kind = AArch64InstructionSpillShape::Kind::BranchOnValue;
            shape.use_operand_indices = value_use_indices;
        }
        return shape;
    }

    if (!condition_code_indices.empty()) {
        if (condition_code_indices.size() == 1 && value_def_indices.size() == 1 &&
            value_use_indices.empty()) {
            shape.kind = AArch64InstructionSpillShape::Kind::SetFromCondition;
            shape.def_operand_indices = value_def_indices;
            return shape;
        }
        if (condition_code_indices.size() == 1 && value_def_indices.size() == 1 &&
            value_use_indices.size() == 2) {
            shape.kind = AArch64InstructionSpillShape::Kind::SelectFromCondition;
            shape.use_operand_indices = value_use_indices;
            shape.def_operand_indices = value_def_indices;
            return shape;
        }
        return shape;
    }

    if (value_def_indices.empty() && !value_use_indices.empty() &&
        value_use_indices.size() <= 2) {
        shape.kind = AArch64InstructionSpillShape::Kind::Compare;
        shape.use_operand_indices = value_use_indices;
        return shape;
    }
    if (value_def_indices.size() == 1 && value_use_indices.size() <= 1) {
        shape.kind = AArch64InstructionSpillShape::Kind::SingleValueDef;
        shape.use_operand_indices = value_use_indices;
        shape.def_operand_indices = value_def_indices;
        return shape;
    }
    if (value_def_indices.size() == 1 && value_use_indices.size() >= 2) {
        shape.kind = AArch64InstructionSpillShape::Kind::MultiValueDef;
        shape.use_operand_indices = value_use_indices;
        shape.def_operand_indices = value_def_indices;
        return shape;
    }
    return shape;
}

bool assign_split_scratch(AArch64InstructionRewritePlan &plan,
                          std::size_t &next_general_scratch,
                          std::size_t &next_float_scratch,
                          std::size_t virtual_reg_id,
                          AArch64VirtualRegKind kind) {
    if (find_spill_assignment(plan, virtual_reg_id) != nullptr) {
        return true;
    }

    const bool is_float = AArch64VirtualReg(virtual_reg_id, kind).is_floating_point();
    if (is_float) {
        if (next_float_scratch >= std::size(kAArch64SpillScratchFloatPhysicalRegs)) {
            return false;
        }
        plan.assignments.push_back(AArch64ScratchAssignment{
            virtual_reg_id, kAArch64SpillScratchFloatPhysicalRegs[next_float_scratch++],
            kind});
        return true;
    }

    if (next_general_scratch >= std::size(kAArch64SpillScratchGeneralPhysicalRegs)) {
        return false;
    }
    plan.assignments.push_back(AArch64ScratchAssignment{
        virtual_reg_id, kAArch64SpillScratchGeneralPhysicalRegs[next_general_scratch++],
        kind});
    return true;
}

void append_split_use_step(AArch64InstructionRewritePlan &plan,
                           std::size_t virtual_reg_id,
                           AArch64VirtualRegKind kind) {
    const AArch64ScratchAssignment *assignment =
        find_spill_assignment(plan, virtual_reg_id);
    if (assignment == nullptr) {
        return;
    }
    const AArch64SpillScratchClass scratch_class =
        AArch64VirtualReg(virtual_reg_id, kind).is_floating_point()
            ? AArch64SpillScratchClass::FloatingValue
            : AArch64SpillScratchClass::GeneralValue;
    plan.steps.push_back(AArch64SpillRewriteStep{
        AArch64SpillRewriteStep::Kind::LoadUse,
        virtual_reg_id,
        assignment->physical_reg,
        kind,
        scratch_class,
        std::nullopt});
}

void append_split_def_step(AArch64InstructionRewritePlan &plan,
                           std::size_t virtual_reg_id,
                           AArch64VirtualRegKind kind) {
    const AArch64ScratchAssignment *assignment =
        find_spill_assignment(plan, virtual_reg_id);
    if (assignment == nullptr) {
        return;
    }
    const AArch64SpillScratchClass scratch_class =
        AArch64VirtualReg(virtual_reg_id, kind).is_floating_point()
            ? AArch64SpillScratchClass::FloatingValue
            : AArch64SpillScratchClass::GeneralValue;
    plan.steps.push_back(AArch64SpillRewriteStep{
        AArch64SpillRewriteStep::Kind::StoreDef,
        virtual_reg_id,
        assignment->physical_reg,
        kind,
        scratch_class,
        std::nullopt});
}

void append_split_steps_for_operands(
    AArch64InstructionRewritePlan &plan,
    const std::vector<AArch64InstructionSpillOccurrence> &occurrences,
    const std::vector<std::size_t> &operand_indices, bool defs) {
    std::unordered_set<std::size_t> seen_virtual_regs;
    for (std::size_t operand_index : operand_indices) {
        const AArch64InstructionSpillOccurrence *occurrence =
            find_spill_occurrence(occurrences, operand_index);
        if (occurrence == nullptr ||
            !seen_virtual_regs.insert(occurrence->ref.id).second) {
            continue;
        }
        if (defs) {
            append_split_def_step(plan, occurrence->ref.id, occurrence->ref.kind);
        } else {
            append_split_use_step(plan, occurrence->ref.id, occurrence->ref.kind);
        }
    }
}

void mark_address_scratch_if_needed(AArch64InstructionRewritePlan &plan,
                                    const AArch64MachineFunction &function,
                                    std::size_t virtual_reg_id) {
    const auto maybe_offset =
        function.get_frame_info().get_virtual_reg_spill_offset(virtual_reg_id);
    if (maybe_offset.has_value() && spill_slot_requires_address_scratch(*maybe_offset)) {
        plan.needs_address_scratch = true;
    }
}

AArch64InstructionRewritePlan
build_split_rewrite_plan(const AArch64MachineInstr &instruction,
                         const AArch64MachineFunction &function) {
    AArch64InstructionRewritePlan plan;
    const std::vector<AArch64InstructionSpillOccurrence> occurrences =
        collect_spill_occurrences(instruction, function);
    if (occurrences.empty()) {
        return plan;
    }

    const AArch64InstructionSpillShape shape =
        classify_spill_instruction_shape(instruction);
    if (shape.kind == AArch64InstructionSpillShape::Kind::Unsupported) {
        plan.unassigned_operands = build_instruction_rewrite_plan(instruction, function)
                                       .unassigned_operands;
        return plan;
    }

    std::size_t next_general_scratch = 0;
    std::size_t next_float_scratch = 0;

    auto assign_for_operand = [&](std::size_t operand_index) -> bool {
        const AArch64InstructionSpillOccurrence *occurrence =
            find_spill_occurrence(occurrences, operand_index);
        if (occurrence == nullptr) {
            return true;
        }
        mark_address_scratch_if_needed(plan, function, occurrence->ref.id);
        return assign_split_scratch(plan, next_general_scratch, next_float_scratch,
                                    occurrence->ref.id, occurrence->ref.kind);
    };

    auto assign_for_operands =
        [&](const std::vector<std::size_t> &operand_indices) -> bool {
        for (std::size_t operand_index : operand_indices) {
            if (!assign_for_operand(operand_index)) {
                return false;
            }
        }
        return true;
    };

    const bool assign_ok =
        assign_for_operands(shape.use_operand_indices) &&
        assign_for_operands(shape.def_operand_indices);

    if (!assign_ok) {
        plan.unassigned_operands = build_instruction_rewrite_plan(instruction, function)
                                       .unassigned_operands;
        return plan;
    }

    std::unordered_map<std::size_t, unsigned> spill_mapping;
    for (const AArch64ScratchAssignment &assignment : plan.assignments) {
        spill_mapping.emplace(assignment.virtual_reg_id, assignment.physical_reg);
    }

    std::vector<AArch64MachineOperand> operands;
    operands.reserve(instruction.get_operands().size());
    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        operands.push_back(rewrite_spilled_operand(operand, spill_mapping, function));
    }

    append_split_steps_for_operands(plan, occurrences, shape.use_operand_indices,
                                    false);
    plan.steps.push_back(AArch64SpillRewriteStep{
        AArch64SpillRewriteStep::Kind::EmitInstruction,
        0,
        0,
        AArch64VirtualRegKind::General32,
        AArch64SpillScratchClass::GeneralValue,
        AArch64MachineInstr(instruction.get_mnemonic(), std::move(operands),
                            instruction.get_flags(),
                            instruction.get_implicit_defs(),
                            instruction.get_implicit_uses(),
                            instruction.get_call_clobber_mask())});
    append_split_steps_for_operands(plan, occurrences, shape.def_operand_indices,
                                    true);

    return plan;
}

AArch64InstructionRewritePlan
build_instruction_rewrite_plan(const AArch64MachineInstr &instruction,
                               const AArch64MachineFunction &function) {
    AArch64InstructionRewritePlan plan;
    std::unordered_map<std::size_t, std::size_t> operand_indices;

    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        for (const ParsedVirtualRegRef &ref :
             collect_spilled_virtual_refs(operand, function)) {
            std::size_t entry_index = 0;
            const auto existing_it = operand_indices.find(ref.id);
            if (existing_it == operand_indices.end()) {
                entry_index = plan.operands.size();
                operand_indices.emplace(ref.id, entry_index);
                plan.operands.push_back(AArch64SpillRewriteOperand{ref});
            } else {
                entry_index = existing_it->second;
            }
            plan.operands[entry_index].has_use =
                plan.operands[entry_index].has_use || !ref.is_def;
            plan.operands[entry_index].has_def =
                plan.operands[entry_index].has_def || ref.is_def;
        }
    }

    std::vector<const AArch64SpillRewriteOperand *> large_general_operands;
    std::vector<const AArch64SpillRewriteOperand *> small_general_operands;
    std::vector<const AArch64SpillRewriteOperand *> float_operands;

    for (const AArch64SpillRewriteOperand &operand : plan.operands) {
        const auto maybe_offset =
            function.get_frame_info().get_virtual_reg_spill_offset(operand.ref.id);
        if (!maybe_offset.has_value()) {
            continue;
        }
        if (spill_slot_requires_address_scratch(*maybe_offset)) {
            plan.needs_address_scratch = true;
        }
        if (AArch64VirtualReg(operand.ref.id, operand.ref.kind).is_floating_point()) {
            float_operands.push_back(&operand);
            continue;
        }
        if (spill_slot_requires_address_scratch(*maybe_offset)) {
            large_general_operands.push_back(&operand);
            continue;
        }
        small_general_operands.push_back(&operand);
    }

    if (large_general_operands.size() + small_general_operands.size() >
            std::size(kAArch64SpillScratchGeneralPhysicalRegs) ||
        float_operands.size() > std::size(kAArch64SpillScratchFloatPhysicalRegs)) {
        plan.unassigned_operands = plan.operands;
        return plan;
    }

    std::size_t next_general_scratch = 0;
    auto assign_general_operand =
        [&](const AArch64SpillRewriteOperand &operand, unsigned physical_reg) {
            plan.assignments.push_back(AArch64ScratchAssignment{
                operand.ref.id, physical_reg, operand.ref.kind});
        };

    for (const AArch64SpillRewriteOperand *operand : large_general_operands) {
        assign_general_operand(*operand,
                               kAArch64SpillScratchGeneralPhysicalRegs[next_general_scratch++]);
    }
    for (const AArch64SpillRewriteOperand *operand : small_general_operands) {
        assign_general_operand(*operand,
                               kAArch64SpillScratchGeneralPhysicalRegs[next_general_scratch++]);
    }
    for (std::size_t index = 0; index < float_operands.size(); ++index) {
        plan.assignments.push_back(AArch64ScratchAssignment{
            float_operands[index]->ref.id, kAArch64SpillScratchFloatPhysicalRegs[index],
            float_operands[index]->ref.kind});
    }

    std::unordered_map<std::size_t, unsigned> spill_mapping;
    for (const AArch64ScratchAssignment &assignment : plan.assignments) {
        spill_mapping.emplace(assignment.virtual_reg_id, assignment.physical_reg);
    }

    auto append_use_step = [&](const AArch64SpillRewriteOperand &operand) {
        const AArch64ScratchAssignment *assignment =
            find_spill_assignment(plan, operand.ref.id);
        if (assignment == nullptr || !operand.has_use) {
            return;
        }
        const AArch64SpillScratchClass scratch_class =
            AArch64VirtualReg(operand.ref.id, operand.ref.kind).is_floating_point()
                ? AArch64SpillScratchClass::FloatingValue
                : AArch64SpillScratchClass::GeneralValue;
        plan.steps.push_back(AArch64SpillRewriteStep{
            AArch64SpillRewriteStep::Kind::LoadUse,
            operand.ref.id,
            assignment->physical_reg,
            operand.ref.kind,
            scratch_class,
            std::nullopt});
    };

    auto append_def_step = [&](const AArch64SpillRewriteOperand &operand) {
        const AArch64ScratchAssignment *assignment =
            find_spill_assignment(plan, operand.ref.id);
        if (assignment == nullptr || !operand.has_def) {
            return;
        }
        const AArch64SpillScratchClass scratch_class =
            AArch64VirtualReg(operand.ref.id, operand.ref.kind).is_floating_point()
                ? AArch64SpillScratchClass::FloatingValue
                : AArch64SpillScratchClass::GeneralValue;
        plan.steps.push_back(AArch64SpillRewriteStep{
            AArch64SpillRewriteStep::Kind::StoreDef,
            operand.ref.id,
            assignment->physical_reg,
            operand.ref.kind,
            scratch_class,
            std::nullopt});
    };

    for (const AArch64SpillRewriteOperand *operand : large_general_operands) {
        append_use_step(*operand);
    }
    for (const AArch64SpillRewriteOperand *operand : small_general_operands) {
        append_use_step(*operand);
    }
    for (const AArch64SpillRewriteOperand *operand : float_operands) {
        append_use_step(*operand);
    }

    std::vector<AArch64MachineOperand> operands;
    operands.reserve(instruction.get_operands().size());
    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        operands.push_back(rewrite_spilled_operand(operand, spill_mapping, function));
    }
    plan.steps.push_back(AArch64SpillRewriteStep{
        AArch64SpillRewriteStep::Kind::EmitInstruction,
        0,
        0,
        AArch64VirtualRegKind::General32,
        AArch64SpillScratchClass::GeneralValue,
        AArch64MachineInstr(instruction.get_mnemonic(), std::move(operands),
                            instruction.get_flags(),
                            instruction.get_implicit_defs(),
                            instruction.get_implicit_uses(),
                            instruction.get_call_clobber_mask())});

    for (const AArch64SpillRewriteOperand *operand : large_general_operands) {
        append_def_step(*operand);
    }
    for (const AArch64SpillRewriteOperand *operand : small_general_operands) {
        append_def_step(*operand);
    }
    for (const AArch64SpillRewriteOperand *operand : float_operands) {
        append_def_step(*operand);
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
    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> rewritten;
        for (AArch64MachineInstr &instruction : block.get_instructions()) {
            AArch64InstructionRewritePlan rewrite_plan =
                build_instruction_rewrite_plan(instruction, function);
            if (!rewrite_plan.unassigned_operands.empty()) {
                rewrite_plan = build_split_rewrite_plan(instruction, function);
                if (!rewrite_plan.unassigned_operands.empty()) {
                    add_backend_error(
                        diagnostic_engine,
                        "unsupported spill rewrite shape for AArch64 instruction '" +
                            instruction.get_mnemonic() + "'");
                    return false;
                }
            }

            for (const AArch64ScratchAssignment &assignment : rewrite_plan.assignments) {
                if (!is_float_physical_reg(assignment.physical_reg)) {
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
