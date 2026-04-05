#pragma once

namespace sysycc {

class CoreIrFunction;
class CoreIrInstruction;

enum class CoreIrMemoryBehavior : unsigned char {
    None,
    Read,
    Write,
    ReadWrite,
};

struct CoreIrEffectInfo {
    CoreIrMemoryBehavior memory_behavior = CoreIrMemoryBehavior::None;
    bool has_control_effect = false;
    bool may_capture_pointer_operands = false;
    bool is_pure_value = false;
};

CoreIrMemoryBehavior
merge_memory_behavior(CoreIrMemoryBehavior lhs, CoreIrMemoryBehavior rhs) noexcept;

bool memory_behavior_reads(CoreIrMemoryBehavior behavior) noexcept;

bool memory_behavior_writes(CoreIrMemoryBehavior behavior) noexcept;

CoreIrEffectInfo get_core_ir_instruction_effect(
    const CoreIrInstruction &instruction) noexcept;

CoreIrEffectInfo summarize_core_ir_function_effect(
    const CoreIrFunction &function) noexcept;

} // namespace sysycc
