#pragma once

namespace sysycc {

enum class PointerNullabilityKind : unsigned char {
    None,
    Nullable,
    Nonnull,
    NullUnspecified,
};

} // namespace sysycc
