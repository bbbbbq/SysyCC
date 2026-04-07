#pragma once

#include <memory>
#include <unordered_map>

namespace sysycc {

class CoreIrInstruction;
class CoreIrValue;

namespace detail {

std::unique_ptr<CoreIrInstruction> clone_instruction_remapped(
    const CoreIrInstruction &instruction,
    const std::unordered_map<const CoreIrValue *, CoreIrValue *> &value_map);

} // namespace detail

} // namespace sysycc
