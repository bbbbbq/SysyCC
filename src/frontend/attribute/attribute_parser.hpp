#pragma once

#include "frontend/attribute/attribute.hpp"
#include "frontend/parser/parser_runtime.hpp"

namespace sysycc {

class AttributeParser {
  public:
    ParsedAttributeList parse_gnu_attribute_specifier_seq(
        const ParseTreeNode *node,
        AttributeAttachmentSite attachment_site) const;
};

} // namespace sysycc
