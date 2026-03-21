#include "frontend/dialects/registries/preprocess_directive_handler_registry.hpp"

#include <utility>

namespace sysycc {

namespace {

const std::string &empty_owner_name() {
    static const std::string kEmptyOwnerName;
    return kEmptyOwnerName;
}

} // namespace

void PreprocessDirectiveHandlerRegistry::add_handler(
    PreprocessDirectiveHandlerKind handler_kind, std::string owner_name) {
    const auto iterator = owner_names_.find(handler_kind);
    if (iterator == owner_names_.end()) {
        owner_names_.emplace(handler_kind, std::move(owner_name));
        return;
    }

    if (iterator->second == owner_name) {
        return;
    }

    registration_errors_.push_back("handler already owned by '" +
                                   iterator->second +
                                   "', cannot also be owned by '" +
                                   owner_name + "'");
}

bool PreprocessDirectiveHandlerRegistry::has_handler(
    PreprocessDirectiveHandlerKind handler_kind) const noexcept {
    return owner_names_.find(handler_kind) != owner_names_.end();
}

const std::string &PreprocessDirectiveHandlerRegistry::get_owner_name(
    PreprocessDirectiveHandlerKind handler_kind) const noexcept {
    const auto iterator = owner_names_.find(handler_kind);
    if (iterator == owner_names_.end()) {
        return empty_owner_name();
    }
    return iterator->second;
}

const std::vector<std::string> &
PreprocessDirectiveHandlerRegistry::get_registration_errors() const noexcept {
    return registration_errors_;
}

} // namespace sysycc
