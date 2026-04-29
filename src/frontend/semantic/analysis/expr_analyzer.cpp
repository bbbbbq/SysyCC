#include "frontend/semantic/analysis/expr_analyzer.hpp"

#include <memory>
#include <string>
#include <utility>

#include "common/integer_literal.hpp"
#include "common/string_literal.hpp"
#include "common/diagnostic/warning_options.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"
#include "frontend/semantic/type_system/constant_evaluator.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/type_system/type_layout.hpp"
#include "frontend/semantic/type_system/type_resolver.hpp"
#include "frontend/semantic/model/semantic_diagnostic.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

namespace {

const SemanticType *get_float_literal_semantic_type(
    const FloatLiteralExpr *expr, SemanticModel &semantic_model) {
    if (expr == nullptr) {
        return nullptr;
    }

    const std::string &value_text = expr->get_value_text();
    if (!value_text.empty()) {
        const char suffix = value_text.back();
        if (suffix == 'f' || suffix == 'F') {
            return semantic_model.own_type(
                std::make_unique<BuiltinSemanticType>("float"));
        }
        if (suffix == 'l' || suffix == 'L') {
            return semantic_model.own_type(
                std::make_unique<BuiltinSemanticType>("long double"));
        }
    }

    return semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("double"));
}

const SemanticType *strip_qualifiers(const SemanticType *type) {
    const SemanticType *current = type;
    while (current != nullptr &&
           current->get_kind() == SemanticTypeKind::Qualified) {
        current =
            static_cast<const QualifiedSemanticType *>(current)->get_base_type();
    }
    return current;
}

struct TopLevelQualifiers {
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;
    const SemanticType *base_type = nullptr;
};

TopLevelQualifiers split_top_level_qualifiers(const SemanticType *type) {
    TopLevelQualifiers qualifiers;
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

const SemanticType *apply_top_level_qualifiers(const SemanticType *type,
                                               bool is_const,
                                               bool is_volatile,
                                               bool is_restrict,
                                               SemanticModel &semantic_model) {
    if (type == nullptr || (!is_const && !is_volatile && !is_restrict)) {
        return type;
    }
    return semantic_model.own_type(std::make_unique<QualifiedSemanticType>(
        is_const, is_volatile, is_restrict, type));
}

const SemanticType *get_builtin_semantic_type(SemanticModel &semantic_model,
                                              const std::string &name) {
    return semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>(name));
}

const SemanticType *get_int_semantic_type(SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "int");
}

const SemanticType *get_void_semantic_type(SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "void");
}

const SemanticType *get_ptrdiff_semantic_type(SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "ptrdiff_t");
}

const SemanticType *get_float_semantic_type(SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "float");
}

const SemanticType *get_unsigned_int_semantic_type(
    SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "unsigned int");
}

const SemanticType *get_unsigned_short_semantic_type(
    SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "unsigned short");
}

const SemanticType *get_unsigned_char_semantic_type(
    SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "unsigned char");
}

const SemanticType *get_unsigned_long_long_semantic_type(
    SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "unsigned long long");
}

const SemanticType *get_long_semantic_type(SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "long int");
}

const SemanticType *get_long_long_semantic_type(SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "long long int");
}

const SemanticType *get_unsigned_long_semantic_type(
    SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "unsigned long");
}

const SemanticType *get_size_t_semantic_type(SemanticModel &semantic_model) {
    return get_builtin_semantic_type(semantic_model, "size_t");
}

bool fits_signed_32(unsigned long long magnitude) {
    return magnitude <= 2147483647ULL;
}

bool fits_unsigned_32(unsigned long long magnitude) { return magnitude <= 0xFFFFFFFFULL; }

bool fits_signed_64(unsigned long long magnitude) {
    return magnitude <= 9223372036854775807ULL;
}

const SemanticType *get_integer_literal_semantic_type(
    const IntegerLiteralExpr *expr, SemanticModel &semantic_model) {
    if (expr == nullptr) {
        return nullptr;
    }

    const auto parsed_literal =
        parse_integer_literal_info(expr->get_value_text());
    if (!parsed_literal.has_value()) {
        return get_int_semantic_type(semantic_model);
    }

    const unsigned long long magnitude = parsed_literal->magnitude;
    const bool is_decimal = parsed_literal->base == 10;
    const bool is_unsigned = parsed_literal->is_unsigned_suffix;
    const int long_count = parsed_literal->long_count;

    if (is_unsigned) {
        if (long_count >= 2) {
            return get_unsigned_long_long_semantic_type(semantic_model);
        }
        if (long_count == 1) {
            return get_unsigned_long_semantic_type(semantic_model);
        }
        if (fits_unsigned_32(magnitude)) {
            return get_unsigned_int_semantic_type(semantic_model);
        }
        return get_unsigned_long_semantic_type(semantic_model);
    }

    if (long_count >= 2) {
        if (fits_signed_64(magnitude)) {
            return get_long_long_semantic_type(semantic_model);
        }
        return get_unsigned_long_long_semantic_type(semantic_model);
    }

    if (long_count == 1) {
        if (fits_signed_64(magnitude)) {
            return get_long_semantic_type(semantic_model);
        }
        return is_decimal ? get_long_long_semantic_type(semantic_model)
                          : get_unsigned_long_semantic_type(semantic_model);
    }

    if (is_decimal) {
        if (fits_signed_32(magnitude)) {
            return get_int_semantic_type(semantic_model);
        }
        if (fits_signed_64(magnitude)) {
            return get_long_semantic_type(semantic_model);
        }
        return get_long_long_semantic_type(semantic_model);
    }

    if (fits_signed_32(magnitude)) {
        return get_int_semantic_type(semantic_model);
    }
    if (fits_unsigned_32(magnitude)) {
        return get_unsigned_int_semantic_type(semantic_model);
    }
    if (fits_signed_64(magnitude)) {
        return get_long_semantic_type(semantic_model);
    }
    return get_unsigned_long_semantic_type(semantic_model);
}

bool is_compound_assignment_operator(const std::string &operator_text) {
    return operator_text == "+=" || operator_text == "-=" ||
           operator_text == "*=" || operator_text == "/=" ||
           operator_text == "%=" || operator_text == "<<=" ||
           operator_text == ">>=" || operator_text == "&=" ||
           operator_text == "^=" || operator_text == "|=";
}

std::string get_compound_assignment_binary_operator(
    const std::string &operator_text) {
    if (operator_text == "+=") {
        return "+";
    }
    if (operator_text == "-=") {
        return "-";
    }
    if (operator_text == "*=") {
        return "*";
    }
    if (operator_text == "/=") {
        return "/";
    }
    if (operator_text == "%=") {
        return "%";
    }
    if (operator_text == "<<=") {
        return "<<";
    }
    if (operator_text == ">>=") {
        return ">>";
    }
    if (operator_text == "&=") {
        return "&";
    }
    if (operator_text == "^=") {
        return "^";
    }
    if (operator_text == "|=") {
        return "|";
    }
    return {};
}

const SemanticType *get_integer_promotion_type(const SemanticType *type,
                                               SemanticModel &semantic_model) {
    const SemanticType *unqualified_type = strip_qualifiers(type);
    if (unqualified_type == nullptr) {
        return nullptr;
    }
    if (unqualified_type->get_kind() == SemanticTypeKind::Enum) {
        return get_int_semantic_type(semantic_model);
    }
    if (unqualified_type->get_kind() != SemanticTypeKind::Builtin) {
        return nullptr;
    }

    IntegerConversionService integer_conversion_service;
    const auto type_info =
        integer_conversion_service.get_integer_type_info(unqualified_type);
    if (!type_info.has_value()) {
        return nullptr;
    }
    if (type_info->get_rank() < 3) {
        return get_int_semantic_type(semantic_model);
    }
    return unqualified_type;
}

const SemanticType *get_unsigned_equivalent_integer_type(
    const SemanticType *type, SemanticModel &semantic_model) {
    const SemanticType *unqualified_type = strip_qualifiers(type);
    if (unqualified_type == nullptr ||
        unqualified_type->get_kind() != SemanticTypeKind::Builtin) {
        return nullptr;
    }

    IntegerConversionService integer_conversion_service;
    const auto type_info =
        integer_conversion_service.get_integer_type_info(unqualified_type);
    if (!type_info.has_value()) {
        return nullptr;
    }

    if (type_info->get_bit_width() <= 8) {
        return get_unsigned_char_semantic_type(semantic_model);
    }
    if (type_info->get_bit_width() <= 16) {
        return get_unsigned_short_semantic_type(semantic_model);
    }
    if (type_info->get_bit_width() <= 32) {
        return get_unsigned_int_semantic_type(semantic_model);
    }
    return get_unsigned_long_long_semantic_type(semantic_model);
}

