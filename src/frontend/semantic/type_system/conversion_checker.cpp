#include "frontend/semantic/type_system/conversion_checker.hpp"

#include <memory>

#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

bool ConversionChecker::is_assignable_expr(const Expr *expr) const {
    if (expr == nullptr) {
        return false;
    }

    switch (expr->get_kind()) {
    case AstKind::IdentifierExpr:
    case AstKind::IndexExpr:
    case AstKind::MemberExpr:
        return true;
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        return unary_expr->get_operator_text() == "*";
    }
    default:
        return false;
    }
}

bool ConversionChecker::is_same_type(const SemanticType *lhs,
                                     const SemanticType *rhs) const {
    if (lhs == rhs) {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    if (lhs->get_kind() != rhs->get_kind()) {
        return false;
    }

    switch (lhs->get_kind()) {
    case SemanticTypeKind::Builtin:
        return static_cast<const BuiltinSemanticType *>(lhs)->get_name() ==
               static_cast<const BuiltinSemanticType *>(rhs)->get_name();
    case SemanticTypeKind::Pointer:
        return is_same_type(
            static_cast<const PointerSemanticType *>(lhs)->get_pointee_type(),
            static_cast<const PointerSemanticType *>(rhs)->get_pointee_type());
    case SemanticTypeKind::Struct:
        return static_cast<const StructSemanticType *>(lhs)->get_name() ==
               static_cast<const StructSemanticType *>(rhs)->get_name();
    case SemanticTypeKind::Enum:
        return static_cast<const EnumSemanticType *>(lhs)->get_name() ==
               static_cast<const EnumSemanticType *>(rhs)->get_name();
    case SemanticTypeKind::Array: {
        const auto *lhs_array = static_cast<const ArraySemanticType *>(lhs);
        const auto *rhs_array = static_cast<const ArraySemanticType *>(rhs);
        return lhs_array->get_dimensions() == rhs_array->get_dimensions() &&
               is_same_type(lhs_array->get_element_type(),
                            rhs_array->get_element_type());
    }
    case SemanticTypeKind::Function: {
        const auto *lhs_function = static_cast<const FunctionSemanticType *>(lhs);
        const auto *rhs_function = static_cast<const FunctionSemanticType *>(rhs);
        if (!is_same_type(lhs_function->get_return_type(),
                          rhs_function->get_return_type()) ||
            lhs_function->get_parameter_types().size() !=
                rhs_function->get_parameter_types().size()) {
            return false;
        }
        for (std::size_t index = 0;
             index < lhs_function->get_parameter_types().size(); ++index) {
            if (!is_same_type(lhs_function->get_parameter_types()[index],
                              rhs_function->get_parameter_types()[index])) {
                return false;
            }
        }
        return true;
    }
    }

    return false;
}

bool ConversionChecker::is_assignable_type(const SemanticType *target,
                                           const SemanticType *value) const {
    if (is_same_type(target, value)) {
        return true;
    }
    if (target == nullptr || value == nullptr) {
        return false;
    }
    if (target->get_kind() == SemanticTypeKind::Pointer &&
        value->get_kind() == SemanticTypeKind::Pointer) {
        return is_same_type(target, value);
    }
    if (target->get_kind() != SemanticTypeKind::Builtin ||
        value->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }

    const auto &target_name =
        static_cast<const BuiltinSemanticType *>(target)->get_name();
    const auto &value_name =
        static_cast<const BuiltinSemanticType *>(value)->get_name();
    if ((target_name == "float" || target_name == "int" ||
         target_name == "char") &&
        (value_name == "float" || value_name == "int" ||
         value_name == "char")) {
        return true;
    }
    return false;
}

bool ConversionChecker::is_assignable_value(
    const SemanticType *target, const SemanticType *value, const Expr *value_expr,
    SemanticContext &semantic_context,
    const ConstantEvaluator &constant_evaluator) const {
    if (target == nullptr || value == nullptr) {
        return false;
    }

    SemanticModel &semantic_model = semantic_context.get_semantic_model();
    if (is_assignable_type(target, value)) {
        return true;
    }

    if (is_pointer_type(target)) {
        if (is_same_or_decayed_pointer_target(target, value, semantic_model)) {
            return true;
        }
        if (is_null_pointer_constant(value_expr, semantic_context,
                                     constant_evaluator)) {
            return true;
        }
    }

    const SemanticType *decayed_target = get_decayed_type(target, semantic_model);
    const SemanticType *decayed_value = get_decayed_type(value, semantic_model);
    if (decayed_target != target || decayed_value != value) {
        return is_assignable_type(decayed_target, decayed_value);
    }
    return false;
}

bool ConversionChecker::is_compatible_equality_type(
    const SemanticType *lhs, const SemanticType *rhs, const Expr *lhs_expr,
    const Expr *rhs_expr, SemanticContext &semantic_context,
    const ConstantEvaluator &constant_evaluator) const {
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    if (is_arithmetic_type(lhs) && is_arithmetic_type(rhs)) {
        return true;
    }
    if (lhs->get_kind() == SemanticTypeKind::Pointer &&
        rhs->get_kind() == SemanticTypeKind::Pointer) {
        return is_same_type(lhs, rhs);
    }

    const auto lhs_constant =
        constant_evaluator.get_integer_constant_value(lhs_expr, semantic_context);
    const auto rhs_constant =
        constant_evaluator.get_integer_constant_value(rhs_expr, semantic_context);
    if (lhs->get_kind() == SemanticTypeKind::Pointer &&
        rhs_constant.has_value() && *rhs_constant == 0) {
        return true;
    }
    if (rhs->get_kind() == SemanticTypeKind::Pointer &&
        lhs_constant.has_value() && *lhs_constant == 0) {
        return true;
    }
    return false;
}

const SemanticType *ConversionChecker::get_usual_arithmetic_conversion_type(
    const SemanticType *lhs, const SemanticType *rhs,
    SemanticModel &semantic_model) const {
    if (!is_arithmetic_type(lhs) || !is_arithmetic_type(rhs)) {
        return nullptr;
    }
    if (lhs != nullptr && lhs->get_kind() == SemanticTypeKind::Builtin &&
        static_cast<const BuiltinSemanticType *>(lhs)->get_name() == "float") {
        return semantic_model.own_type(
            std::make_unique<BuiltinSemanticType>("float"));
    }
    if (rhs != nullptr && rhs->get_kind() == SemanticTypeKind::Builtin &&
        static_cast<const BuiltinSemanticType *>(rhs)->get_name() == "float") {
        return semantic_model.own_type(
            std::make_unique<BuiltinSemanticType>("float"));
    }
    return semantic_model.own_type(std::make_unique<BuiltinSemanticType>("int"));
}

bool ConversionChecker::is_arithmetic_type(const SemanticType *type) const {
    if (type == nullptr) {
        return false;
    }
    if (type->get_kind() == SemanticTypeKind::Enum) {
        return true;
    }
    if (type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
    return name == "int" || name == "char" || name == "float";
}

bool ConversionChecker::is_scalar_type(const SemanticType *type) const {
    if (type == nullptr) {
        return false;
    }
    switch (type->get_kind()) {
    case SemanticTypeKind::Builtin:
    case SemanticTypeKind::Pointer:
    case SemanticTypeKind::Enum:
        return true;
    case SemanticTypeKind::Array:
    case SemanticTypeKind::Function:
    case SemanticTypeKind::Struct:
        return false;
    }
    return false;
}

bool ConversionChecker::is_integer_like_type(const SemanticType *type) const {
    if (type == nullptr) {
        return false;
    }
    if (type->get_kind() == SemanticTypeKind::Enum) {
        return true;
    }
    if (type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
    return name == "int" || name == "char";
}

bool ConversionChecker::is_incrementable_type(const SemanticType *type) const {
    if (type == nullptr) {
        return false;
    }
    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return true;
    }
    if (type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
    return name == "int" || name == "float" || name == "char";
}

bool ConversionChecker::is_void_type(const SemanticType *type) const {
    return type != nullptr && type->get_kind() == SemanticTypeKind::Builtin &&
           static_cast<const BuiltinSemanticType *>(type)->get_name() == "void";
}

bool ConversionChecker::is_pointer_type(const SemanticType *type) const {
    return type != nullptr && type->get_kind() == SemanticTypeKind::Pointer;
}

bool ConversionChecker::is_struct_type(const SemanticType *type) const {
    return type != nullptr && type->get_kind() == SemanticTypeKind::Struct;
}

bool ConversionChecker::is_pointer_to_struct_type(const SemanticType *type) const {
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Pointer) {
        return false;
    }
    const auto *pointer_type = static_cast<const PointerSemanticType *>(type);
    return pointer_type->get_pointee_type() != nullptr &&
           is_struct_type(pointer_type->get_pointee_type());
}

bool ConversionChecker::is_null_pointer_constant(
    const Expr *expr, SemanticContext &semantic_context,
    const ConstantEvaluator &constant_evaluator) const {
    const auto value =
        constant_evaluator.get_integer_constant_value(expr, semantic_context);
    return value.has_value() && *value == 0;
}

const SemanticType *ConversionChecker::get_decayed_type(
    const SemanticType *type, SemanticModel &semantic_model) const {
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Array) {
        return type;
    }
    const auto *array_type = static_cast<const ArraySemanticType *>(type);
    return semantic_model.own_type(
        std::make_unique<PointerSemanticType>(array_type->get_element_type()));
}

// `target` and `value` are intentionally ordered to match assignment semantics.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool ConversionChecker::is_same_or_decayed_pointer_target(
    const SemanticType *target, const SemanticType *value,
    SemanticModel &semantic_model) const { // NOLINT(bugprone-easily-swappable-parameters)
    if (!is_pointer_type(target)) {
        return false;
    }
    const SemanticType *decayed_value = get_decayed_type(value, semantic_model);
    return is_pointer_type(decayed_value) && is_same_type(target, decayed_value);
}

} // namespace sysycc::detail
