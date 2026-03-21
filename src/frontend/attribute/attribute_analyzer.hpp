#pragma once

#include <vector>

#include "frontend/semantic/model/semantic_function_attribute.hpp"

namespace sysycc {

class FunctionDecl;

namespace detail {
class SemanticContext;
}

class AttributeAnalyzer {
  public:
    std::vector<SemanticFunctionAttribute>
    analyze_function_attributes(const FunctionDecl *function_decl,
                                detail::SemanticContext &semantic_context) const;
};

} // namespace sysycc
