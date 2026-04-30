#include "frontend/preprocess/detail/macro_table.hpp"

#include <cctype>

namespace sysycc::preprocess::detail {

namespace {

std::string normalize_replacement_for_equivalence(const std::string &replacement) {
    std::string normalized;
    bool in_string_literal = false;
    bool in_char_literal = false;
    bool escaping = false;
    bool pending_space = false;

    for (const char current : replacement) {
        const auto byte = static_cast<unsigned char>(current);
        if (in_string_literal || in_char_literal) {
            if (pending_space && !normalized.empty()) {
                normalized.push_back(' ');
                pending_space = false;
            }
            normalized.push_back(current);
            if (escaping) {
                escaping = false;
            } else if (current == '\\') {
                escaping = true;
            } else if (in_string_literal && current == '"') {
                in_string_literal = false;
            } else if (in_char_literal && current == '\'') {
                in_char_literal = false;
            }
            continue;
        }

        if (std::isspace(byte) != 0) {
            pending_space = true;
            continue;
        }

        if (pending_space && !normalized.empty()) {
            normalized.push_back(' ');
        }
        pending_space = false;
        normalized.push_back(current);
        if (current == '"') {
            in_string_literal = true;
        } else if (current == '\'') {
            in_char_literal = true;
        }
    }

    return normalized;
}

} // namespace

void MacroTable::clear() { macro_definitions_.clear(); }

bool MacroTable::has_macro(const std::string &name) const {
    return macro_definitions_.find(name) != macro_definitions_.end();
}

const MacroDefinition *
MacroTable::get_macro_definition(const std::string &name) const noexcept {
    const auto iter = macro_definitions_.find(name);
    if (iter == macro_definitions_.end()) {
        return nullptr;
    }

    return &iter->second;
}

PassResult MacroTable::define_macro(const MacroDefinition &definition,
                                    bool allow_redefinition) {
    const MacroDefinition *existing_definition =
        get_macro_definition(definition.get_name());
    if (existing_definition != nullptr) {
        MacroDefinition normalized_existing = *existing_definition;
        normalized_existing.set_replacement(
            normalize_replacement_for_equivalence(
                normalized_existing.get_replacement()));
        MacroDefinition normalized_definition = definition;
        normalized_definition.set_replacement(
            normalize_replacement_for_equivalence(
                normalized_definition.get_replacement()));
        if (normalized_existing.is_equivalent_to(normalized_definition)) {
            return PassResult::Success();
        }
        if (allow_redefinition) {
            macro_definitions_[definition.get_name()] = definition;
            return PassResult::Success();
        }
        return PassResult::Failure("macro redefinition is not allowed: " +
                                   definition.get_name());
    }

    macro_definitions_[definition.get_name()] = definition;
    return PassResult::Success();
}

void MacroTable::undefine_macro(const std::string &name) {
    macro_definitions_.erase(name);
}

} // namespace sysycc::preprocess::detail
