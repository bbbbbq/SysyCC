#pragma once

#include <vector>

#include "backend/ir/ir_backend.hpp"
#include "frontend/semantic/model/semantic_function_attribute.hpp"

namespace sysycc {

class GnuFunctionAttributeLoweringHandler {
  public:
    std::vector<IRFunctionAttribute> lower_function_attributes(
        const std::vector<SemanticFunctionAttribute> &semantic_attributes) const;
};

} // namespace sysycc
