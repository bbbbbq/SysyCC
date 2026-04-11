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
    const CoreIrStackSlot *stack_slot = nullptr;
    const CoreIrGlobal *global = nullptr;
    const CoreIrParameter *parameter = nullptr;
    std::size_t parameter_index = 0;
    std::vector<std::uint64_t> access_path;
    bool exact_access_path = true;

    static CoreIrMemoryLocation make_unknown() noexcept {
        return CoreIrMemoryLocation{};
    }

    bool is_unknown() const noexcept {
        return kind == CoreIrMemoryLocationKind::Unknown;
    }

    bool has_constant_path() const noexcept {
        return !is_unknown() && exact_access_path;
    }
};

CoreIrMemoryLocation describe_memory_location(const CoreIrValue *value);

} // namespace sysycc
