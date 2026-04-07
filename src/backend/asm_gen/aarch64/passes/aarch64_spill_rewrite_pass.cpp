#include "backend/asm_gen/aarch64/passes/aarch64_spill_rewrite_pass.hpp"

#include <optional>
#include <string>
#include <unordered_map>
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

enum class AArch64SpillSplitRecipeKind : unsigned char {
    Unsupported,
    MoveLike,
    BinaryLike,
    CompareLike,
    SelectLike,
    SetLike,
    LoadLike,
    StoreLike,
    PairLoadLike,
    PairStoreLike,
    BranchLike,
    IndirectCallLike,
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
        if (const std::string *symbolic_offset =
                memory->get_symbolic_offset_text();
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

bool is_load_like_mnemonic(const std::string &mnemonic) {
    return mnemonic == "ldp" || mnemonic.rfind("ldr", 0) == 0;
}

bool is_store_like_mnemonic(const std::string &mnemonic) {
    return mnemonic == "stp" || mnemonic.rfind("str", 0) == 0;
}

bool is_value_like_operand(const AArch64MachineOperand &operand) {
    return operand.get_virtual_reg_operand() != nullptr ||
           operand.get_physical_reg_operand() != nullptr ||
           operand.get_immediate_operand() != nullptr ||
           operand.get_symbol_operand() != nullptr ||
           operand.get_zero_register_operand() != nullptr ||
           operand.get_stack_pointer_operand() != nullptr;
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

AArch64SpillSplitRecipeKind
classify_spill_split_recipe(const AArch64MachineInstr &instruction) {
    const std::vector<AArch64MachineOperand> &operands = instruction.get_operands();
    const std::string &mnemonic = instruction.get_mnemonic();

    if (!operands.empty() && operands.back().get_memory_address_operand() != nullptr) {
        if (is_load_like_mnemonic(mnemonic)) {
            if (operands.size() == 2) {
                return AArch64SpillSplitRecipeKind::LoadLike;
            }
            if (operands.size() == 3) {
                return AArch64SpillSplitRecipeKind::PairLoadLike;
            }
        }
        if (is_store_like_mnemonic(mnemonic)) {
            if (operands.size() == 2) {
                return AArch64SpillSplitRecipeKind::StoreLike;
            }
            if (operands.size() == 3) {
                return AArch64SpillSplitRecipeKind::PairStoreLike;
            }
        }
    }

    if ((mnemonic == "cmp" || mnemonic == "fcmp") && operands.size() >= 2) {
        return AArch64SpillSplitRecipeKind::CompareLike;
    }
    if (mnemonic == "cset" && operands.size() == 2 &&
        operands[1].get_condition_code_operand() != nullptr) {
        return AArch64SpillSplitRecipeKind::SetLike;
    }
    if (mnemonic == "csel" && operands.size() == 4 &&
        operands[3].get_condition_code_operand() != nullptr) {
        return AArch64SpillSplitRecipeKind::SelectLike;
    }
    if ((mnemonic == "cbnz" || mnemonic == "cbz") && operands.size() == 2 &&
        operands[1].get_label_operand() != nullptr) {
        return AArch64SpillSplitRecipeKind::BranchLike;
    }
    if (mnemonic == "blr" && operands.size() == 1) {
        return AArch64SpillSplitRecipeKind::IndirectCallLike;
    }
    if (operands.size() == 2 && is_value_like_operand(operands[1])) {
        return AArch64SpillSplitRecipeKind::MoveLike;
    }
    if (operands.size() >= 3 && is_value_like_operand(operands[1]) &&
        is_value_like_operand(operands[2])) {
        return AArch64SpillSplitRecipeKind::BinaryLike;
    }
    return AArch64SpillSplitRecipeKind::Unsupported;
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

    const AArch64SpillSplitRecipeKind recipe =
        classify_spill_split_recipe(instruction);
    if (recipe == AArch64SpillSplitRecipeKind::Unsupported) {
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

    auto append_use_for_operand = [&](std::size_t operand_index) {
        const AArch64InstructionSpillOccurrence *occurrence =
            find_spill_occurrence(occurrences, operand_index);
        if (occurrence != nullptr) {
            append_split_use_step(plan, occurrence->ref.id, occurrence->ref.kind);
        }
    };

    auto append_def_for_operand = [&](std::size_t operand_index) {
        const AArch64InstructionSpillOccurrence *occurrence =
            find_spill_occurrence(occurrences, operand_index);
        if (occurrence != nullptr) {
            append_split_def_step(plan, occurrence->ref.id, occurrence->ref.kind);
        }
    };

    bool assign_ok = true;
    switch (recipe) {
    case AArch64SpillSplitRecipeKind::MoveLike:
        assign_ok = assign_for_operand(0) && assign_for_operand(1);
        if (assign_ok) {
            append_use_for_operand(1);
            append_def_for_operand(0);
        }
        break;
    case AArch64SpillSplitRecipeKind::BinaryLike:
        assign_ok = assign_for_operand(0) && assign_for_operand(1) &&
                    assign_for_operand(2);
        if (assign_ok) {
            append_use_for_operand(1);
            append_use_for_operand(2);
            append_def_for_operand(0);
        }
        break;
    case AArch64SpillSplitRecipeKind::CompareLike:
        assign_ok = assign_for_operand(0) && assign_for_operand(1);
        if (assign_ok) {
            append_use_for_operand(0);
            append_use_for_operand(1);
        }
        break;
    case AArch64SpillSplitRecipeKind::SelectLike:
        assign_ok = assign_for_operand(0) && assign_for_operand(1) &&
                    assign_for_operand(2);
        if (assign_ok) {
            append_use_for_operand(1);
            append_use_for_operand(2);
            append_def_for_operand(0);
        }
        break;
    case AArch64SpillSplitRecipeKind::SetLike:
        assign_ok = assign_for_operand(0);
        if (assign_ok) {
            append_def_for_operand(0);
        }
        break;
    case AArch64SpillSplitRecipeKind::LoadLike:
        assign_ok = assign_for_operand(0) && assign_for_operand(1);
        if (assign_ok) {
            append_use_for_operand(1);
            append_def_for_operand(0);
        }
        break;
    case AArch64SpillSplitRecipeKind::StoreLike:
        assign_ok = assign_for_operand(0) && assign_for_operand(1);
        if (assign_ok) {
            append_use_for_operand(0);
            append_use_for_operand(1);
        }
        break;
    case AArch64SpillSplitRecipeKind::PairLoadLike:
        assign_ok = assign_for_operand(0) && assign_for_operand(1) &&
                    assign_for_operand(2);
        if (assign_ok) {
            append_use_for_operand(2);
            append_def_for_operand(0);
            append_def_for_operand(1);
        }
        break;
    case AArch64SpillSplitRecipeKind::PairStoreLike:
        assign_ok = assign_for_operand(0) && assign_for_operand(1) &&
                    assign_for_operand(2);
        if (assign_ok) {
            append_use_for_operand(0);
            append_use_for_operand(1);
            append_use_for_operand(2);
        }
        break;
    case AArch64SpillSplitRecipeKind::BranchLike:
    case AArch64SpillSplitRecipeKind::IndirectCallLike:
        assign_ok = assign_for_operand(0);
        if (assign_ok) {
            append_use_for_operand(0);
        }
        break;
    case AArch64SpillSplitRecipeKind::Unsupported:
        break;
    }

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

    std::vector<AArch64SpillRewriteStep> prefix_steps;
    prefix_steps.swap(plan.steps);
    for (const AArch64SpillRewriteStep &step : prefix_steps) {
        if (step.kind == AArch64SpillRewriteStep::Kind::LoadUse) {
            plan.steps.push_back(step);
        }
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
    for (const AArch64SpillRewriteStep &step : prefix_steps) {
        if (step.kind == AArch64SpillRewriteStep::Kind::StoreDef) {
            plan.steps.push_back(step);
        }
    }

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
                        "unsupported spill split recipe for AArch64 instruction '" +
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
