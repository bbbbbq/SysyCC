#pragma once

#include <map>
#include <string>
#include <vector>

namespace sysycc {

enum class IrExtensionLoweringHandlerKind : unsigned char {
    GnuFunctionAttributes,
};

class IrExtensionLoweringRegistry {
  private:
    std::map<IrExtensionLoweringHandlerKind, std::string> owner_names_;
    std::vector<std::string> registration_errors_;

  public:
    void add_handler(IrExtensionLoweringHandlerKind handler_kind,
                     std::string owner_name);

    bool has_handler(IrExtensionLoweringHandlerKind handler_kind) const noexcept;

    const std::string &
    get_owner_name(IrExtensionLoweringHandlerKind handler_kind) const noexcept;

    const std::vector<std::string> &get_registration_errors() const noexcept;
};

} // namespace sysycc
