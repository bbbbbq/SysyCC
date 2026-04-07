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

bool spill_slot_requires_address_scratch(std::size_t offset) {
    return offset > 255;
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
        return AArch64MachineOperand::memory_address_physical_reg(
            it->second, memory->offset_text, memory->address_mode);
    }
    return operand;
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
            const AArch64InstructionRewritePlan rewrite_plan =
                build_instruction_rewrite_plan(instruction, function);
            if (!rewrite_plan.unassigned_operands.empty()) {
                bool requires_general_split = false;
                bool requires_float_split = false;
                for (const AArch64SpillRewriteOperand &operand :
                     rewrite_plan.unassigned_operands) {
                    if (AArch64VirtualReg(operand.ref.id, operand.ref.kind)
                            .is_floating_point()) {
                        requires_float_split = true;
                    } else {
                        requires_general_split = true;
                    }
                }
                add_backend_error(
                    diagnostic_engine,
                    "AArch64 spill rewrite exhausted the fixed " +
                        std::string(requires_general_split && !requires_float_split
                                        ? "general"
                                        : !requires_general_split && requires_float_split
                                              ? "floating-point"
                                              : "mixed-bank") +
                        " scratch budget while rewriting instruction '" +
                        instruction.get_mnemonic() + "'");
                return false;
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
