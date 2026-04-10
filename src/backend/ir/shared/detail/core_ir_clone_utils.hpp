#pragma once

#include <memory>
#include <unordered_map>

namespace sysycc {

class CoreIrBasicBlock;
class CoreIrInstruction;
class CoreIrStackSlot;
class CoreIrValue;

namespace detail {

std::unique_ptr<CoreIrInstruction> clone_instruction_remapped(
    const CoreIrInstruction &instruction,
    const std::unordered_map<const CoreIrValue *, CoreIrValue *> &value_map,
    const std::unordered_map<const CoreIrStackSlot *, CoreIrStackSlot *> *stack_slot_map =
        nullptr,
    const std::unordered_map<const CoreIrBasicBlock *, CoreIrBasicBlock *> *block_map =
        nullptr);

} // namespace detail

} // namespace sysycc