const SemanticType *get_common_integer_type(const SemanticType *lhs,
                                            const SemanticType *rhs,
                                            SemanticModel &semantic_model,
                                            const ConversionChecker
                                                &conversion_checker) {
    const SemanticType *unqualified_lhs = strip_qualifiers(lhs);
    const SemanticType *unqualified_rhs = strip_qualifiers(rhs);
    if (unqualified_lhs == nullptr || unqualified_rhs == nullptr ||
        unqualified_lhs->get_kind() != SemanticTypeKind::Builtin ||
        unqualified_rhs->get_kind() != SemanticTypeKind::Builtin) {
        return nullptr;
    }

    IntegerConversionService integer_conversion_service;
    const auto lhs_info =
        integer_conversion_service.get_integer_type_info(unqualified_lhs);
    const auto rhs_info =
        integer_conversion_service.get_integer_type_info(unqualified_rhs);
    if (!lhs_info.has_value() || !rhs_info.has_value()) {
        return nullptr;
    }

    const SemanticType *lhs_promoted =
        get_integer_promotion_type(unqualified_lhs, semantic_model);
    const SemanticType *rhs_promoted =
        get_integer_promotion_type(unqualified_rhs, semantic_model);
    if (lhs_promoted == nullptr || rhs_promoted == nullptr) {
        return nullptr;
    }

    if (conversion_checker.is_same_type(lhs_promoted, rhs_promoted)) {
        return lhs_promoted;
    }

    const auto lhs_promoted_info =
        integer_conversion_service.get_integer_type_info(lhs_promoted);
    const auto rhs_promoted_info =
        integer_conversion_service.get_integer_type_info(rhs_promoted);
    if (!lhs_promoted_info.has_value() || !rhs_promoted_info.has_value()) {
        return nullptr;
    }

    if (lhs_promoted_info->get_is_signed() ==
        rhs_promoted_info->get_is_signed()) {
        if (lhs_promoted_info->get_rank() > rhs_promoted_info->get_rank()) {
            return lhs_promoted;
        }
        if (rhs_promoted_info->get_rank() > lhs_promoted_info->get_rank()) {
            return rhs_promoted;
        }
        if (strip_qualifiers(lhs_promoted) != nullptr &&
            strip_qualifiers(lhs_promoted)->get_kind() ==
                SemanticTypeKind::Builtin &&
            static_cast<const BuiltinSemanticType *>(strip_qualifiers(lhs_promoted))
                    ->get_name() == "ptrdiff_t") {
            return lhs_promoted;
        }
        if (strip_qualifiers(rhs_promoted) != nullptr &&
            strip_qualifiers(rhs_promoted)->get_kind() ==
                SemanticTypeKind::Builtin &&
            static_cast<const BuiltinSemanticType *>(strip_qualifiers(rhs_promoted))
                    ->get_name() == "ptrdiff_t") {
            return rhs_promoted;
        }
        return lhs_promoted;
    }

    const bool lhs_is_signed = lhs_promoted_info->get_is_signed();
    const IntegerTypeInfo &signed_info =
        lhs_is_signed ? *lhs_promoted_info : *rhs_promoted_info;
    const IntegerTypeInfo &unsigned_info =
        lhs_is_signed ? *rhs_promoted_info : *lhs_promoted_info;
    const SemanticType *signed_type = lhs_is_signed ? lhs_promoted : rhs_promoted;
    const SemanticType *unsigned_type =
        lhs_is_signed ? rhs_promoted : lhs_promoted;

    if (signed_info.get_rank() > unsigned_info.get_rank()) {
        if (signed_info.get_bit_width() > unsigned_info.get_bit_width()) {
            return signed_type;
        }
        return get_unsigned_equivalent_integer_type(signed_type,
                                                    semantic_model);
    }

    if (unsigned_info.get_rank() > signed_info.get_rank()) {
        return unsigned_type;
    }

    return get_unsigned_equivalent_integer_type(signed_type, semantic_model);
}

const SemanticType *get_common_arithmetic_type(
    const SemanticType *lhs, const SemanticType *rhs,
    SemanticModel &semantic_model, const ConversionChecker &conversion_checker) {
    if (!conversion_checker.is_arithmetic_type(lhs) ||
        !conversion_checker.is_arithmetic_type(rhs)) {
        return nullptr;
    }

    const SemanticType *unqualified_lhs = strip_qualifiers(lhs);
    const SemanticType *unqualified_rhs = strip_qualifiers(rhs);
    if (unqualified_lhs != nullptr &&
        unqualified_lhs->get_kind() == SemanticTypeKind::Builtin &&
        unqualified_rhs != nullptr &&
        unqualified_rhs->get_kind() == SemanticTypeKind::Builtin &&
        conversion_checker.is_integer_like_type(unqualified_lhs) &&
        conversion_checker.is_integer_like_type(unqualified_rhs)) {
        return get_common_integer_type(unqualified_lhs, unqualified_rhs,
                                       semantic_model, conversion_checker);
    }

    return conversion_checker.get_usual_arithmetic_conversion_type(
        lhs, rhs, semantic_model);
}

const SemanticType *get_pointer_decay_or_self(const SemanticType *type,
                                              SemanticModel &semantic_model) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return nullptr;
    }
    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return type;
    }
    if (type->get_kind() != SemanticTypeKind::Array) {
        return nullptr;
    }
    const auto *array_type = static_cast<const ArraySemanticType *>(type);
    return semantic_model.own_type(
        std::make_unique<PointerSemanticType>(array_type->get_element_type()));
}

bool are_compatible_pointer_relational_operands(
    const SemanticType *lhs, const SemanticType *rhs,
    const ConversionChecker &conversion_checker) {
    lhs = strip_qualifiers(lhs);
    rhs = strip_qualifiers(rhs);
    if (lhs == nullptr || rhs == nullptr ||
        lhs->get_kind() != SemanticTypeKind::Pointer ||
        rhs->get_kind() != SemanticTypeKind::Pointer) {
        return false;
    }
    if (conversion_checker.is_void_pointer_type(lhs) ||
        conversion_checker.is_void_pointer_type(rhs)) {
        return true;
    }

    const auto *lhs_pointer = static_cast<const PointerSemanticType *>(lhs);
    const auto *rhs_pointer = static_cast<const PointerSemanticType *>(rhs);
    return conversion_checker.is_same_type(
        strip_qualifiers(lhs_pointer->get_pointee_type()),
        strip_qualifiers(rhs_pointer->get_pointee_type()));
}

const FunctionSemanticType *get_callable_function_type(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return nullptr;
    }
    if (type->get_kind() == SemanticTypeKind::Function) {
        return static_cast<const FunctionSemanticType *>(type);
    }
    if (type->get_kind() != SemanticTypeKind::Pointer) {
        return nullptr;
    }
    const SemanticType *pointee_type =
        strip_qualifiers(static_cast<const PointerSemanticType *>(type)->get_pointee_type());
    if (pointee_type == nullptr ||
        pointee_type->get_kind() != SemanticTypeKind::Function) {
        return nullptr;
    }
    return static_cast<const FunctionSemanticType *>(pointee_type);
}

bool is_addressable_expr(const Expr *expr, const ConversionChecker &conversion_checker,
                         SemanticModel &semantic_model) {
    if (expr == nullptr) {
        return false;
    }
    if (conversion_checker.is_assignable_expr(expr) ||
        expr->get_kind() == AstKind::StringLiteralExpr) {
        return true;
    }

    const SemanticType *expr_type = semantic_model.get_node_type(expr);
    return expr_type != nullptr &&
           strip_qualifiers(expr_type) != nullptr &&
           strip_qualifiers(expr_type)->get_kind() == SemanticTypeKind::Function;
}

const SemanticType *get_member_owner_type(
    const SemanticType *base_type, const std::string &operator_text,
    const ConversionChecker &conversion_checker) {
    if (base_type == nullptr) {
        return nullptr;
    }
    const SemanticType *unqualified_base_type = strip_qualifiers(base_type);
    if (operator_text == "->") {
        if (conversion_checker.is_pointer_to_struct_type(base_type) ||
            conversion_checker.is_pointer_to_union_type(base_type)) {
            return strip_qualifiers(
                static_cast<const PointerSemanticType *>(unqualified_base_type)
                    ->get_pointee_type());
        }
        if (unqualified_base_type != nullptr &&
            unqualified_base_type->get_kind() == SemanticTypeKind::Array) {
            const SemanticType *element_type = strip_qualifiers(
                static_cast<const ArraySemanticType *>(unqualified_base_type)
                    ->get_element_type());
            if (element_type != nullptr &&
                (element_type->get_kind() == SemanticTypeKind::Struct ||
                 element_type->get_kind() == SemanticTypeKind::Union)) {
                return element_type;
            }
        }
        return nullptr;
    }
    if (operator_text == "." &&
        (conversion_checker.is_struct_type(base_type) ||
         conversion_checker.is_union_type(base_type))) {
        return unqualified_base_type;
    }
    return nullptr;
}

const SemanticType *get_member_object_type(
    const SemanticType *base_type, const std::string &operator_text,
    const ConversionChecker &conversion_checker) {
    if (base_type == nullptr) {
        return nullptr;
    }

    const SemanticType *unqualified_base_type = strip_qualifiers(base_type);
    if (operator_text == "->") {
        if (conversion_checker.is_pointer_to_struct_type(base_type) ||
            conversion_checker.is_pointer_to_union_type(base_type)) {
            return static_cast<const PointerSemanticType *>(unqualified_base_type)
                ->get_pointee_type();
        }
        if (unqualified_base_type != nullptr &&
            unqualified_base_type->get_kind() == SemanticTypeKind::Array) {
            const SemanticType *element_type =
                static_cast<const ArraySemanticType *>(unqualified_base_type)
                    ->get_element_type();
            const SemanticType *unqualified_element_type =
                strip_qualifiers(element_type);
            if (unqualified_element_type != nullptr &&
                (unqualified_element_type->get_kind() ==
                     SemanticTypeKind::Struct ||
                 unqualified_element_type->get_kind() ==
                     SemanticTypeKind::Union)) {
                return element_type;
            }
        }
        return nullptr;
    }
    if (operator_text == "." &&
        (conversion_checker.is_struct_type(base_type) ||
         conversion_checker.is_union_type(base_type))) {
        return base_type;
    }
    return nullptr;
}

const SemanticType *apply_member_object_qualifiers(
    const SemanticType *field_type, const SemanticType *owner_object_type,
    SemanticModel &semantic_model) {
    if (field_type == nullptr || owner_object_type == nullptr) {
        return field_type;
    }

    TopLevelQualifiers field_qualifiers = split_top_level_qualifiers(field_type);
    TopLevelQualifiers owner_qualifiers =
        split_top_level_qualifiers(owner_object_type);

    return apply_top_level_qualifiers(
        field_qualifiers.base_type,
        field_qualifiers.is_const || owner_qualifiers.is_const,
        field_qualifiers.is_volatile || owner_qualifiers.is_volatile,
        field_qualifiers.is_restrict || owner_qualifiers.is_restrict,
        semantic_model);
}

