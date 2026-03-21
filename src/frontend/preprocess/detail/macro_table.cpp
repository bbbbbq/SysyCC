#include "frontend/preprocess/detail/macro_table.hpp"

namespace sysycc::preprocess::detail {

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
        if (existing_definition->is_equivalent_to(definition)) {
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
