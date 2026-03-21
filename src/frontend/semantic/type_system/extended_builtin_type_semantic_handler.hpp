#pragma once

#include <string_view>

namespace sysycc {

class SemanticModel;
class SemanticType;

namespace detail {

class ExtendedBuiltinTypeSemanticHandler {
  public:
    bool is_extended_scalar_builtin_name(std::string_view name) const noexcept;

    const SemanticType *get_usual_arithmetic_conversion_type(
        const SemanticType *lhs, const SemanticType *rhs,
        SemanticModel &semantic_model) const;
};

} // namespace detail
} // namespace sysycc
