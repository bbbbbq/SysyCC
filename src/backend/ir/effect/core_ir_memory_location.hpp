#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sysycc {

class CoreIrGlobal;
class CoreIrParameter;
class CoreIrStackSlot;
class CoreIrValue;

enum class CoreIrMemoryLocationKind : unsigned char {
    StackSlot,
    Global,
    ArgumentDerived,
    Unknown,
};

struct CoreIrMemoryLocation {
    CoreIrMemoryLocationKind kind = CoreIrMemoryLocationKind::Unknown;
    CoreIrStackSlot *stack_slot = nullptr;
    CoreIrGlobal *global = nullptr;
    CoreIrParameter *parameter = nullptr;
    std::size_t parameter_index = 0;
    std::vector<std::uint64_t> access_path;

    static CoreIrMemoryLocation make_unknown() noexcept {
        return CoreIrMemoryLocation{};
    }

    bool is_unknown() const noexcept {
        return kind == CoreIrMemoryLocationKind::Unknown;
    }
};

CoreIrMemoryLocation describe_memory_location(CoreIrValue *value);

} // namespace sysycc
