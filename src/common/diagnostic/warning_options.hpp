#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace sysycc::warning_options {

inline constexpr char kUnusedVariable[] = "unused-variable";
inline constexpr char kUnusedParameter[] = "unused-parameter";
inline constexpr char kUnusedLabel[] = "unused-label";
inline constexpr char kUnusedButSetVariable[] = "unused-but-set-variable";
inline constexpr char kUnusedFunction[] = "unused-function";
inline constexpr char kUnusedValue[] = "unused-value";
inline constexpr char kSignCompare[] = "sign-compare";
inline constexpr char kConversion[] = "conversion";
inline constexpr char kConstantCondition[] = "constant-condition";
inline constexpr char kUnreachableCode[] = "unreachable-code";
inline constexpr char kIncompatiblePointerTypes[] = "incompatible-pointer-types";
inline constexpr char kUnknownType[] = "unknown-type";
inline constexpr char kReturnType[] = "return-type";

inline constexpr std::array<std::string_view, 13> kKnownOptions = {
    kUnusedVariable,       kUnusedParameter,   kUnusedLabel,
    kUnusedButSetVariable, kUnusedFunction,    kUnusedValue,
    kSignCompare,          kConversion,        kConstantCondition,
    kUnreachableCode,      kIncompatiblePointerTypes,
    kUnknownType,          kReturnType,
};

inline bool is_known_warning_option(std::string_view option) {
    return std::find(kKnownOptions.begin(), kKnownOptions.end(), option) !=
           kKnownOptions.end();
}

} // namespace sysycc::warning_options
