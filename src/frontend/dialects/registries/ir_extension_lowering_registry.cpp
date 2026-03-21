#include "frontend/dialects/registries/ir_extension_lowering_registry.hpp"

#include <utility>

namespace sysycc {

namespace {

const std::string &empty_owner_name() {
    static const std::string kEmptyOwnerName;
    return kEmptyOwnerName;
}

} // namespace

void IrExtensionLoweringRegistry::add_handler(
    IrExtensionLoweringHandlerKind handler_kind, std::string owner_name) {
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

bool IrExtensionLoweringRegistry::has_handler(
    IrExtensionLoweringHandlerKind handler_kind) const noexcept {
    return owner_names_.find(handler_kind) != owner_names_.end();
}

const std::string &IrExtensionLoweringRegistry::get_owner_name(
    IrExtensionLoweringHandlerKind handler_kind) const noexcept {
    const auto iterator = owner_names_.find(handler_kind);
    if (iterator == owner_names_.end()) {
        return empty_owner_name();
    }
    return iterator->second;
}

const std::vector<std::string> &
IrExtensionLoweringRegistry::get_registration_errors() const noexcept {
    return registration_errors_;
}

} // namespace sysycc