const SemanticType *
resolve_completed_named_aggregate_type(const SemanticType *type,
                                       const ScopeStack &scope_stack) {
    const SemanticType *unqualified_type = strip_qualifiers(type);
    if (unqualified_type == nullptr) {
        return type;
    }

    if (unqualified_type->get_kind() == SemanticTypeKind::Struct) {
        const auto *struct_type =
            static_cast<const StructSemanticType *>(unqualified_type);
        if (!struct_type->get_fields().empty()) {
            return type;
        }
        const SemanticSymbol *tag_symbol =
            scope_stack.lookup_tag(struct_type->get_name());
        if (tag_symbol != nullptr &&
            tag_symbol->get_kind() == SymbolKind::StructName &&
            tag_symbol->get_type() != nullptr) {
            const auto *completed_type =
                dynamic_cast<const StructSemanticType *>(strip_qualifiers(
                    tag_symbol->get_type()));
            if (completed_type != nullptr &&
                !completed_type->get_fields().empty()) {
                return tag_symbol->get_type();
            }
        }
    }

    if (unqualified_type->get_kind() == SemanticTypeKind::Union) {
        const auto *union_type =
            static_cast<const UnionSemanticType *>(unqualified_type);
        if (!union_type->get_fields().empty()) {
            return type;
        }
        const SemanticSymbol *tag_symbol =
            scope_stack.lookup_tag(union_type->get_name());
        if (tag_symbol != nullptr &&
            tag_symbol->get_kind() == SymbolKind::UnionName &&
            tag_symbol->get_type() != nullptr) {
            const auto *completed_type =
                dynamic_cast<const UnionSemanticType *>(strip_qualifiers(
                    tag_symbol->get_type()));
            if (completed_type != nullptr &&
                !completed_type->get_fields().empty()) {
                return tag_symbol->get_type();
            }
        }
    }

    return type;
}

const SemanticType *find_direct_union_field_type(const SemanticType *owner_type,
                                                 const std::string &field_name) {
    if (owner_type == nullptr || owner_type->get_kind() != SemanticTypeKind::Union) {
        return nullptr;
    }
    const auto *union_type = static_cast<const UnionSemanticType *>(owner_type);
    for (const auto &field : union_type->get_fields()) {
        if (field.get_name() == field_name) {
            return field.get_type();
        }
    }
    return nullptr;
}

const SemanticFieldInfo *find_direct_union_field_info(
    const SemanticType *owner_type, const std::string &field_name) {
    if (owner_type == nullptr || owner_type->get_kind() != SemanticTypeKind::Union) {
        return nullptr;
    }
    const auto *union_type = static_cast<const UnionSemanticType *>(owner_type);
    for (const auto &field : union_type->get_fields()) {
        if (field.get_name() == field_name) {
            return &field;
        }
    }
    return nullptr;
}

const SemanticType *find_direct_struct_field_type(const SemanticType *owner_type,
                                                  const std::string &field_name) {
    if (owner_type == nullptr ||
        owner_type->get_kind() != SemanticTypeKind::Struct) {
        return nullptr;
    }
    const auto *struct_type = static_cast<const StructSemanticType *>(owner_type);
    for (const auto &field : struct_type->get_fields()) {
        if (field.get_name() == field_name) {
            return field.get_type();
        }
    }
    return nullptr;
}

const SemanticFieldInfo *find_direct_struct_field_info(
    const SemanticType *owner_type, const std::string &field_name) {
    if (owner_type == nullptr ||
        owner_type->get_kind() != SemanticTypeKind::Struct) {
        return nullptr;
    }
    const auto *struct_type = static_cast<const StructSemanticType *>(owner_type);
    for (const auto &field : struct_type->get_fields()) {
        if (field.get_name() == field_name) {
            return &field;
        }
    }
    return nullptr;
}

std::optional<int> get_bit_field_width_for_expr(
    const Expr *expr, SemanticModel &semantic_model,
    const ConversionChecker &conversion_checker) {
    if (expr == nullptr) {
        return std::nullopt;
    }

    if (expr->get_kind() == AstKind::MemberExpr) {
        const auto *member_expr = static_cast<const MemberExpr *>(expr);
        const SemanticType *base_type =
            semantic_model.get_node_type(member_expr->get_base());
        const SemanticType *owner_type = get_member_owner_type(
            base_type, member_expr->get_operator_text(), conversion_checker);
        if (owner_type == nullptr) {
            return std::nullopt;
        }

        const SemanticFieldInfo *field_info = nullptr;
        if (owner_type->get_kind() == SemanticTypeKind::Struct) {
            field_info = find_direct_struct_field_info(owner_type,
                                                       member_expr->get_member_name());
        } else if (owner_type->get_kind() == SemanticTypeKind::Union) {
            field_info = find_direct_union_field_info(owner_type,
                                                      member_expr->get_member_name());
        }

        if (field_info == nullptr || !field_info->get_is_bit_field()) {
            return std::nullopt;
        }
        return field_info->get_bit_width();
    }

    if (expr->get_kind() == AstKind::BinaryExpr) {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        if (binary_expr->get_operator_text() == ",") {
            return get_bit_field_width_for_expr(binary_expr->get_rhs(),
                                                semantic_model,
                                                conversion_checker);
        }
        return std::nullopt;
    }

    if (expr->get_kind() == AstKind::AssignExpr) {
        const auto *assign_expr = static_cast<const AssignExpr *>(expr);
        return get_bit_field_width_for_expr(assign_expr->get_target(),
                                            semantic_model,
                                            conversion_checker);
    }

    if (expr->get_kind() == AstKind::PrefixExpr) {
        const auto *prefix_expr = static_cast<const PrefixExpr *>(expr);
        return get_bit_field_width_for_expr(prefix_expr->get_operand(),
                                            semantic_model,
                                            conversion_checker);
    }

    if (expr->get_kind() == AstKind::PostfixExpr) {
        const auto *postfix_expr = static_cast<const PostfixExpr *>(expr);
        return get_bit_field_width_for_expr(postfix_expr->get_operand(),
                                            semantic_model,
                                            conversion_checker);
    }

    if (expr->get_kind() == AstKind::ConditionalExpr) {
        const auto *conditional_expr = static_cast<const ConditionalExpr *>(expr);
        const auto true_width = get_bit_field_width_for_expr(
            conditional_expr->get_true_expr(), semantic_model, conversion_checker);
        const auto false_width = get_bit_field_width_for_expr(
            conditional_expr->get_false_expr(), semantic_model, conversion_checker);
        if (true_width.has_value() && false_width.has_value() &&
            *true_width == *false_width) {
            return true_width;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

const SemanticType *get_integer_promotion_type(
    const Expr *expr, SemanticModel &semantic_model,
    const ConversionChecker &conversion_checker) {
    if (expr == nullptr) {
        return nullptr;
    }

    detail::IntegerConversionService integer_conversion_service;
    return integer_conversion_service.get_integer_promotion_type(
        semantic_model.get_node_type(expr),
        get_bit_field_width_for_expr(expr, semantic_model, conversion_checker),
        semantic_model);
}

const SemanticType *get_common_integer_type(
    const Expr *lhs_expr, const SemanticType *lhs, const Expr *rhs_expr,
    const SemanticType *rhs, SemanticModel &semantic_model,
    const ConversionChecker &conversion_checker) {
    detail::IntegerConversionService integer_conversion_service;
    return integer_conversion_service.get_common_integer_type(
        lhs, get_bit_field_width_for_expr(lhs_expr, semantic_model, conversion_checker),
        rhs, get_bit_field_width_for_expr(rhs_expr, semantic_model, conversion_checker),
        semantic_model);
}

const SemanticType *get_common_arithmetic_type(
    const Expr *lhs_expr, const SemanticType *lhs, const Expr *rhs_expr,
    const SemanticType *rhs, SemanticModel &semantic_model,
    const ConversionChecker &conversion_checker) {
    detail::IntegerConversionService integer_conversion_service;
    if (const SemanticType *converted =
            integer_conversion_service.get_usual_arithmetic_conversion_type(
                lhs,
                get_bit_field_width_for_expr(lhs_expr, semantic_model,
                                             conversion_checker),
                rhs,
                get_bit_field_width_for_expr(rhs_expr, semantic_model,
                                             conversion_checker),
                semantic_model);
        converted != nullptr) {
        return converted;
    }
    return get_common_arithmetic_type(lhs, rhs, semantic_model, conversion_checker);
}

const Expr *get_statement_expr_result_expr(const Stmt *stmt) {
    if (stmt == nullptr) {
        return nullptr;
    }
    if (const auto *expr_stmt = dynamic_cast<const ExprStmt *>(stmt);
        expr_stmt != nullptr) {
        return expr_stmt->get_expression();
    }
    if (const auto *block_stmt = dynamic_cast<const BlockStmt *>(stmt);
        block_stmt != nullptr) {
        for (auto it = block_stmt->get_statements().rbegin();
             it != block_stmt->get_statements().rend(); ++it) {
            if (*it == nullptr) {
                continue;
            }
            return get_statement_expr_result_expr(it->get());
        }
    }
    return nullptr;
}

const IdentifierExpr *get_callee_identifier(const CallExpr *call_expr) {
    if (call_expr == nullptr) {
        return nullptr;
    }
    return dynamic_cast<const IdentifierExpr *>(call_expr->get_callee());
}

void analyze_type_constant_expressions(const TypeNode *type_node,
                                       const ExprAnalyzer &expr_analyzer,
                                       SemanticContext &semantic_context,
                                       ScopeStack &scope_stack) {
    if (type_node == nullptr) {
        return;
    }
    switch (type_node->get_kind()) {
    case AstKind::QualifiedType: {
        const auto *qualified_type =
            static_cast<const QualifiedTypeNode *>(type_node);
        analyze_type_constant_expressions(qualified_type->get_base_type(),
                                          expr_analyzer, semantic_context,
                                          scope_stack);
        return;
    }
    case AstKind::PointerType: {
        const auto *pointer_type =
            static_cast<const PointerTypeNode *>(type_node);
        analyze_type_constant_expressions(pointer_type->get_pointee_type(),
                                          expr_analyzer, semantic_context,
                                          scope_stack);
        return;
    }
    case AstKind::ArrayType: {
        const auto *array_type =
            static_cast<const ArrayTypeNode *>(type_node);
        analyze_type_constant_expressions(array_type->get_element_type(),
                                          expr_analyzer, semantic_context,
                                          scope_stack);
        for (const auto &dimension : array_type->get_dimensions()) {
            expr_analyzer.analyze_expr(dimension.get(), semantic_context,
                                       scope_stack);
        }
        return;
    }
    case AstKind::FunctionType: {
        const auto *function_type =
            static_cast<const FunctionTypeNode *>(type_node);
        analyze_type_constant_expressions(function_type->get_return_type(),
                                          expr_analyzer, semantic_context,
                                          scope_stack);
        for (const auto &parameter_type :
             function_type->get_parameter_types()) {
            analyze_type_constant_expressions(parameter_type.get(),
                                              expr_analyzer, semantic_context,
                                              scope_stack);
        }
        return;
    }
    default:
        return;
    }
}

void analyze_statement_expr_stmt(const Stmt *stmt,
                                 const ExprAnalyzer &expr_analyzer,
                                 SemanticContext &semantic_context,
                                 ScopeStack &scope_stack) {
    if (stmt == nullptr) {
        return;
    }
    switch (stmt->get_kind()) {
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        scope_stack.push_scope();
        for (const auto &statement : block_stmt->get_statements()) {
            analyze_statement_expr_stmt(statement.get(), expr_analyzer,
                                        semantic_context, scope_stack);
        }
        scope_stack.pop_scope();
        return;
    }
    case AstKind::ExprStmt:
        expr_analyzer.analyze_expr(
            static_cast<const ExprStmt *>(stmt)->get_expression(),
            semantic_context, scope_stack);
        return;
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(stmt);
        expr_analyzer.analyze_expr(if_stmt->get_condition(), semantic_context,
                                   scope_stack);
        analyze_statement_expr_stmt(if_stmt->get_then_branch(), expr_analyzer,
                                    semantic_context, scope_stack);
        analyze_statement_expr_stmt(if_stmt->get_else_branch(), expr_analyzer,
                                    semantic_context, scope_stack);
        return;
    }
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(stmt);
        expr_analyzer.analyze_expr(while_stmt->get_condition(), semantic_context,
                                   scope_stack);
        analyze_statement_expr_stmt(while_stmt->get_body(), expr_analyzer,
                                    semantic_context, scope_stack);
        return;
    }
    case AstKind::DoWhileStmt: {
        const auto *do_while_stmt = static_cast<const DoWhileStmt *>(stmt);
        analyze_statement_expr_stmt(do_while_stmt->get_body(), expr_analyzer,
                                    semantic_context, scope_stack);
        expr_analyzer.analyze_expr(do_while_stmt->get_condition(),
                                   semantic_context, scope_stack);
        return;
    }
    case AstKind::ForStmt: {
        const auto *for_stmt = static_cast<const ForStmt *>(stmt);
        scope_stack.push_scope();
        expr_analyzer.analyze_expr(for_stmt->get_init(), semantic_context,
                                   scope_stack);
        expr_analyzer.analyze_expr(for_stmt->get_condition(), semantic_context,
                                   scope_stack);
        expr_analyzer.analyze_expr(for_stmt->get_step(), semantic_context,
                                   scope_stack);
        analyze_statement_expr_stmt(for_stmt->get_body(), expr_analyzer,
                                    semantic_context, scope_stack);
        scope_stack.pop_scope();
        return;
    }
    case AstKind::SwitchStmt: {
        const auto *switch_stmt = static_cast<const SwitchStmt *>(stmt);
        expr_analyzer.analyze_expr(switch_stmt->get_condition(),
                                   semantic_context, scope_stack);
        analyze_statement_expr_stmt(switch_stmt->get_body(), expr_analyzer,
                                    semantic_context, scope_stack);
        return;
    }
    case AstKind::CaseStmt:
        expr_analyzer.analyze_expr(
            static_cast<const CaseStmt *>(stmt)->get_value(), semantic_context,
            scope_stack);
        analyze_statement_expr_stmt(
            static_cast<const CaseStmt *>(stmt)->get_body(), expr_analyzer,
            semantic_context, scope_stack);
        return;
    case AstKind::DefaultStmt:
        analyze_statement_expr_stmt(
            static_cast<const DefaultStmt *>(stmt)->get_body(), expr_analyzer,
            semantic_context, scope_stack);
        return;
    case AstKind::LabelStmt:
        analyze_statement_expr_stmt(
            static_cast<const LabelStmt *>(stmt)->get_body(), expr_analyzer,
            semantic_context, scope_stack);
        return;
    case AstKind::ReturnStmt:
        expr_analyzer.analyze_expr(
            static_cast<const ReturnStmt *>(stmt)->get_value(),
            semantic_context, scope_stack);
        return;
    case AstKind::GotoStmt: {
        const auto *goto_stmt = static_cast<const GotoStmt *>(stmt);
        if (goto_stmt->get_is_indirect()) {
            expr_analyzer.analyze_expr(goto_stmt->get_indirect_target(),
                                       semantic_context, scope_stack);
        }
        return;
    }
    default:
        return;
    }
}

} // namespace

