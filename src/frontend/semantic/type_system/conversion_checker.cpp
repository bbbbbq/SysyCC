#include "frontend/semantic/type_system/conversion_checker.hpp"

#include <memory>

#include "frontend/ast/ast_node.hpp"
#include "frontend/dialects/registries/builtin_type_semantic_handler_registry.hpp"
#include "frontend/dialects/registries/semantic_feature_registry.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"
#include "frontend/semantic/type_system/extended_builtin_type_semantic_handler.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

namespace {

const SemanticType *strip_qualifiers(const SemanticType *type) {
    const SemanticType *current = type;
    while (current != nullptr &&
           current->get_kind() == SemanticTypeKind::Qualified) {
        current =
            static_cast<const QualifiedSemanticType *>(current)->get_base_type();
    }
    return current;
}

struct TypeQualifiers {
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;
    const SemanticType *base_type = nullptr;
};

TypeQualifiers split_top_level_qualifiers(const SemanticType *type) {
    TypeQualifiers qualifiers;
    qualifiers.base_type = type;
    while (qualifiers.base_type != nullptr &&
           qualifiers.base_type->get_kind() == SemanticTypeKind::Qualified) {
        const auto *qualified_type =
            static_cast<const QualifiedSemanticType *>(qualifiers.base_type);
        qualifiers.is_const =
            qualifiers.is_const || qualified_type->get_is_const();
        qualifiers.is_volatile =
            qualifiers.is_volatile || qualified_type->get_is_volatile();
        qualifiers.is_restrict =
            qualifiers.is_restrict || qualified_type->get_is_restrict();
        qualifiers.base_type = qualified_type->get_base_type();
    }
    return qualifiers;
}

bool qualifiers_are_superset(const TypeQualifiers &target,
                             const TypeQualifiers &value) {
    return (!value.is_const || target.is_const) &&
           (!value.is_volatile || target.is_volatile) &&
           (!value.is_restrict || target.is_restrict);
}

bool drops_qualifiers_in_pointer_conversion(const SemanticType *target,
                                            const SemanticType *value) {
    const SemanticType *unqualified_target = strip_qualifiers(target);
    const SemanticType *unqualified_value = strip_qualifiers(value);
    if (unqualified_target == nullptr || unqualified_value == nullptr ||
        unqualified_target->get_kind() != SemanticTypeKind::Pointer ||
        unqualified_value->get_kind() != SemanticTypeKind::Pointer) {
        return false;
    }

    const auto *target_pointer =
        static_cast<const PointerSemanticType *>(unqualified_target);
    const auto *value_pointer =
        static_cast<const PointerSemanticType *>(unqualified_value);
    const TypeQualifiers target_qualifiers =
        split_top_level_qualifiers(target_pointer->get_pointee_type());
    const TypeQualifiers value_qualifiers =
        split_top_level_qualifiers(value_pointer->get_pointee_type());

    if (!qualifiers_are_superset(target_qualifiers, value_qualifiers)) {
        return true;
    }

    return drops_qualifiers_in_pointer_conversion(target_qualifiers.base_type,
                                                  value_qualifiers.base_type);
}

bool is_qualified_pointee_type(const SemanticType *type) {
    return type != nullptr && type->get_kind() == SemanticTypeKind::Qualified &&
           (static_cast<const QualifiedSemanticType *>(type)->get_is_const() ||
            static_cast<const QualifiedSemanticType *>(type)->get_is_volatile() ||
           static_cast<const QualifiedSemanticType *>(type)->get_is_restrict());
}

bool is_void_type_name(const SemanticType *type) {
    return type != nullptr && type->get_kind() == SemanticTypeKind::Builtin &&
           static_cast<const BuiltinSemanticType *>(type)->get_name() == "void";
}

bool is_void_pointer_type(const SemanticType *type) {
    const SemanticType *unqualified_type = strip_qualifiers(type);
    if (unqualified_type == nullptr ||
        unqualified_type->get_kind() != SemanticTypeKind::Pointer) {
        return false;
    }
    const auto *pointer_type =
        static_cast<const PointerSemanticType *>(unqualified_type);
    return is_void_type_name(strip_qualifiers(pointer_type->get_pointee_type()));
}

bool is_standard_numeric_builtin_name(const std::string &name) {
    return name == "long double" || name == "double" || name == "long long int" ||
           name == "long int" || name == "float" || name == "int" ||
           name == "ptrdiff_t" || name == "size_t" ||
           name == "short" || name == "signed char" ||
           name == "unsigned int" || name == "unsigned short" ||
           name == "unsigned long" ||
           name == "unsigned long long" || name == "unsigned char" ||
           name == "char";
}

bool is_value_representable_in_integer_type(long long value,
                                            const IntegerTypeInfo &type_info) {
    if (!type_info.get_is_signed()) {
        if (value < 0) {
            return false;
        }
        if (type_info.get_bit_width() >= 63) {
            return true;
        }
        const unsigned long long max_value =
            (1ULL << type_info.get_bit_width()) - 1ULL;
        return static_cast<unsigned long long>(value) <= max_value;
    }

    if (type_info.get_bit_width() >= 63) {
        return true;
    }
    const long long min_value = -(1LL << (type_info.get_bit_width() - 1));
    const long long max_value = (1LL << (type_info.get_bit_width() - 1)) - 1LL;
    return value >= min_value && value <= max_value;
}

} // namespace

