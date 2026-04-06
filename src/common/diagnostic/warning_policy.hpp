#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

#include "common/diagnostic/warning_catalog.hpp"

namespace sysycc {

class WarningPolicy {
  private:
    bool wall_enabled_ = false;
    bool wextra_enabled_ = false;
    bool treat_all_warnings_as_errors_ = false;
    std::unordered_set<std::string> enabled_warnings_;
    std::unordered_set<std::string> disabled_warnings_;
    std::unordered_set<std::string> warnings_as_errors_;
    std::unordered_set<std::string> warnings_not_as_errors_;

  public:
    void enable_wall() noexcept { wall_enabled_ = true; }
    void enable_wextra() noexcept { wextra_enabled_ = true; }

    bool wall_enabled() const noexcept { return wall_enabled_; }
    bool wextra_enabled() const noexcept { return wextra_enabled_; }

    void set_all_warnings_as_errors(bool enabled) noexcept {
        treat_all_warnings_as_errors_ = enabled;
    }

    bool all_warnings_as_errors() const noexcept {
        return treat_all_warnings_as_errors_;
    }

    void enable_warning(std::string warning_option) {
        enabled_warnings_.insert(warning_option);
        disabled_warnings_.erase(warning_option);
    }

    void disable_warning(std::string warning_option) {
        enabled_warnings_.erase(warning_option);
        disabled_warnings_.insert(std::move(warning_option));
    }

    bool should_emit_warning(std::string_view warning_option) const {
        if (warning_option.empty()) {
            return true;
        }

        const std::string option(warning_option);
        if (disabled_warnings_.find(option) != disabled_warnings_.end()) {
            return false;
        }
        if (enabled_warnings_.find(option) != enabled_warnings_.end()) {
            return true;
        }

        switch (get_warning_catalog_level(warning_option)) {
        case WarningCatalogLevel::DefaultOn:
            return true;
        case WarningCatalogLevel::Wall:
            return wall_enabled_;
        case WarningCatalogLevel::WExtra:
            return wextra_enabled_;
        case WarningCatalogLevel::DefaultOff:
            return false;
        case WarningCatalogLevel::Unknown:
            return true;
        }

        return true;
    }

    void set_warning_as_error(std::string warning_option) {
        enabled_warnings_.insert(warning_option);
        disabled_warnings_.erase(warning_option);
        warnings_not_as_errors_.erase(warning_option);
        warnings_as_errors_.insert(std::move(warning_option));
    }

    void set_warning_not_as_error(std::string warning_option) {
        warnings_as_errors_.erase(warning_option);
        warnings_not_as_errors_.insert(std::move(warning_option));
    }

    bool should_treat_warning_as_error(
        std::string_view warning_option) const {
        if (!warning_option.empty()) {
            const std::string option(warning_option);
            if (warnings_not_as_errors_.find(option) !=
                warnings_not_as_errors_.end()) {
                return false;
            }
            if (warnings_as_errors_.find(option) != warnings_as_errors_.end()) {
                return true;
            }
        }
        return treat_all_warnings_as_errors_;
    }
};

} // namespace sysycc
