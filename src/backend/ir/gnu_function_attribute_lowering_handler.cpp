#include "backend/ir/gnu_function_attribute_lowering_handler.hpp"

namespace sysycc {

std::vector<IRFunctionAttribute>
GnuFunctionAttributeLoweringHandler::lower_function_attributes(
    const std::vector<SemanticFunctionAttribute> &semantic_attributes) const {
    std::vector<IRFunctionAttribute> ir_attributes;
    for (const SemanticFunctionAttribute semantic_attribute :
         semantic_attributes) {
        if (semantic_attribute == SemanticFunctionAttribute::AlwaysInline) {
            ir_attributes.push_back(IRFunctionAttribute::AlwaysInline);
        }
    }
    return ir_attributes;
}

} // namespace sysycc
