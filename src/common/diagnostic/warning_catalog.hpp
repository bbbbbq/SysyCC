#pragma once

#include <string_view>

#include "common/diagnostic/warning_options.hpp"

namespace sysycc {

enum class WarningCatalogLevel : unsigned char {
    DefaultOn,
    Wall,
    WExtra,
    DefaultOff,
    Unknown,
};

inline WarningCatalogLevel get_warning_catalog_level(
    std::string_view warning_option) noexcept {
    using namespace warning_options;

    if (warning_option == kUnusedVariable ||
        warning_option == kUnusedParameter ||
        warning_option == kUnusedLabel ||
        warning_option == kUnusedButSetVariable ||
        warning_option == kUnusedFunction ||
        warning_option == kIncompatiblePointerTypes ||
        warning_option == kUnknownType) {
        return WarningCatalogLevel::DefaultOn;
    }

    if (warning_option == kUnusedValue || warning_option == kSignCompare ||
        warning_option == kConstantCondition ||
        warning_option == kUnreachableCode) {
        return WarningCatalogLevel::Wall;
    }

    if (warning_option == kConversion) {
        return WarningCatalogLevel::WExtra;
    }

    if (warning_option.empty()) {
        return WarningCatalogLevel::DefaultOn;
    }

    return WarningCatalogLevel::Unknown;
}

} // namespace sysycc
