#include "frontend/semantic/type_system/extended_builtin_type_semantic_handler.hpp"

#include <memory>
#include <string_view>

#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

namespace {

bool matches_builtin_type_name(const SemanticType *type,
                               std::string_view name) noexcept {
    return type != nullptr && type->get_kind() == SemanticTypeKind::Builtin &&
           static_cast<const BuiltinSemanticType *>(type)->get_name() == name;
}

} // namespace

bool ExtendedBuiltinTypeSemanticHandler::is_extended_scalar_builtin_name(
    std::string_view name) const noexcept {
    return name == "_Float16";
}

const SemanticType *
ExtendedBuiltinTypeSemanticHandler::get_usual_arithmetic_conversion_type(
    const SemanticType *lhs, const SemanticType *rhs,
    SemanticModel &semantic_model) const {
    if (matches_builtin_type_name(lhs, "_Float16") ||
        matches_builtin_type_name(rhs, "_Float16")) {
        return semantic_model.own_type(
            std::make_unique<BuiltinSemanticType>("_Float16"));
    }
    return nullptr;
}

} // namespace sysycc::detail
