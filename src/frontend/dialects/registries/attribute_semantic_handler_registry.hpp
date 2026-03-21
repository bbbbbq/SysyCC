#pragma once

#include <map>
#include <string>
#include <vector>

namespace sysycc {

enum class AttributeSemanticHandlerKind : unsigned char {
    GnuFunctionAttributes,
};

class AttributeSemanticHandlerRegistry {
  private:
    std::map<AttributeSemanticHandlerKind, std::string> owner_names_;
    std::vector<std::string> registration_errors_;

  public:
    void add_handler(AttributeSemanticHandlerKind handler_kind,
                     std::string owner_name);

    bool has_handler(AttributeSemanticHandlerKind handler_kind) const noexcept;

    const std::string &
    get_owner_name(AttributeSemanticHandlerKind handler_kind) const noexcept;

    const std::vector<std::string> &get_registration_errors() const noexcept;
};

} // namespace sysycc
