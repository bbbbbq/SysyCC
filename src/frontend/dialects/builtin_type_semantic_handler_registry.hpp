#pragma once

#include <map>
#include <string>
#include <vector>

namespace sysycc {

enum class BuiltinTypeSemanticHandlerKind : unsigned char {
    ExtendedBuiltinScalarTypes,
};

class BuiltinTypeSemanticHandlerRegistry {
  private:
    std::map<BuiltinTypeSemanticHandlerKind, std::string> owner_names_;
    std::vector<std::string> registration_errors_;

  public:
    void add_handler(BuiltinTypeSemanticHandlerKind handler_kind,
                     std::string owner_name);

    bool has_handler(BuiltinTypeSemanticHandlerKind handler_kind) const noexcept;

    const std::string &
    get_owner_name(BuiltinTypeSemanticHandlerKind handler_kind) const noexcept;

    const std::vector<std::string> &get_registration_errors() const noexcept;
};

} // namespace sysycc