ExprAnalyzer::ExprAnalyzer(const TypeResolver &type_resolver,
                           const ConversionChecker &conversion_checker,
                           const ConstantEvaluator &constant_evaluator)
    : type_resolver_(type_resolver),
      conversion_checker_(conversion_checker),
      constant_evaluator_(constant_evaluator) {}

void ExprAnalyzer::add_error(SemanticContext &semantic_context,
                             std::string message,
                             const SourceSpan &source_span) const {
    semantic_context.get_semantic_model().add_diagnostic(
        SemanticDiagnostic(DiagnosticSeverity::Error, std::move(message),
                           source_span));
}

void ExprAnalyzer::add_warning(SemanticContext &semantic_context,
                               std::string message,
                               const SourceSpan &source_span,
                               std::string warning_option) const {
    if (semantic_context.is_system_header_span(source_span)) {
        return;
    }
    semantic_context.get_semantic_model().add_diagnostic(
        SemanticDiagnostic(DiagnosticSeverity::Warning, std::move(message),
                           source_span, std::move(warning_option)));
}

void ExprAnalyzer::analyze_expr(const Expr *expr,
                                SemanticContext &semantic_context,
                                ScopeStack &scope_stack) const {
    if (expr == nullptr) {
        return;
    }

    SemanticModel &semantic_model = semantic_context.get_semantic_model();

    switch (expr->get_kind()) {
    case AstKind::IdentifierExpr: {
        const auto *identifier_expr = static_cast<const IdentifierExpr *>(expr);
        const SemanticSymbol *symbol = scope_stack.lookup(identifier_expr->get_name());
        if (symbol == nullptr) {
            add_error(semantic_context,
                      "undefined identifier: " + identifier_expr->get_name(),
                      identifier_expr->get_source_span());
            return;
        }
        semantic_model.bind_symbol(identifier_expr, symbol);
        semantic_model.mark_symbol_used(symbol);
        semantic_model.bind_node_type(identifier_expr, symbol->get_type());
        if ((symbol->get_kind() == SymbolKind::Constant ||
             symbol->get_kind() == SymbolKind::Enumerator) &&
            symbol->get_decl_node() != nullptr) {
            const auto integer_constant_value =
                constant_evaluator_.get_integer_constant_value(
                    symbol->get_decl_node(), semantic_context);
            if (integer_constant_value.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    identifier_expr, *integer_constant_value, semantic_context);
            }
        }
        return;
    }
    case AstKind::IntegerLiteralExpr:
        semantic_model.bind_node_type(
            expr, get_integer_literal_semantic_type(
                      static_cast<const IntegerLiteralExpr *>(expr),
                      semantic_model));
        if (const auto parsed_value = parse_integer_literal(
                static_cast<const IntegerLiteralExpr *>(expr)->get_value_text());
            parsed_value.has_value()) {
            constant_evaluator_.bind_integer_constant_value(
                expr, *parsed_value, semantic_context);
        }
        return;
    case AstKind::FloatLiteralExpr:
        semantic_model.bind_node_type(
            expr, get_float_literal_semantic_type(
                      static_cast<const FloatLiteralExpr *>(expr),
                      semantic_model));
        return;
    case AstKind::CharLiteralExpr:
        semantic_model.bind_node_type(
            expr,
            semantic_model.own_type(std::make_unique<BuiltinSemanticType>("int")));
        if (const auto char_constant =
                constant_evaluator_.get_integer_constant_value(expr,
                                                               semantic_context);
            char_constant.has_value()) {
            constant_evaluator_.bind_integer_constant_value(
                expr, *char_constant, semantic_context);
        }
        return;
    case AstKind::StringLiteralExpr: {
        const auto *string_literal = static_cast<const StringLiteralExpr *>(expr);
        const auto *char_type =
            semantic_model.own_type(std::make_unique<BuiltinSemanticType>("char"));
        const std::string decoded_text =
            decode_string_literal_token(string_literal->get_value_text());
        semantic_model.bind_node_type(
            expr, semantic_model.own_type(std::make_unique<ArraySemanticType>(
                      char_type,
                      std::vector<int>{static_cast<int>(decoded_text.size() + 1)})));
        return;
    }
    case AstKind::SizeofTypeExpr: {
        const auto *sizeof_type_expr = static_cast<const SizeofTypeExpr *>(expr);
        analyze_type_constant_expressions(sizeof_type_expr->get_target_type(),
                                          *this, semantic_context, scope_stack);
        const SemanticType *target_type = type_resolver_.resolve_type(
            sizeof_type_expr->get_target_type(), semantic_context, &scope_stack);
        semantic_model.bind_node_type(sizeof_type_expr,
                                      get_size_t_semantic_type(semantic_model));
        const SemanticType *layout_target_type =
            resolve_completed_named_aggregate_type(target_type, scope_stack);
        const auto type_size = get_semantic_type_size(layout_target_type);
        if (!type_size.has_value()) {
            add_error(semantic_context,
                      "operator 'sizeof' requires a complete object type",
                      sizeof_type_expr->get_source_span());
            return;
        }
        constant_evaluator_.bind_integer_constant_value(
            sizeof_type_expr, static_cast<long long>(*type_size), semantic_context);
        return;
    }
    case AstKind::BuiltinVaArgExpr: {
        const auto *va_arg_expr = static_cast<const BuiltinVaArgExpr *>(expr);
        analyze_expr(va_arg_expr->get_va_list_expr(), semantic_context,
                     scope_stack);
        const SemanticType *target_type = type_resolver_.resolve_type(
            va_arg_expr->get_target_type(), semantic_context, &scope_stack);
        if (target_type == nullptr) {
            add_error(semantic_context,
                      "__builtin_va_arg requires a valid target type",
                      va_arg_expr->get_source_span());
            return;
        }
        semantic_model.bind_node_type(va_arg_expr, target_type);
        return;
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        if (unary_expr->get_operator_text() == "&&") {
            const auto *label_expr =
                dynamic_cast<const IdentifierExpr *>(unary_expr->get_operand());
            if (label_expr == nullptr) {
                add_error(semantic_context,
                          "GNU label address operator requires a label name",
                          unary_expr->get_source_span());
                return;
            }
            semantic_context.record_goto_reference(label_expr->get_name(),
                                                   unary_expr->get_source_span());
            const auto *void_type =
                semantic_model.own_type(std::make_unique<BuiltinSemanticType>("void"));
            semantic_model.bind_node_type(
                unary_expr,
                semantic_model.own_type(
                    std::make_unique<PointerSemanticType>(void_type)));
            return;
        }
        analyze_expr(unary_expr->get_operand(), semantic_context, scope_stack);
        const SemanticType *operand_type =
            semantic_model.get_node_type(unary_expr->get_operand());
        if (operand_type == nullptr) {
            return;
        }
        if (unary_expr->get_operator_text() == "sizeof") {
            semantic_model.bind_node_type(unary_expr,
                                          get_size_t_semantic_type(semantic_model));
            const SemanticType *layout_operand_type =
                resolve_completed_named_aggregate_type(operand_type, scope_stack);
            const auto operand_size = get_semantic_type_size(layout_operand_type);
            if (!operand_size.has_value()) {
                add_error(semantic_context,
                          "operator 'sizeof' requires a complete object type",
                          unary_expr->get_source_span());
                return;
            }
            constant_evaluator_.bind_integer_constant_value(
                unary_expr, static_cast<long long>(*operand_size),
                semantic_context);
            return;
        }
        if (unary_expr->get_operator_text() == "&") {
            if (!is_addressable_expr(unary_expr->get_operand(), conversion_checker_,
                                     semantic_model)) {
                add_error(semantic_context,
                          "operator '&' requires an assignable operand",
                          unary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(
                unary_expr, semantic_model.own_type(
                                std::make_unique<PointerSemanticType>(
                                    operand_type)));
            return;
        }
        if (unary_expr->get_operator_text() == "*") {
            const SemanticType *unqualified_operand_type =
                strip_qualifiers(conversion_checker_.get_decayed_type(
                    operand_type, semantic_model));
            if (unqualified_operand_type == nullptr ||
                unqualified_operand_type->get_kind() !=
                    SemanticTypeKind::Pointer) {
                add_error(semantic_context,
                          "operator '*' requires a pointer operand",
                          unary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(
                unary_expr,
                resolve_completed_named_aggregate_type(
                    static_cast<const PointerSemanticType *>(unqualified_operand_type)
                        ->get_pointee_type(),
                    scope_stack));
            return;
        }
        if (unary_expr->get_operator_text() == "!") {
            if (!conversion_checker_.is_scalar_type(operand_type)) {
                add_error(semantic_context,
                          "operator '!' requires a scalar operand",
                          unary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(unary_expr,
                                          get_int_semantic_type(semantic_model));
            const auto operand_constant =
                constant_evaluator_.get_integer_constant_value(
                    unary_expr->get_operand(), semantic_context);
            if (operand_constant.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    unary_expr, *operand_constant == 0 ? 1 : 0,
                    semantic_context);
            }
            return;
        }
        if (unary_expr->get_operator_text() == "~") {
            if (!conversion_checker_.is_integer_like_type(operand_type)) {
                add_error(semantic_context,
                          "operator '~' requires an integer operand",
                          unary_expr->get_source_span());
                return;
            }
            const SemanticType *result_type =
                get_integer_promotion_type(unary_expr->get_operand(),
                                           semantic_model, conversion_checker_);
            if (result_type == nullptr) {
                result_type = get_int_semantic_type(semantic_model);
            }
            semantic_model.bind_node_type(unary_expr, result_type);
            const auto operand_constant =
                constant_evaluator_.get_integer_constant_value(
                    unary_expr->get_operand(), semantic_context);
            if (operand_constant.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    unary_expr, ~(*operand_constant), semantic_context);
            }
            return;
        }
        if (unary_expr->get_operator_text() == "+" ||
            unary_expr->get_operator_text() == "-") {
            if (!conversion_checker_.is_arithmetic_type(operand_type)) {
                add_error(semantic_context,
                          "operator '" + unary_expr->get_operator_text() +
                              "' requires an arithmetic operand",
                          unary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(
                unary_expr,
                get_common_arithmetic_type(
                    unary_expr->get_operand(), operand_type,
                    unary_expr->get_operand(), operand_type, semantic_model,
                    conversion_checker_));
            const auto operand_constant =
                constant_evaluator_.get_integer_constant_value(
                    unary_expr->get_operand(), semantic_context);
            if (operand_constant.has_value() &&
                unary_expr->get_operator_text() == "+") {
                constant_evaluator_.bind_integer_constant_value(
                    unary_expr, *operand_constant, semantic_context);
            } else if (operand_constant.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    unary_expr, -(*operand_constant), semantic_context);
            }
            return;
        }
        semantic_model.bind_node_type(unary_expr, operand_type);
        return;
    }
    case AstKind::PrefixExpr: {
        const auto *prefix_expr = static_cast<const PrefixExpr *>(expr);
        analyze_expr(prefix_expr->get_operand(), semantic_context, scope_stack);
        if (prefix_expr->get_operand() != nullptr &&
            prefix_expr->get_operand()->get_kind() == AstKind::IdentifierExpr) {
            semantic_model.mark_symbol_written(
                semantic_model.get_symbol_binding(prefix_expr->get_operand()));
        }
        const SemanticType *operand_type =
            semantic_model.get_node_type(prefix_expr->get_operand());
        if (!conversion_checker_.is_assignable_expr(prefix_expr->get_operand())) {
            add_error(semantic_context,
                      "prefix operator '" + prefix_expr->get_operator_text() +
                          "' requires an assignable operand",
                      prefix_expr->get_source_span());
        }
        if (operand_type != nullptr &&
            !conversion_checker_.is_incrementable_type(operand_type)) {
            add_error(semantic_context,
                      "prefix operator '" + prefix_expr->get_operator_text() +
                          "' requires an incrementable operand",
                      prefix_expr->get_source_span());
        }
        semantic_model.bind_node_type(prefix_expr, operand_type);
        return;
    }
    case AstKind::PostfixExpr: {
        const auto *postfix_expr = static_cast<const PostfixExpr *>(expr);
        analyze_expr(postfix_expr->get_operand(), semantic_context, scope_stack);
        if (postfix_expr->get_operand() != nullptr &&
            postfix_expr->get_operand()->get_kind() == AstKind::IdentifierExpr) {
            semantic_model.mark_symbol_written(
                semantic_model.get_symbol_binding(postfix_expr->get_operand()));
        }
        const SemanticType *operand_type =
            semantic_model.get_node_type(postfix_expr->get_operand());
        if (!conversion_checker_.is_assignable_expr(postfix_expr->get_operand())) {
            add_error(semantic_context,
                      "postfix operator '" + postfix_expr->get_operator_text() +
                          "' requires an assignable operand",
                      postfix_expr->get_source_span());
        }
        if (operand_type != nullptr &&
            !conversion_checker_.is_incrementable_type(operand_type)) {
            add_error(semantic_context,
                      "postfix operator '" + postfix_expr->get_operator_text() +
                          "' requires an incrementable operand",
                      postfix_expr->get_source_span());
        }
        semantic_model.bind_node_type(postfix_expr, operand_type);
        return;
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        analyze_expr(binary_expr->get_lhs(), semantic_context, scope_stack);
        analyze_expr(binary_expr->get_rhs(), semantic_context, scope_stack);
        const SemanticType *lhs_type =
            semantic_model.get_node_type(binary_expr->get_lhs());
        const SemanticType *rhs_type =
            semantic_model.get_node_type(binary_expr->get_rhs());
        const auto lhs_constant = constant_evaluator_.get_integer_constant_value(
            binary_expr->get_lhs(), semantic_context);
        const auto rhs_constant = constant_evaluator_.get_integer_constant_value(
            binary_expr->get_rhs(), semantic_context);
        if (lhs_type == nullptr || rhs_type == nullptr) {
            return;
        }

        const std::string &operator_text = binary_expr->get_operator_text();
        const SemanticType *lhs_pointer_like =
            get_pointer_decay_or_self(lhs_type, semantic_model);
        const SemanticType *rhs_pointer_like =
            get_pointer_decay_or_self(rhs_type, semantic_model);

        if (operator_text == ",") {
            semantic_model.bind_node_type(binary_expr, rhs_type);
            const auto rhs_constant =
                constant_evaluator_.get_integer_constant_value(
                    binary_expr->get_rhs(), semantic_context);
            if (rhs_constant.has_value()) {
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, *rhs_constant, semantic_context);
            }
            return;
        }

        if (operator_text == "+" || operator_text == "-" ||
            operator_text == "*" || operator_text == "/") {
            if ((operator_text == "+" || operator_text == "-") &&
                lhs_pointer_like != nullptr &&
                conversion_checker_.is_integer_like_type(rhs_type)) {
                semantic_model.bind_node_type(binary_expr, lhs_pointer_like);
                return;
            }
            if (operator_text == "+" &&
                conversion_checker_.is_integer_like_type(lhs_type) &&
                rhs_pointer_like != nullptr) {
                semantic_model.bind_node_type(binary_expr, rhs_pointer_like);
                return;
            }
            if (operator_text == "-" && lhs_pointer_like != nullptr &&
                rhs_pointer_like != nullptr) {
                semantic_model.bind_node_type(
                    binary_expr, get_ptrdiff_semantic_type(semantic_model));
                return;
            }
            if (!conversion_checker_.is_arithmetic_type(lhs_type) ||
                !conversion_checker_.is_arithmetic_type(rhs_type)) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires arithmetic operands",
                          binary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(
                binary_expr, get_common_arithmetic_type(
                                 binary_expr->get_lhs(), lhs_type,
                                 binary_expr->get_rhs(), rhs_type,
                                 semantic_model, conversion_checker_));
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                long long result = 0;
                if (operator_text == "+") {
                    result = *lhs_constant + *rhs_constant;
                } else if (operator_text == "-") {
                    result = *lhs_constant - *rhs_constant;
                } else if (operator_text == "*") {
                    result = *lhs_constant * *rhs_constant;
                } else if (*rhs_constant != 0) {
                    result = *lhs_constant / *rhs_constant;
                } else {
                    return;
                }
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        if (operator_text == "%") {
            if (!conversion_checker_.is_integer_like_type(lhs_type) ||
                !conversion_checker_.is_integer_like_type(rhs_type)) {
                add_error(semantic_context,
                          "binary operator '%' requires integer operands",
                          binary_expr->get_source_span());
                return;
            }
            const SemanticType *result_type = get_common_integer_type(
                binary_expr->get_lhs(), lhs_type, binary_expr->get_rhs(),
                rhs_type, semantic_model, conversion_checker_);
            if (result_type == nullptr) {
                result_type = get_int_semantic_type(semantic_model);
            }
            semantic_model.bind_node_type(binary_expr, result_type);
            if (lhs_constant.has_value() && rhs_constant.has_value() &&
                *rhs_constant != 0) {
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, *lhs_constant % *rhs_constant, semantic_context);
            }
            return;
        }

        if (operator_text == "<<" || operator_text == ">>" ||
            operator_text == "&" || operator_text == "|" ||
            operator_text == "^") {
            if (!conversion_checker_.is_integer_like_type(lhs_type) ||
                !conversion_checker_.is_integer_like_type(rhs_type)) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires integer operands",
                          binary_expr->get_source_span());
                return;
            }
            const SemanticType *result_type = nullptr;
            if (operator_text == "<<" || operator_text == ">>") {
                result_type = get_integer_promotion_type(
                    binary_expr->get_lhs(), semantic_model,
                    conversion_checker_);
            } else {
                result_type = get_common_integer_type(
                    binary_expr->get_lhs(), lhs_type, binary_expr->get_rhs(),
                    rhs_type, semantic_model, conversion_checker_);
            }
            if (result_type == nullptr) {
                result_type = get_int_semantic_type(semantic_model);
            }
            semantic_model.bind_node_type(binary_expr, result_type);
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                long long result = 0;
                if (operator_text == "<<") {
                    result = *lhs_constant << *rhs_constant;
                } else if (operator_text == ">>") {
                    result = *lhs_constant >> *rhs_constant;
                } else if (operator_text == "&") {
                    result = *lhs_constant & *rhs_constant;
                } else if (operator_text == "|") {
                    result = *lhs_constant | *rhs_constant;
                } else {
                    result = *lhs_constant ^ *rhs_constant;
                }
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        if (operator_text == "&&" || operator_text == "||") {
            const bool lhs_is_logical_operand =
                conversion_checker_.is_scalar_type(lhs_type) ||
                lhs_pointer_like != nullptr;
            const bool rhs_is_logical_operand =
                conversion_checker_.is_scalar_type(rhs_type) ||
                rhs_pointer_like != nullptr;
            if (!lhs_is_logical_operand || !rhs_is_logical_operand) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires scalar operands",
                          binary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(binary_expr,
                                          get_int_semantic_type(semantic_model));
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                const long long result =
                    operator_text == "&&"
                        ? ((*lhs_constant != 0) && (*rhs_constant != 0))
                        : ((*lhs_constant != 0) || (*rhs_constant != 0));
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        if (operator_text == "<" || operator_text == "<=" ||
            operator_text == ">" || operator_text == ">=") {
            if (are_compatible_pointer_relational_operands(
                    lhs_pointer_like, rhs_pointer_like, conversion_checker_)) {
                semantic_model.bind_node_type(binary_expr,
                                              get_int_semantic_type(semantic_model));
                return;
            }
            if (!conversion_checker_.is_arithmetic_type(lhs_type) ||
                !conversion_checker_.is_arithmetic_type(rhs_type)) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires arithmetic operands",
                          binary_expr->get_source_span());
                return;
            }
            if (get_common_arithmetic_type(
                    binary_expr->get_lhs(), lhs_type, binary_expr->get_rhs(),
                    rhs_type, semantic_model, conversion_checker_) == nullptr) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires compatible arithmetic operands",
                          binary_expr->get_source_span());
                return;
            }
            if (conversion_checker_.is_integer_like_type(lhs_type) &&
                conversion_checker_.is_integer_like_type(rhs_type) &&
                conversion_checker_.should_warn_sign_compare(
                    lhs_type, rhs_type, semantic_model)) {
                add_warning(semantic_context,
                            "comparison of integers of different signs",
                            binary_expr->get_source_span(),
                            warning_options::kSignCompare);
            }
            semantic_model.bind_node_type(binary_expr,
                                          get_int_semantic_type(semantic_model));
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                long long result = 0;
                if (operator_text == "<") {
                    result = *lhs_constant < *rhs_constant;
                } else if (operator_text == "<=") {
                    result = *lhs_constant <= *rhs_constant;
                } else if (operator_text == ">") {
                    result = *lhs_constant > *rhs_constant;
                } else {
                    result = *lhs_constant >= *rhs_constant;
                }
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        if (operator_text == "==" || operator_text == "!=") {
            if (conversion_checker_.is_arithmetic_type(lhs_type) &&
                conversion_checker_.is_arithmetic_type(rhs_type)) {
                if (get_common_arithmetic_type(
                        binary_expr->get_lhs(), lhs_type,
                        binary_expr->get_rhs(), rhs_type, semantic_model,
                        conversion_checker_) == nullptr) {
                    add_error(semantic_context,
                              "binary operator '" + operator_text +
                                  "' requires compatible arithmetic operands",
                              binary_expr->get_source_span());
                    return;
                }
                if (conversion_checker_.is_integer_like_type(lhs_type) &&
                    conversion_checker_.is_integer_like_type(rhs_type) &&
                    conversion_checker_.should_warn_sign_compare(
                        lhs_type, rhs_type, semantic_model)) {
                    add_warning(semantic_context,
                                "comparison of integers of different signs",
                                binary_expr->get_source_span(),
                                warning_options::kSignCompare);
                }
            } else if (!conversion_checker_.is_compatible_equality_type(
                           lhs_type, rhs_type, binary_expr->get_lhs(),
                           binary_expr->get_rhs(), semantic_context,
                           constant_evaluator_)) {
                add_error(semantic_context,
                          "binary operator '" + operator_text +
                              "' requires compatible operands",
                          binary_expr->get_source_span());
                return;
            }
            semantic_model.bind_node_type(binary_expr,
                                          get_int_semantic_type(semantic_model));
            if (lhs_constant.has_value() && rhs_constant.has_value()) {
                const long long result =
                    operator_text == "==" ? (*lhs_constant == *rhs_constant)
                                          : (*lhs_constant != *rhs_constant);
                constant_evaluator_.bind_integer_constant_value(
                    binary_expr, result, semantic_context);
            }
            return;
        }

        semantic_model.bind_node_type(binary_expr, lhs_type);
        return;
    }
    case AstKind::CastExpr: {
        const auto *cast_expr = static_cast<const CastExpr *>(expr);
        analyze_expr(cast_expr->get_operand(), semantic_context, scope_stack);

        const SemanticType *operand_type =
            semantic_model.get_node_type(cast_expr->get_operand());
        const SemanticType *target_type =
            type_resolver_.resolve_type(cast_expr->get_target_type(),
                                        semantic_context, &scope_stack);
        if (operand_type == nullptr || target_type == nullptr) {
            return;
        }

        const SemanticType *cast_operand_type =
            conversion_checker_.get_decayed_type(operand_type, semantic_model);
        if (!conversion_checker_.is_castable_type(target_type,
                                                  cast_operand_type)) {
            add_error(semantic_context, "unsupported cast between operand types",
                      cast_expr->get_source_span());
            return;
        }

        semantic_model.bind_node_type(cast_expr, target_type);
        const auto cast_constant =
            constant_evaluator_.get_scalar_constant_value_as_integer(
                cast_expr->get_operand(), target_type, semantic_context);
        if (cast_constant.has_value() &&
            conversion_checker_.is_integer_like_type(target_type)) {
            constant_evaluator_.bind_integer_constant_value(
                cast_expr, *cast_constant, semantic_context);
        }
        return;
    }
    case AstKind::ConditionalExpr: {
        const auto *conditional_expr = static_cast<const ConditionalExpr *>(expr);
        analyze_expr(conditional_expr->get_condition(), semantic_context,
                     scope_stack);
        analyze_expr(conditional_expr->get_true_expr(), semantic_context,
                     scope_stack);
        analyze_expr(conditional_expr->get_false_expr(), semantic_context,
                     scope_stack);

        const SemanticType *condition_type =
            semantic_model.get_node_type(conditional_expr->get_condition());
        const SemanticType *true_type =
            semantic_model.get_node_type(conditional_expr->get_true_expr());
        const SemanticType *false_type =
            semantic_model.get_node_type(conditional_expr->get_false_expr());
        if (condition_type == nullptr || true_type == nullptr ||
            false_type == nullptr) {
            return;
        }

        if (!conversion_checker_.is_scalar_type(condition_type)) {
            add_error(semantic_context,
                      "conditional operator requires a scalar condition",
                      conditional_expr->get_source_span());
            return;
        }

        const SemanticType *result_type = nullptr;
        const SemanticType *true_value_type =
            conversion_checker_.get_decayed_type(true_type, semantic_model);
        const SemanticType *false_value_type =
            conversion_checker_.get_decayed_type(false_type, semantic_model);
        if (conversion_checker_.is_arithmetic_type(true_type) &&
            conversion_checker_.is_arithmetic_type(false_type)) {
            result_type = get_common_arithmetic_type(
                true_type, false_type, semantic_model, conversion_checker_);
        } else if (conversion_checker_.is_same_type(true_value_type,
                                                    false_value_type)) {
            result_type = true_value_type;
        } else if (conversion_checker_.is_pointer_type(true_value_type) &&
                   conversion_checker_.is_null_pointer_constant(
                       conditional_expr->get_false_expr(), semantic_context,
                       constant_evaluator_)) {
            result_type = true_value_type;
        } else if (conversion_checker_.is_pointer_type(false_value_type) &&
                   conversion_checker_.is_null_pointer_constant(
                       conditional_expr->get_true_expr(), semantic_context,
                       constant_evaluator_)) {
            result_type = false_value_type;
        } else if (conversion_checker_.is_pointer_type(true_value_type) &&
                   conversion_checker_.is_pointer_type(false_value_type)) {
            if (conversion_checker_.is_assignable_type(true_value_type,
                                                       false_value_type)) {
                result_type = true_value_type;
            } else if (conversion_checker_.is_assignable_type(false_value_type,
                                                              true_value_type)) {
                result_type = false_value_type;
            }
        }

        if (result_type == nullptr) {
            add_error(semantic_context,
                      "conditional operator requires compatible branch types",
                      conditional_expr->get_source_span());
            return;
        }

        semantic_model.bind_node_type(conditional_expr, result_type);
        const auto condition_constant =
            constant_evaluator_.get_integer_constant_value(
                conditional_expr->get_condition(), semantic_context);
        if (!condition_constant.has_value()) {
            return;
        }
        const Expr *selected_expr = *condition_constant != 0
                                        ? conditional_expr->get_true_expr()
                                        : conditional_expr->get_false_expr();
        const auto selected_constant =
            constant_evaluator_.get_integer_constant_value(selected_expr,
                                                           semantic_context);
        if (selected_constant.has_value()) {
            constant_evaluator_.bind_integer_constant_value(
                conditional_expr, *selected_constant, semantic_context);
        }
        return;
    }
    case AstKind::AssignExpr: {
        const auto *assign_expr = static_cast<const AssignExpr *>(expr);
        analyze_expr(assign_expr->get_target(), semantic_context, scope_stack);
        analyze_expr(assign_expr->get_value(), semantic_context, scope_stack);
        if (assign_expr->get_target() != nullptr &&
            assign_expr->get_target()->get_kind() == AstKind::IdentifierExpr) {
            semantic_model.mark_symbol_written(
                semantic_model.get_symbol_binding(assign_expr->get_target()));
        }
        const bool target_assignable =
            conversion_checker_.is_assignable_expr(assign_expr->get_target());
        if (!target_assignable) {
            add_error(semantic_context, "assignment target is not assignable",
                      assign_expr->get_target()->get_source_span());
        }
        const SemanticType *target_type =
            semantic_model.get_node_type(assign_expr->get_target());
        const SemanticType *value_type =
            semantic_model.get_node_type(assign_expr->get_value());
        if (target_type != nullptr && value_type != nullptr) {
            if (assign_expr->get_operator_text() == "=") {
                if (!conversion_checker_.is_assignable_value(
                        target_type, value_type, assign_expr->get_value(),
                        semantic_context, constant_evaluator_)) {
                    if (target_assignable &&
                        conversion_checker_.is_incompatible_pointer_assignment(
                            target_type, value_type, semantic_model)) {
                        add_warning(
                            semantic_context,
                            "assignment between incompatible pointer types",
                            assign_expr->get_source_span(),
                            warning_options::kIncompatiblePointerTypes);
                    } else {
                        add_error(
                            semantic_context,
                            "assignment value type does not match target type",
                            assign_expr->get_source_span());
                    }
                } else if (assign_expr->get_value()->get_kind() != AstKind::CastExpr &&
                           conversion_checker_.should_warn_implicit_integer_narrowing(
                               target_type, value_type,
                               constant_evaluator_.get_integer_constant_value(
                                   assign_expr->get_value(), semantic_context))) {
                    add_warning(semantic_context,
                                "implicit integer conversion may change value",
                                assign_expr->get_source_span(),
                                warning_options::kConversion);
                }
            } else if (is_compound_assignment_operator(
                           assign_expr->get_operator_text())) {
                const std::string binary_operator =
                    get_compound_assignment_binary_operator(
                        assign_expr->get_operator_text());
                const SemanticType *target_pointer_like =
                    get_pointer_decay_or_self(target_type, semantic_model);
                if ((binary_operator == "+" || binary_operator == "-") &&
                    target_pointer_like != nullptr &&
                    conversion_checker_.is_integer_like_type(value_type)) {
                    semantic_model.bind_node_type(assign_expr, target_type);
                    return;
                }
                if (binary_operator == "+" || binary_operator == "-" ||
                    binary_operator == "*" || binary_operator == "/") {
                    if (!conversion_checker_.is_arithmetic_type(target_type) ||
                        !conversion_checker_.is_arithmetic_type(value_type)) {
                        add_error(semantic_context,
                                  "compound assignment '" +
                                      assign_expr->get_operator_text() +
                                      "' requires arithmetic operands",
                                  assign_expr->get_source_span());
                    }
                } else if (binary_operator == "%" || binary_operator == "<<" ||
                           binary_operator == ">>" || binary_operator == "&" ||
                           binary_operator == "|" || binary_operator == "^") {
                    if (!conversion_checker_.is_integer_like_type(target_type) ||
                        !conversion_checker_.is_integer_like_type(value_type)) {
                        add_error(semantic_context,
                                  "compound assignment '" +
                                      assign_expr->get_operator_text() +
                                      "' requires integer operands",
                                  assign_expr->get_source_span());
                    }
                }
            }
        }
        semantic_model.bind_node_type(assign_expr, target_type);
        return;
    }
    case AstKind::CallExpr: {
        const auto *call_expr = static_cast<const CallExpr *>(expr);
        if (const IdentifierExpr *callee = get_callee_identifier(call_expr);
            callee != nullptr && callee->get_name() == "__typeof__") {
            if (call_expr->get_arguments().size() != 1) {
                add_error(semantic_context,
                          "__typeof__ expects exactly one expression",
                          call_expr->get_source_span());
                return;
            }
            const Expr *argument = call_expr->get_arguments().front().get();
            analyze_expr(argument, semantic_context, scope_stack);
            semantic_model.bind_node_type(call_expr,
                                          semantic_model.get_node_type(argument));
            return;
        }
        if (const IdentifierExpr *callee = get_callee_identifier(call_expr);
            callee != nullptr &&
            callee->get_name() == "__builtin_types_compatible_p") {
            if (call_expr->get_arguments().size() != 2) {
                add_error(semantic_context,
                          "__builtin_types_compatible_p expects two type "
                          "arguments",
                          call_expr->get_source_span());
                return;
            }
            const Expr *lhs = call_expr->get_arguments()[0].get();
            const Expr *rhs = call_expr->get_arguments()[1].get();
            analyze_expr(lhs, semantic_context, scope_stack);
            analyze_expr(rhs, semantic_context, scope_stack);
            const SemanticType *lhs_type = semantic_model.get_node_type(lhs);
            const SemanticType *rhs_type = semantic_model.get_node_type(rhs);
            const long long is_compatible =
                lhs_type != nullptr && rhs_type != nullptr &&
                        conversion_checker_.is_same_type(lhs_type, rhs_type)
                    ? 1LL
                    : 0LL;
            semantic_model.bind_node_type(call_expr,
                                          get_int_semantic_type(semantic_model));
            constant_evaluator_.bind_integer_constant_value(
                call_expr, is_compatible, semantic_context);
            return;
        }
        analyze_expr(call_expr->get_callee(), semantic_context, scope_stack);
        for (const auto &argument : call_expr->get_arguments()) {
            analyze_expr(argument.get(), semantic_context, scope_stack);
        }

        const SemanticSymbol *callee_symbol =
            semantic_model.get_symbol_binding(call_expr->get_callee());
        const SemanticType *callee_type = nullptr;
        if (callee_symbol != nullptr) {
            callee_type = callee_symbol->get_type();
        } else {
            callee_type = semantic_model.get_node_type(call_expr->get_callee());
        }
        if (callee_type == nullptr) {
            return;
        }

        const auto *function_type = get_callable_function_type(callee_type);
        if (function_type == nullptr) {
            add_error(semantic_context, "called object is not a function",
                      call_expr->get_source_span());
            return;
        }
        const std::size_t fixed_parameter_count =
            function_type->get_parameter_types().size();
        const std::size_t argument_count = call_expr->get_arguments().size();
        const bool variadic = function_type->get_is_variadic();
        if ((variadic && argument_count < fixed_parameter_count) ||
            (!variadic && fixed_parameter_count != argument_count)) {
            add_error(semantic_context,
                      "function call argument count does not match declaration",
                      call_expr->get_source_span());
        } else {
            for (std::size_t index = 0; index < fixed_parameter_count; ++index) {
                const SemanticType *argument_type =
                    semantic_model.get_node_type(call_expr->get_arguments()[index].get());
                const SemanticType *parameter_type =
                    function_type->get_parameter_types()[index];
                if (argument_type != nullptr && parameter_type != nullptr &&
                    !conversion_checker_.is_assignable_value(
                        parameter_type, argument_type,
                        call_expr->get_arguments()[index].get(), semantic_context,
                        constant_evaluator_)) {
                    if (conversion_checker_.is_incompatible_pointer_assignment(
                            parameter_type, argument_type, semantic_model)) {
                        add_warning(
                            semantic_context,
                            "function call argument uses incompatible pointer type",
                            call_expr->get_arguments()[index]->get_source_span(),
                            warning_options::kIncompatiblePointerTypes);
                    } else {
                        add_error(
                            semantic_context,
                            "function call argument type does not match declaration",
                            call_expr->get_arguments()[index]->get_source_span());
                        break;
                    }
                    break;
                } else if (argument_type != nullptr && parameter_type != nullptr &&
                           call_expr->get_arguments()[index]->get_kind() !=
                               AstKind::CastExpr &&
                           conversion_checker_.should_warn_implicit_integer_narrowing(
                               parameter_type, argument_type,
                               constant_evaluator_.get_integer_constant_value(
                                   call_expr->get_arguments()[index].get(),
                                   semantic_context))) {
                    add_warning(
                        semantic_context,
                        "implicit integer conversion may change value",
                        call_expr->get_arguments()[index]->get_source_span(),
                        warning_options::kConversion);
                }
            }
        }
        semantic_model.bind_node_type(call_expr, function_type->get_return_type());
        return;
    }
    case AstKind::IndexExpr: {
        const auto *index_expr = static_cast<const IndexExpr *>(expr);
        analyze_expr(index_expr->get_base(), semantic_context, scope_stack);
        analyze_expr(index_expr->get_index(), semantic_context, scope_stack);
        const SemanticType *base_type =
            semantic_model.get_node_type(index_expr->get_base());
        const SemanticType *index_type =
            semantic_model.get_node_type(index_expr->get_index());
        if (base_type == nullptr) {
            return;
        }
        const SemanticType *unqualified_base_type = strip_qualifiers(base_type);
        if (unqualified_base_type == nullptr ||
            (unqualified_base_type->get_kind() != SemanticTypeKind::Pointer &&
             unqualified_base_type->get_kind() != SemanticTypeKind::Array)) {
            add_error(semantic_context,
                      "subscripted object is not an indexable pointer",
                      index_expr->get_base()->get_source_span());
            return;
        }
        if (!conversion_checker_.is_integer_like_type(index_type)) {
            add_error(semantic_context,
                      "array subscript must have integer type",
                      index_expr->get_index()->get_source_span());
            return;
        }
        if (unqualified_base_type->get_kind() == SemanticTypeKind::Pointer) {
            const auto *pointer_type =
                static_cast<const PointerSemanticType *>(unqualified_base_type);
            semantic_model.bind_node_type(index_expr,
                                          pointer_type->get_pointee_type());
            return;
        }
        const auto *array_type =
            static_cast<const ArraySemanticType *>(unqualified_base_type);
        semantic_model.bind_node_type(index_expr, array_type->get_element_type());
        return;
    }
    case AstKind::MemberExpr: {
        const auto *member_expr = static_cast<const MemberExpr *>(expr);
        analyze_expr(member_expr->get_base(), semantic_context, scope_stack);
        const SemanticType *base_type =
            semantic_model.get_node_type(member_expr->get_base());
        const SemanticType *owner_object_type = get_member_object_type(
            base_type, member_expr->get_operator_text(), conversion_checker_);
        const SemanticType *owner_type = get_member_owner_type(
            base_type, member_expr->get_operator_text(), conversion_checker_);
        if (owner_type == nullptr) {
            add_error(
                semantic_context,
                "operator '" + member_expr->get_operator_text() +
                    "' requires a struct or union operand",
                member_expr->get_source_span());
            return;
        }

        if (const SemanticType *field_type =
                find_direct_union_field_type(owner_type,
                                             member_expr->get_member_name());
            field_type != nullptr) {
            semantic_model.bind_node_type(
                expr, apply_member_object_qualifiers(field_type,
                                                     owner_object_type,
                                                     semantic_model));
            return;
        }

        if (const SemanticType *field_type =
                find_direct_struct_field_type(owner_type,
                                              member_expr->get_member_name());
            field_type != nullptr) {
            semantic_model.bind_node_type(
                expr, apply_member_object_qualifiers(field_type,
                                                     owner_object_type,
                                                     semantic_model));
            return;
        }

        if (owner_type->get_kind() == SemanticTypeKind::Struct) {
            const std::string &struct_name =
                static_cast<const StructSemanticType *>(owner_type)->get_name();
            const SemanticSymbol *struct_symbol =
                scope_stack.lookup_tag(struct_name);
            if (struct_symbol != nullptr &&
                struct_symbol->get_kind() == SymbolKind::StructName) {
                if (const SemanticType *field_type =
                        find_direct_struct_field_type(
                            struct_symbol->get_type(),
                            member_expr->get_member_name());
                    field_type != nullptr) {
                    semantic_model.bind_node_type(
                        expr, apply_member_object_qualifiers(
                                  field_type, owner_object_type, semantic_model));
                    return;
                }
                if (struct_symbol->get_decl_node() == nullptr ||
                    struct_symbol->get_decl_node()->get_kind() !=
                        AstKind::StructDecl) {
                    add_error(semantic_context,
                              "member '" + member_expr->get_member_name() +
                                  "' does not exist in struct '" + struct_name + "'",
                              member_expr->get_source_span());
                    return;
                }
                const auto *struct_decl =
                    static_cast<const StructDecl *>(struct_symbol->get_decl_node());
                for (const auto &field : struct_decl->get_fields()) {
                    const auto *field_decl =
                        static_cast<const FieldDecl *>(field.get());
                    if (field_decl->get_name() == member_expr->get_member_name()) {
                        const SemanticType *resolved_field_type =
                            type_resolver_.apply_array_dimensions(
                                type_resolver_.resolve_type(
                                    field_decl->get_declared_type(),
                                    semantic_context, &scope_stack),
                                field_decl->get_dimensions(), semantic_context);
                        semantic_model.bind_node_type(
                            expr, apply_member_object_qualifiers(
                                      resolved_field_type,
                                      owner_object_type, semantic_model));
                        return;
                    }
                }
                add_error(semantic_context,
                          "member '" + member_expr->get_member_name() +
                              "' does not exist in struct '" + struct_name + "'",
                          member_expr->get_source_span());
                return;
            }
        }

        if (owner_type->get_kind() == SemanticTypeKind::Union) {
            const std::string &union_name =
                static_cast<const UnionSemanticType *>(owner_type)->get_name();
            const SemanticSymbol *union_symbol =
                scope_stack.lookup_tag(union_name);
            if (union_symbol != nullptr &&
                union_symbol->get_kind() == SymbolKind::UnionName) {
                if (const SemanticType *field_type =
                        find_direct_union_field_type(
                            union_symbol->get_type(),
                            member_expr->get_member_name());
                    field_type != nullptr) {
                    semantic_model.bind_node_type(
                        expr, apply_member_object_qualifiers(
                                  field_type, owner_object_type, semantic_model));
                    return;
                }
                if (union_symbol->get_decl_node() == nullptr ||
                    union_symbol->get_decl_node()->get_kind() !=
                        AstKind::UnionDecl) {
                    add_error(semantic_context,
                              "member '" + member_expr->get_member_name() +
                                  "' does not exist in union '" + union_name + "'",
                              member_expr->get_source_span());
                    return;
                }
                const auto *union_decl =
                    static_cast<const UnionDecl *>(union_symbol->get_decl_node());
                for (const auto &field : union_decl->get_fields()) {
                    const auto *field_decl =
                        static_cast<const FieldDecl *>(field.get());
                    if (field_decl->get_name() == member_expr->get_member_name()) {
                        const SemanticType *resolved_field_type =
                            type_resolver_.apply_array_dimensions(
                                type_resolver_.resolve_type(
                                    field_decl->get_declared_type(),
                                    semantic_context, &scope_stack),
                                field_decl->get_dimensions(), semantic_context);
                        semantic_model.bind_node_type(
                            expr, apply_member_object_qualifiers(
                                      resolved_field_type,
                                      owner_object_type, semantic_model));
                        return;
                    }
                }
                add_error(semantic_context,
                          "member '" + member_expr->get_member_name() +
                              "' does not exist in union '" + union_name + "'",
                          member_expr->get_source_span());
                return;
            }
        }

        semantic_model.bind_node_type(expr, owner_type);
        return;
    }
    case AstKind::StatementExpr: {
        const auto *statement_expr = static_cast<const StatementExpr *>(expr);
        analyze_statement_expr_stmt(statement_expr->get_body(), *this,
                                    semantic_context, scope_stack);
        const Expr *result_expr =
            get_statement_expr_result_expr(statement_expr->get_body());
        const SemanticType *result_type =
            result_expr == nullptr ? nullptr
                                   : semantic_model.get_node_type(result_expr);
        semantic_model.bind_node_type(
            statement_expr,
            result_type != nullptr ? result_type
                                   : get_void_semantic_type(semantic_model));
        return;
    }
    case AstKind::InitListExpr: {
        const auto *init_list_expr = static_cast<const InitListExpr *>(expr);
        for (const auto &element : init_list_expr->get_elements()) {
            analyze_expr(element.get(), semantic_context, scope_stack);
        }
        return;
    }
    default:
        return;
    }
}

} // namespace sysycc::detail
