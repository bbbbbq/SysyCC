#pragma once

#include <vector>

#include "frontend/attribute/gnu_function_attribute_handler.hpp"
#include "frontend/semantic/model/semantic_function_attribute.hpp"

namespace sysycc {

class FunctionDecl;

namespace detail {
class SemanticContext;
}

class AttributeAnalyzer {
  private:
    GnuFunctionAttributeHandler gnu_function_attribute_handler_;

  public:
    std::vector<SemanticFunctionAttribute>
    analyze_function_attributes(const FunctionDecl *function_decl,
                                detail::SemanticContext &semantic_context) const;
};

} // namespace sysycc