bool ConversionChecker::has_semantic_feature(SemanticFeature feature) const
    noexcept {
    return semantic_feature_registry_ == nullptr ||
           semantic_feature_registry_->has_feature(feature);
}

bool ConversionChecker::has_extended_builtin_scalar_handler() const noexcept {
    return has_semantic_feature(SemanticFeature::ExtendedBuiltinTypes) &&
           builtin_type_semantic_handler_registry_ != nullptr &&
           builtin_type_semantic_handler_registry_->has_handler(
               BuiltinTypeSemanticHandlerKind::ExtendedBuiltinScalarTypes);
}

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
    case SemanticTypeKind::Qualified: {
        const auto *lhs_qualified =
            static_cast<const QualifiedSemanticType *>(lhs);
        const auto *rhs_qualified =
            static_cast<const QualifiedSemanticType *>(rhs);
        return lhs_qualified->get_is_const() == rhs_qualified->get_is_const() &&
               lhs_qualified->get_is_volatile() ==
                   rhs_qualified->get_is_volatile() &&
               lhs_qualified->get_is_restrict() ==
                   rhs_qualified->get_is_restrict() &&
               is_same_type(lhs_qualified->get_base_type(),
                            rhs_qualified->get_base_type());
    }
    case SemanticTypeKind::Pointer:
        return is_same_type(
            static_cast<const PointerSemanticType *>(lhs)->get_pointee_type(),
            static_cast<const PointerSemanticType *>(rhs)->get_pointee_type());
    case SemanticTypeKind::Struct:
        return static_cast<const StructSemanticType *>(lhs)->get_name() ==
               static_cast<const StructSemanticType *>(rhs)->get_name();
    case SemanticTypeKind::Union:
        return static_cast<const UnionSemanticType *>(lhs)->get_name() ==
               static_cast<const UnionSemanticType *>(rhs)->get_name();
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
            lhs_function->get_is_variadic() != rhs_function->get_is_variadic() ||
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
    const SemanticType *unqualified_target = strip_qualifiers(target);
    const SemanticType *unqualified_value = strip_qualifiers(value);

    if (unqualified_target != nullptr && unqualified_value != nullptr &&
        unqualified_target->get_kind() == SemanticTypeKind::Pointer &&
        unqualified_value->get_kind() == SemanticTypeKind::Pointer) {
        const auto *target_pointer =
            static_cast<const PointerSemanticType *>(unqualified_target);
        const auto *value_pointer =
            static_cast<const PointerSemanticType *>(unqualified_value);
        const SemanticType *target_pointee = target_pointer->get_pointee_type();
        const SemanticType *value_pointee = value_pointer->get_pointee_type();
        if (is_same_type(target_pointee, value_pointee)) {
            return true;
        }
        if (has_semantic_feature(SemanticFeature::QualifiedPointerConversions)) {
            const TypeQualifiers target_qualifiers =
                split_top_level_qualifiers(target_pointee);
            const TypeQualifiers value_qualifiers =
                split_top_level_qualifiers(value_pointee);
            if ((target_qualifiers.is_const || target_qualifiers.is_volatile ||
                 target_qualifiers.is_restrict ||
                 value_qualifiers.is_const || value_qualifiers.is_volatile ||
                 value_qualifiers.is_restrict) &&
                qualifiers_are_superset(target_qualifiers, value_qualifiers) &&
                is_same_type(target_qualifiers.base_type,
                             value_qualifiers.base_type)) {
                return true;
            }
        }
        if (is_void_pointer_type(unqualified_target) ||
            is_void_pointer_type(unqualified_value)) {
            return true;
        }
        return false;
    }
    if (is_integer_like_type(unqualified_target) &&
        is_integer_like_type(unqualified_value)) {
        return true;
    }
    if (unqualified_target == nullptr || unqualified_value == nullptr ||
        unqualified_target->get_kind() != SemanticTypeKind::Builtin ||
        unqualified_value->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }

    const auto &target_name =
        static_cast<const BuiltinSemanticType *>(unqualified_target)->get_name();
    const auto &value_name =
        static_cast<const BuiltinSemanticType *>(unqualified_value)->get_name();
    bool target_supported = is_standard_numeric_builtin_name(target_name);
    bool value_supported = is_standard_numeric_builtin_name(value_name);
    if (has_extended_builtin_scalar_handler()) {
        ExtendedBuiltinTypeSemanticHandler handler;
        target_supported = target_supported ||
                           handler.is_extended_scalar_builtin_name(target_name);
        value_supported = value_supported ||
                          handler.is_extended_scalar_builtin_name(value_name);
    }
    if (target_supported && value_supported) {
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

bool ConversionChecker::is_incompatible_pointer_assignment(
    const SemanticType *target, const SemanticType *value,
    SemanticModel &semantic_model) const {
    if (target == nullptr || value == nullptr) {
        return false;
    }

    const SemanticType *decayed_target = get_decayed_type(target, semantic_model);
    const SemanticType *decayed_value = get_decayed_type(value, semantic_model);
    if (!is_pointer_type(decayed_target) || !is_pointer_type(decayed_value)) {
        return false;
    }

    return !is_assignable_type(decayed_target, decayed_value) &&
           !drops_qualifiers_in_pointer_conversion(decayed_target, decayed_value);
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
    const SemanticType *unqualified_lhs = strip_qualifiers(lhs);
    const SemanticType *unqualified_rhs = strip_qualifiers(rhs);
    if (unqualified_lhs != nullptr && unqualified_rhs != nullptr &&
        unqualified_lhs->get_kind() == SemanticTypeKind::Pointer &&
        unqualified_rhs->get_kind() == SemanticTypeKind::Pointer) {
        return true;
    }

    const auto lhs_constant =
        constant_evaluator.get_integer_constant_value(lhs_expr, semantic_context);
    const auto rhs_constant =
        constant_evaluator.get_integer_constant_value(rhs_expr, semantic_context);
    if (unqualified_lhs != nullptr &&
        unqualified_lhs->get_kind() == SemanticTypeKind::Pointer &&
        rhs_constant.has_value() && *rhs_constant == 0) {
        return true;
    }
    if (unqualified_rhs != nullptr &&
        unqualified_rhs->get_kind() == SemanticTypeKind::Pointer &&
        lhs_constant.has_value() && *lhs_constant == 0) {
        return true;
    }
    return false;
}

bool ConversionChecker::should_warn_sign_compare(
    const SemanticType *lhs, const SemanticType *rhs,
    SemanticModel &semantic_model) const {
    if (!is_integer_like_type(lhs) || !is_integer_like_type(rhs)) {
        return false;
    }

    IntegerConversionService service;
    const SemanticType *lhs_promoted =
        service.get_integer_promotion_type(lhs, semantic_model);
    const SemanticType *rhs_promoted =
        service.get_integer_promotion_type(rhs, semantic_model);
    const auto lhs_info = service.get_integer_type_info(lhs_promoted);
    const auto rhs_info = service.get_integer_type_info(rhs_promoted);
    if (!lhs_info.has_value() || !rhs_info.has_value()) {
        return false;
    }

    return lhs_info->get_is_signed() != rhs_info->get_is_signed();
}

bool ConversionChecker::should_warn_implicit_integer_narrowing(
    const SemanticType *target, const SemanticType *value,
    std::optional<long long> constant_value) const {
    if (!is_integer_like_type(target) || !is_integer_like_type(value)) {
        return false;
    }

    const SemanticType *unqualified_target = strip_qualifiers(target);
    const SemanticType *unqualified_value = strip_qualifiers(value);
    if (unqualified_target == nullptr || unqualified_value == nullptr) {
        return false;
    }

    IntegerConversionService service;
    const auto source_info = service.get_integer_type_info(unqualified_value);
    const auto target_info = service.get_integer_type_info(unqualified_target);
    if (!source_info.has_value() || !target_info.has_value()) {
        return false;
    }

    if (constant_value.has_value()) {
        return !is_value_representable_in_integer_type(*constant_value,
                                                       *target_info);
    }

    if (service.get_integer_conversion_plan(unqualified_value, unqualified_target)
            .get_kind() == IntegerConversionKind::Truncate) {
        return true;
    }

    if (source_info->get_is_signed() != target_info->get_is_signed() &&
        source_info->get_bit_width() >= target_info->get_bit_width()) {
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
    IntegerConversionService service;
    if (const SemanticType *converted =
            service.get_usual_arithmetic_conversion_type(lhs, rhs,
                                                        semantic_model);
        converted != nullptr) {
        return converted;
    }
    if (has_extended_builtin_scalar_handler()) {
        ExtendedBuiltinTypeSemanticHandler handler;
        if (const SemanticType *extended_type =
                handler.get_usual_arithmetic_conversion_type(lhs, rhs,
                                                             semantic_model);
            extended_type != nullptr) {
            return extended_type;
        }
    }
    return semantic_model.own_type(std::make_unique<BuiltinSemanticType>("int"));
}

bool ConversionChecker::is_castable_type(const SemanticType *target,
                                         const SemanticType *value) const {
    if (target == nullptr || value == nullptr) {
        return false;
    }
    if (is_same_type(target, value)) {
        return true;
    }
    if (is_arithmetic_type(target) && is_arithmetic_type(value)) {
        return true;
    }
    if (is_pointer_type(target) && is_pointer_type(value)) {
        return true;
    }
    if ((is_pointer_type(target) && is_integer_like_type(value)) ||
        (is_integer_like_type(target) && is_pointer_type(value))) {
        return true;
    }
    return false;
}

bool ConversionChecker::is_arithmetic_type(const SemanticType *type) const {
    if (type == nullptr) {
        return false;
    }
    type = strip_qualifiers(type);
    if (type->get_kind() == SemanticTypeKind::Enum) {
        return true;
    }
    if (type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
    if (name == "int" || name == "ptrdiff_t" || name == "size_t" ||
        name == "short" ||
        name == "signed char" ||
        name == "long int" || name == "long long int" ||
        name == "unsigned int" || name == "unsigned short" ||
        name == "unsigned long" ||
        name == "unsigned long long" || name == "unsigned char" ||
        name == "char" || name == "float" || name == "double" ||
        name == "long double") {
        return true;
    }
    if (has_extended_builtin_scalar_handler()) {
        ExtendedBuiltinTypeSemanticHandler handler;
        return handler.is_extended_scalar_builtin_name(name);
    }
    return false;
}

bool ConversionChecker::is_scalar_type(const SemanticType *type) const {
    if (type == nullptr) {
        return false;
    }
    type = strip_qualifiers(type);
    switch (type->get_kind()) {
    case SemanticTypeKind::Builtin:
    case SemanticTypeKind::Pointer:
    case SemanticTypeKind::Enum:
        return true;
    case SemanticTypeKind::Qualified:
    case SemanticTypeKind::Array:
    case SemanticTypeKind::Function:
    case SemanticTypeKind::Struct:
    case SemanticTypeKind::Union:
        return false;
    }
    return false;
}

bool ConversionChecker::is_integer_like_type(const SemanticType *type) const {
    if (type == nullptr) {
        return false;
    }
    type = strip_qualifiers(type);
    if (type->get_kind() == SemanticTypeKind::Enum) {
        return true;
    }
    if (type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
    return name == "int" || name == "ptrdiff_t" || name == "size_t" ||
           name == "short" ||
           name == "signed char" ||
           name == "long int" || name == "long long int" ||
           name == "unsigned int" || name == "unsigned short" ||
           name == "unsigned long" ||
           name == "unsigned long long" || name == "unsigned char" ||
           name == "char";
}

bool ConversionChecker::is_incrementable_type(const SemanticType *type) const {
    if (type == nullptr) {
        return false;
    }
    type = strip_qualifiers(type);
    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return true;
    }
    if (type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
    if (name == "int" || name == "ptrdiff_t" || name == "size_t" ||
        name == "short" ||
        name == "signed char" ||
        name == "long int" || name == "long long int" ||
        name == "unsigned int" || name == "unsigned short" ||
        name == "unsigned long" ||
        name == "unsigned long long" || name == "unsigned char" ||
        name == "float" || name == "double" || name == "long double" ||
        name == "char") {
        return true;
    }
    if (has_extended_builtin_scalar_handler()) {
        ExtendedBuiltinTypeSemanticHandler handler;
        return handler.is_extended_scalar_builtin_name(name);
    }
    return false;
}

bool ConversionChecker::is_void_type(const SemanticType *type) const {
    type = strip_qualifiers(type);
    return type != nullptr && type->get_kind() == SemanticTypeKind::Builtin &&
           static_cast<const BuiltinSemanticType *>(type)->get_name() == "void";
}

bool ConversionChecker::is_pointer_type(const SemanticType *type) const {
    type = strip_qualifiers(type);
    return type != nullptr && type->get_kind() == SemanticTypeKind::Pointer;
}

bool ConversionChecker::is_struct_type(const SemanticType *type) const {
    type = strip_qualifiers(type);
    return type != nullptr && type->get_kind() == SemanticTypeKind::Struct;
}

bool ConversionChecker::is_union_type(const SemanticType *type) const {
    type = strip_qualifiers(type);
    return type != nullptr && type->get_kind() == SemanticTypeKind::Union;
}

bool ConversionChecker::is_pointer_to_struct_type(const SemanticType *type) const {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Pointer) {
        return false;
    }
    const auto *pointer_type = static_cast<const PointerSemanticType *>(type);
    return pointer_type->get_pointee_type() != nullptr &&
           is_struct_type(pointer_type->get_pointee_type());
}

bool ConversionChecker::is_pointer_to_union_type(const SemanticType *type) const {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Pointer) {
        return false;
    }
    const auto *pointer_type = static_cast<const PointerSemanticType *>(type);
    return pointer_type->get_pointee_type() != nullptr &&
           is_union_type(pointer_type->get_pointee_type());
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
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return type;
    }
    if (type->get_kind() == SemanticTypeKind::Array) {
        const auto *array_type = static_cast<const ArraySemanticType *>(type);
        return semantic_model.own_type(
            std::make_unique<PointerSemanticType>(array_type->get_element_type()));
    }
    if (type->get_kind() == SemanticTypeKind::Function) {
        return semantic_model.own_type(std::make_unique<PointerSemanticType>(type));
    }
    return type;
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
