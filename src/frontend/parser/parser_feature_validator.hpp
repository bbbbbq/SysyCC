#pragma once

#include "frontend/dialects/registries/parser_feature_registry.hpp"
#include "frontend/parser/parser_runtime.hpp"

namespace sysycc {

class ParserFeatureValidator {
  public:
    bool validate(const ParseTreeNode *parse_tree_root,
                  const ParserFeatureRegistry &feature_registry,
                  ParserErrorInfo &error_info) const;
};

} // namespace sysycc
