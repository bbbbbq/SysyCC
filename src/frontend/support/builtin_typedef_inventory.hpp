#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace sysycc {

enum class BuiltinTypedefGroup : std::uint8_t {
    SignedChar,
    UnsignedChar,
    Short,
    UnsignedShort,
    Int,
    UnsignedInt,
    Long,
    UnsignedLong,
    LongLong,
    UnsignedLongLong,
    VaList,
};

struct BuiltinTypedefInventoryEntry {
    std::string_view name;
    BuiltinTypedefGroup group;
};

inline constexpr std::array<BuiltinTypedefInventoryEntry, 47>
    kBuiltinTypedefInventory = {{
        {"_Bool", BuiltinTypedefGroup::Int},
        {"int8_t", BuiltinTypedefGroup::SignedChar},
        {"uint8_t", BuiltinTypedefGroup::UnsignedChar},
        {"__int8_t", BuiltinTypedefGroup::SignedChar},
        {"__uint8_t", BuiltinTypedefGroup::UnsignedChar},
        {"int16_t", BuiltinTypedefGroup::Short},
        {"uint16_t", BuiltinTypedefGroup::UnsignedShort},
        {"__int16_t", BuiltinTypedefGroup::Short},
        {"__uint16_t", BuiltinTypedefGroup::UnsignedShort},
        {"int32_t", BuiltinTypedefGroup::Int},
        {"uint32_t", BuiltinTypedefGroup::UnsignedInt},
        {"__int32_t", BuiltinTypedefGroup::Int},
        {"__uint32_t", BuiltinTypedefGroup::UnsignedInt},
        {"int64_t", BuiltinTypedefGroup::LongLong},
        {"uint64_t", BuiltinTypedefGroup::UnsignedLongLong},
        {"__int64_t", BuiltinTypedefGroup::LongLong},
        {"__uint64_t", BuiltinTypedefGroup::UnsignedLongLong},
        {"__int128_t", BuiltinTypedefGroup::LongLong},
        {"__uint128_t", BuiltinTypedefGroup::UnsignedLongLong},
        {"intptr_t", BuiltinTypedefGroup::Long},
        {"uintptr_t", BuiltinTypedefGroup::UnsignedLong},
        {"__INTPTR_TYPE__", BuiltinTypedefGroup::Long},
        {"__UINTPTR_TYPE__", BuiltinTypedefGroup::UnsignedLong},
        {"ptrdiff_t", BuiltinTypedefGroup::Long},
        {"__PTRDIFF_TYPE__", BuiltinTypedefGroup::Long},
        {"size_t", BuiltinTypedefGroup::UnsignedLong},
        {"__SIZE_TYPE__", BuiltinTypedefGroup::UnsignedLong},
        {"intmax_t", BuiltinTypedefGroup::Long},
        {"uintmax_t", BuiltinTypedefGroup::UnsignedLong},
        {"__INTMAX_TYPE__", BuiltinTypedefGroup::Long},
        {"__UINTMAX_TYPE__", BuiltinTypedefGroup::UnsignedLong},
        {"__darwin_intptr_t", BuiltinTypedefGroup::Long},
        {"__darwin_natural_t", BuiltinTypedefGroup::UnsignedInt},
        {"__darwin_ptrdiff_t", BuiltinTypedefGroup::Long},
        {"__darwin_size_t", BuiltinTypedefGroup::UnsignedLong},
        {"va_list", BuiltinTypedefGroup::VaList},
        {"__builtin_va_list", BuiltinTypedefGroup::VaList},
        {"__darwin_va_list", BuiltinTypedefGroup::VaList},
        {"wchar_t", BuiltinTypedefGroup::Int},
        {"__WCHAR_TYPE__", BuiltinTypedefGroup::Int},
        {"__WINT_TYPE__", BuiltinTypedefGroup::Int},
        {"__darwin_ct_rune_t", BuiltinTypedefGroup::Int},
        {"__darwin_wchar_t", BuiltinTypedefGroup::Int},
        {"__darwin_rune_t", BuiltinTypedefGroup::Int},
        {"__darwin_wint_t", BuiltinTypedefGroup::Int},
        {"__darwin_nl_item", BuiltinTypedefGroup::Int},
        {"__darwin_wctrans_t", BuiltinTypedefGroup::Int},
    }};

inline constexpr std::array<BuiltinTypedefInventoryEntry, 3>
    kBuiltinTypedefInventoryTail = {{
        {"__darwin_wctype_t", BuiltinTypedefGroup::UnsignedInt},
        {"__darwin_clock_t", BuiltinTypedefGroup::UnsignedLong},
        {"__darwin_socklen_t", BuiltinTypedefGroup::UnsignedInt},
    }};

inline constexpr std::array<BuiltinTypedefInventoryEntry, 2>
    kBuiltinTypedefInventoryTail2 = {{
        {"__darwin_ssize_t", BuiltinTypedefGroup::Long},
        {"__darwin_time_t", BuiltinTypedefGroup::Long},
    }};

template <typename Callback>
inline void for_each_builtin_typedef_inventory_entry(Callback &&callback) {
    for (const auto &entry : kBuiltinTypedefInventory) {
        callback(entry);
    }
    for (const auto &entry : kBuiltinTypedefInventoryTail) {
        callback(entry);
    }
    for (const auto &entry : kBuiltinTypedefInventoryTail2) {
        callback(entry);
    }
}

} // namespace sysycc
