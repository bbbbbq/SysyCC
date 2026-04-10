#pragma once

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"

namespace sysycc {

template <typename DispatchContext, typename FunctionState>
bool dispatch_aarch64_lowered_instruction(
    DispatchContext &context, AArch64MachineFunction &machine_function,
    AArch64MachineBlock &machine_block, const CoreIrBasicBlock *current_block,
    const CoreIrInstruction &instruction, FunctionState &state) {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Phi:
        return true;
    case CoreIrOpcode::Load:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_load(
            machine_block, static_cast<const CoreIrLoadInst &>(instruction),
            state);
    case CoreIrOpcode::Store:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_store(
            machine_block, static_cast<const CoreIrStoreInst &>(instruction),
            state);
    case CoreIrOpcode::Binary:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_binary(
            machine_block, static_cast<const CoreIrBinaryInst &>(instruction),
            state);
    case CoreIrOpcode::Unary:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_unary(
            machine_block, static_cast<const CoreIrUnaryInst &>(instruction),
            state);
    case CoreIrOpcode::Compare:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_compare(
            machine_block, static_cast<const CoreIrCompareInst &>(instruction),
            state);
    case CoreIrOpcode::Select:
    case CoreIrOpcode::ExtractElement:
    case CoreIrOpcode::InsertElement:
    case CoreIrOpcode::ShuffleVector:
    case CoreIrOpcode::VectorReduceAdd:
        return false;
    case CoreIrOpcode::Cast:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_cast(
            machine_block, static_cast<const CoreIrCastInst &>(instruction),
            state);
    case CoreIrOpcode::Call:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_call(
            machine_block, static_cast<const CoreIrCallInst &>(instruction),
            state);
    case CoreIrOpcode::Jump:
        context.emit_debug_location(machine_block, instruction.get_source_span(), state);
        machine_block.append_instruction(AArch64MachineInstr(
            "b",
            {AArch64MachineOperand::label(context.resolve_branch_target_label(
                state, current_block,
                static_cast<const CoreIrJumpInst &>(instruction).get_target_block()))}));
        return true;
    case CoreIrOpcode::CondJump:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_cond_jump(
            machine_block, static_cast<const CoreIrCondJumpInst &>(instruction),
            state, current_block);
    case CoreIrOpcode::Return:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_return(
            machine_function, machine_block,
            static_cast<const CoreIrReturnInst &>(instruction), state);
    case CoreIrOpcode::AddressOfStackSlot:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_address_of_stack_slot(
            machine_block,
            static_cast<const CoreIrAddressOfStackSlotInst &>(instruction),
            state);
    case CoreIrOpcode::AddressOfGlobal:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_address_of_global(
            machine_block,
            static_cast<const CoreIrAddressOfGlobalInst &>(instruction), state);
    case CoreIrOpcode::AddressOfFunction:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_address_of_function(
            machine_block,
            static_cast<const CoreIrAddressOfFunctionInst &>(instruction),
            state);
    case CoreIrOpcode::GetElementPtr:
        context.emit_debug_location(machine_block,
                                    instruction.get_source_span(), state);
        return context.emit_getelementptr(
            machine_block,
            static_cast<const CoreIrGetElementPtrInst &>(instruction), state);
    }
    return false;
}

} // namespace sysycc
