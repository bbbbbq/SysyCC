#include "frontend/semantic/type_system/constant_evaluator.hpp"

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <optional>
#include <string>

#include "common/integer_literal.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/support/semantic_context.hpp"
#include "frontend/semantic/type_system/conversion_checker.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"
#include "frontend/semantic/type_system/type_layout.hpp"

namespace sysycc::detail {

namespace {

int hex_digit_value(char ch) noexcept {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool is_octal_digit(char ch) noexcept { return ch >= '0' && ch <= '7'; }

const SemanticType *strip_qualifiers(const SemanticType *type) {
    const SemanticType *current = type;
    while (current != nullptr &&
           current->get_kind() == SemanticTypeKind::Qualified) {
        current = static_cast<const QualifiedSemanticType *>(current)
                      ->get_base_type();
    }
    return current;
}

bool is_integer_like_semantic_type(const SemanticType *type) {
    const SemanticType *unqualified = strip_qualifiers(type);
    if (unqualified == nullptr) {
        return false;
    }
    if (unqualified->get_kind() == SemanticTypeKind::Enum) {
        return true;
    }
    if (unqualified->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const std::string &name =
        static_cast<const BuiltinSemanticType *>(unqualified)->get_name();
    return name == "char" || name == "signed char" || name == "unsigned char" ||
           name == "short" || name == "unsigned short" || name == "int" ||
           name == "unsigned int" || name == "long int" ||
           name == "unsigned long" || name == "long long int" ||
           name == "unsigned long long" || name == "ptrdiff_t" ||
           name == "size_t";
}

bool is_float_semantic_type(const SemanticType *type) {
    const SemanticType *unqualified = strip_qualifiers(type);
    if (unqualified == nullptr ||
        unqualified->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const std::string &name =
        static_cast<const BuiltinSemanticType *>(unqualified)->get_name();
    return name == "_Float16" || name == "float" || name == "double" ||
           name == "long double";
}

std::optional<long long> get_simple_integer_literal_value(const Expr *expr) {
    const auto *integer_literal = dynamic_cast<const IntegerLiteralExpr *>(expr);
    if (integer_literal == nullptr) {
        return std::nullopt;
    }
    return parse_integer_literal(integer_literal->get_value_text());
}

const CallExpr *get_gnu_typeof_call_expr(const Expr *expr) {
    const auto *call_expr = dynamic_cast<const CallExpr *>(expr);
    if (call_expr == nullptr) {
        return nullptr;
    }
    const auto *callee =
        dynamic_cast<const IdentifierExpr *>(call_expr->get_callee());
    if (callee == nullptr || callee->get_name() != "__typeof__" ||
        call_expr->get_arguments().size() != 1) {
        return nullptr;
    }
    return call_expr;
}

std::optional<long long> evaluate_gnu_typeof_signedness_probe(
    const BinaryExpr *binary_expr, const SemanticModel &semantic_model) {
    if (binary_expr == nullptr ||
        (binary_expr->get_operator_text() != "<" &&
         binary_expr->get_operator_text() != ">")) {
        return std::nullopt;
    }

    const auto rhs_value =
        get_simple_integer_literal_value(binary_expr->get_rhs());
    if (!rhs_value.has_value() || *rhs_value != 0) {
        return std::nullopt;
    }

    const auto *sub_expr = dynamic_cast<const BinaryExpr *>(binary_expr->get_lhs());
    if (sub_expr == nullptr || sub_expr->get_operator_text() != "-") {
        return std::nullopt;
    }
    const auto subtract_value =
        get_simple_integer_literal_value(sub_expr->get_rhs());
    if (!subtract_value.has_value() || *subtract_value != 1) {
        return std::nullopt;
    }

    const CallExpr *typeof_call = get_gnu_typeof_call_expr(sub_expr->get_lhs());
    if (typeof_call == nullptr) {
        return std::nullopt;
    }
    const SemanticType *typeof_argument_type =
        semantic_model.get_node_type(typeof_call->get_arguments().front().get());
    const auto type_info =
        IntegerConversionService().get_integer_type_info(typeof_argument_type);
    if (!type_info.has_value()) {
        return std::nullopt;
    }

    const bool is_signed = type_info->get_is_signed();
    if (binary_expr->get_operator_text() == "<") {
        return is_signed ? 1LL : 0LL;
    }
    return is_signed ? 0LL : 1LL;
}

bool is_pointer_semantic_type(const SemanticType *type) {
    const SemanticType *unqualified = strip_qualifiers(type);
    return unqualified != nullptr &&
           unqualified->get_kind() == SemanticTypeKind::Pointer;
}

bool is_character_semantic_type(const SemanticType *type) {
    const SemanticType *unqualified = strip_qualifiers(type);
    if (unqualified == nullptr ||
        unqualified->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    const std::string &name =
        static_cast<const BuiltinSemanticType *>(unqualified)->get_name();
    return name == "char" || name == "signed char" || name == "unsigned char";
}

bool consumes_array_subinitializer_directly(
    const Expr *expr, const ArraySemanticType *array_type) {
    if (expr == nullptr || array_type == nullptr) {
        return false;
    }
    if (expr->get_kind() == AstKind::InitListExpr) {
        return true;
    }
    return expr->get_kind() == AstKind::StringLiteralExpr &&
           is_character_semantic_type(array_type->get_element_type());
}

bool is_global_storage_symbol(const SemanticSymbol *symbol,
                              const SemanticModel &semantic_model) {
    if (symbol == nullptr) {
        return false;
    }
    if (symbol->get_kind() == SymbolKind::Function) {
        return true;
    }
    const VariableSemanticInfo *info = semantic_model.get_variable_info(symbol);
    return info != nullptr && info->get_is_global_storage();
}

const SemanticType *get_member_owner_type(const SemanticType *base_type,
                                          const std::string &operator_text) {
    const SemanticType *unqualified_base_type = strip_qualifiers(base_type);
    if (operator_text == "->") {
        if (unqualified_base_type == nullptr ||
            unqualified_base_type->get_kind() != SemanticTypeKind::Pointer) {
            return nullptr;
        }
        const auto *pointer_type =
            static_cast<const PointerSemanticType *>(unqualified_base_type);
        return strip_qualifiers(pointer_type->get_pointee_type());
    }
    if (operator_text == ".") {
        return unqualified_base_type;
    }
    return nullptr;
}

std::optional<std::size_t>
get_struct_field_offset(const StructSemanticType *struct_type,
                        const std::string &field_name) {
    if (struct_type == nullptr || struct_type->get_fields().empty()) {
        return std::nullopt;
    }

    std::size_t offset = 0;
    bool active_bit_field_unit = false;
    const SemanticType *active_storage_type = nullptr;
    std::size_t active_storage_bits = 0;
    std::size_t active_used_bits = 0;
    IntegerConversionService conversion_service;

    for (const auto &field : struct_type->get_fields()) {
        const SemanticType *field_type =
            strip_layout_qualifiers(field.get_type());
        const auto field_alignment = get_semantic_type_alignment(field_type);
        if (!field_alignment.has_value()) {
            return std::nullopt;
        }

        if (field.get_is_bit_field()) {
            const auto integer_info =
                conversion_service.get_integer_type_info(field_type);
            if (!integer_info.has_value()) {
                return std::nullopt;
            }
            const auto field_size = get_semantic_type_size(field_type);
            if (!field_size.has_value()) {
                return std::nullopt;
            }
            const std::size_t bit_width =
                static_cast<std::size_t>(field.get_bit_width().value_or(0));
            if (field.get_name() == field_name) {
                return std::nullopt;
            }
            if (bit_width == 0) {
                active_bit_field_unit = false;
                active_storage_type = nullptr;
                active_storage_bits = 0;
                active_used_bits = 0;
                offset = align_layout_offset(offset, *field_alignment);
                continue;
            }
            const bool needs_new_unit =
                !active_bit_field_unit ||
                !have_same_layout_unqualified_type(active_storage_type,
                                                   field_type) ||
                active_used_bits + bit_width > active_storage_bits;
            if (needs_new_unit) {
                const std::size_t storage_start =
                    floor_layout_offset(offset, *field_alignment);
                if (offset == storage_start ||
                    offset + round_up_layout_bits_to_bytes(bit_width) >
                        storage_start + *field_size) {
                    offset = align_layout_offset(offset, *field_alignment);
                    active_storage_bits =
                        static_cast<std::size_t>(integer_info->get_bit_width());
                } else {
                    const std::size_t available_bits =
                        (storage_start + *field_size - offset) * 8U;
                    active_storage_bits =
                        choose_layout_bit_field_storage_bits(available_bits,
                                                             bit_width);
                }
                offset += round_up_layout_bits_to_bytes(active_storage_bits);
                active_bit_field_unit = true;
                active_storage_type = field_type;
                active_used_bits = 0;
            }
            active_used_bits += bit_width;
            if (active_used_bits >= active_storage_bits) {
                active_bit_field_unit = false;
                active_storage_type = nullptr;
                active_storage_bits = 0;
                active_used_bits = 0;
            }
            continue;
        }

        active_bit_field_unit = false;
        active_storage_type = nullptr;
        active_storage_bits = 0;
        active_used_bits = 0;

        offset = align_layout_offset(offset, *field_alignment);
        if (field.get_name() == field_name) {
            return offset;
        }
        const auto field_size = get_semantic_type_size(field_type);
        if (!field_size.has_value()) {
            return std::nullopt;
        }
        offset += *field_size;
    }
    return std::nullopt;
}

std::optional<long long>
evaluate_address_constant_expr(const Expr *expr,
                               const SemanticModel &semantic_model) {
    if (expr == nullptr) {
        return std::nullopt;
    }

    if (const auto *cast_expr = dynamic_cast<const CastExpr *>(expr);
        cast_expr != nullptr) {
        return evaluate_address_constant_expr(cast_expr->get_operand(),
                                              semantic_model);
    }

    if (const auto *member_expr = dynamic_cast<const MemberExpr *>(expr);
        member_expr != nullptr) {
        const auto base_address = evaluate_address_constant_expr(
            member_expr->get_base(), semantic_model);
        if (!base_address.has_value()) {
            return std::nullopt;
        }
        const SemanticType *owner_type = get_member_owner_type(
            semantic_model.get_node_type(member_expr->get_base()),
            member_expr->get_operator_text());
        owner_type = strip_qualifiers(owner_type);
        if (owner_type == nullptr) {
            return std::nullopt;
        }
        if (owner_type->get_kind() == SemanticTypeKind::Union) {
            return *base_address;
        }
        if (owner_type->get_kind() != SemanticTypeKind::Struct) {
            return std::nullopt;
        }
        const auto field_offset = get_struct_field_offset(
            static_cast<const StructSemanticType *>(owner_type),
            member_expr->get_member_name());
        if (!field_offset.has_value()) {
            return std::nullopt;
        }
        return *base_address + static_cast<long long>(*field_offset);
    }

    return ConstantEvaluator().get_integer_constant_value(expr, semantic_model);
}

std::optional<long long>
apply_integer_like_constant_cast(long long value,
                                 const SemanticType *target_type) {
    target_type = strip_qualifiers(target_type);
    if (target_type == nullptr) {
        return std::nullopt;
    }
    if (target_type->get_kind() == SemanticTypeKind::Pointer) {
        return value == 0 ? std::optional<long long>(0) : std::nullopt;
    }
    if (target_type->get_kind() == SemanticTypeKind::Enum) {
        return value;
    }

    IntegerConversionService conversion_service;
    const auto type_info =
        conversion_service.get_integer_type_info(target_type);
    if (!type_info.has_value()) {
        return value;
    }

    std::uint64_t bits = static_cast<std::uint64_t>(value);
    if (type_info->get_bit_width() > 0 && type_info->get_bit_width() < 64) {
        const std::uint64_t mask =
            (std::uint64_t{1} << type_info->get_bit_width()) - 1;
        bits &= mask;
        if (type_info->get_is_signed()) {
            const std::uint64_t sign_bit = std::uint64_t{1}
                                           << (type_info->get_bit_width() - 1);
            if ((bits & sign_bit) != 0) {
                bits |= ~mask;
            }
        }
    }

    return static_cast<long long>(bits);
}

std::optional<long double>
apply_scalar_constant_cast(long double value, const SemanticType *target_type) {
    target_type = strip_qualifiers(target_type);
    if (target_type == nullptr) {
        return std::nullopt;
    }
    if (is_float_semantic_type(target_type)) {
        return value;
    }
    if (is_integer_like_semantic_type(target_type) ||
        target_type->get_kind() == SemanticTypeKind::Pointer) {
        const auto integer_value = apply_integer_like_constant_cast(
            static_cast<long long>(value), target_type);
        return integer_value.has_value()
                   ? std::optional<long double>(*integer_value)
                   : std::nullopt;
    }
    return std::nullopt;
}

} // namespace

std::optional<long long> ConstantEvaluator::get_integer_constant_value(
    const AstNode *node, const SemanticContext &semantic_context) const {
    return get_integer_constant_value(node,
                                      semantic_context.get_semantic_model());
}

std::optional<long long> ConstantEvaluator::get_integer_constant_value(
    const AstNode *node, const SemanticModel &semantic_model) const {
    const auto stored = semantic_model.get_integer_constant_value(node);
    if (stored.has_value()) {
        return stored;
    }
    const auto *expr = dynamic_cast<const Expr *>(node);
    if (expr == nullptr) {
        return std::nullopt;
    }
    return evaluate_integer_expr(expr, semantic_model);
}

void ConstantEvaluator::bind_integer_constant_value(
    const AstNode *node, long long value,
    SemanticContext &semantic_context) const {
    semantic_context.get_semantic_model().bind_integer_constant_value(node,
                                                                      value);
}

std::optional<long long>
ConstantEvaluator::get_scalar_constant_value_as_integer(
    const Expr *expr, const SemanticType *target_type,
    const SemanticContext &semantic_context) const {
    return get_scalar_constant_value_as_integer(
        expr, target_type, semantic_context.get_semantic_model());
}

std::optional<long long>
ConstantEvaluator::get_scalar_constant_value_as_integer(
    const Expr *expr, const SemanticType *target_type,
    const SemanticModel &semantic_model) const {
    const auto value = evaluate_scalar_numeric_expr(expr, semantic_model);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return convert_scalar_numeric_value_to_integer(*value, target_type);
}

bool ConstantEvaluator::is_integer_constant_expr(
    const Expr *expr, const SemanticContext &semantic_context,
    const ConversionChecker &conversion_checker) const {
    if (expr == nullptr) {
        return false;
    }
    const SemanticModel &semantic_model = semantic_context.get_semantic_model();
    const SemanticType *type = semantic_model.get_node_type(expr);
    if (!conversion_checker.is_integer_like_type(type)) {
        return false;
    }
    return get_integer_constant_value(expr,
                                      semantic_context.get_semantic_model())
        .has_value();
}

std::optional<long double> ConstantEvaluator::get_scalar_numeric_constant_value(
    const Expr *expr, const SemanticModel &semantic_model) const {
    return evaluate_scalar_numeric_expr(expr, semantic_model);
}

bool ConstantEvaluator::is_static_storage_initializer(
    const Expr *expr, const SemanticType *target_type,
    const SemanticModel &semantic_model) const {
    return is_static_storage_initializer_impl(expr, target_type,
                                              semantic_model);
}

std::optional<long long> ConstantEvaluator::evaluate_integer_expr(
    const Expr *expr, const SemanticContext &semantic_context) const {
    return evaluate_integer_expr(expr, semantic_context.get_semantic_model());
}

std::optional<long long> ConstantEvaluator::evaluate_integer_expr(
    const Expr *expr, const SemanticModel &semantic_model) const {
    if (expr == nullptr) {
        return std::nullopt;
    }

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr:
        return parse_integer_literal(
            static_cast<const IntegerLiteralExpr *>(expr)->get_value_text());
    case AstKind::CharLiteralExpr:
        return parse_char_literal(
            static_cast<const CharLiteralExpr *>(expr)->get_value_text());
    case AstKind::IdentifierExpr:
        return semantic_model.get_integer_constant_value(expr);
    case AstKind::SizeofTypeExpr:
        return semantic_model.get_integer_constant_value(expr);
    case AstKind::CastExpr: {
        const auto *cast_expr = static_cast<const CastExpr *>(expr);
        const auto operand = evaluate_scalar_numeric_expr(
            cast_expr->get_operand(), semantic_model);
        if (!operand.has_value()) {
            return std::nullopt;
        }
        return apply_integer_like_constant_cast(
            static_cast<long long>(*operand),
            semantic_model.get_node_type(expr));
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        if (unary_expr->get_operator_text() == "sizeof") {
            return semantic_model.get_integer_constant_value(expr);
        }
        const auto operand =
            evaluate_integer_expr(unary_expr->get_operand(), semantic_model);
        if (!operand.has_value()) {
            return std::nullopt;
        }
        const auto &op = unary_expr->get_operator_text();
        if (op == "+") {
            return *operand;
        }
        if (op == "-") {
            return -(*operand);
        }
        if (op == "!") {
            return *operand == 0 ? 1LL : 0LL;
        }
        if (op == "~") {
            return ~(*operand);
        }
        return std::nullopt;
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        if (const auto typeof_probe =
                evaluate_gnu_typeof_signedness_probe(binary_expr, semantic_model);
            typeof_probe.has_value()) {
            return typeof_probe;
        }
        const auto lhs =
            evaluate_integer_expr(binary_expr->get_lhs(), semantic_model);
        const auto rhs =
            evaluate_integer_expr(binary_expr->get_rhs(), semantic_model);
        if (!lhs.has_value() || !rhs.has_value()) {
            return std::nullopt;
        }
        const auto &op = binary_expr->get_operator_text();
        if (op == "+") {
            return *lhs + *rhs;
        }
        if (op == "-") {
            return *lhs - *rhs;
        }
        if (op == "*") {
            return *lhs * *rhs;
        }
        if (op == "/") {
            return *rhs == 0 ? std::nullopt
                             : std::optional<long long>(*lhs / *rhs);
        }
        if (op == "%") {
            return *rhs == 0 ? std::nullopt
                             : std::optional<long long>(*lhs % *rhs);
        }
        if (op == "<<") {
            return *lhs << *rhs;
        }
        if (op == ">>") {
            return *lhs >> *rhs;
        }
        if (op == "&") {
            return *lhs & *rhs;
        }
        if (op == "|") {
            return *lhs | *rhs;
        }
        if (op == "^") {
            return *lhs ^ *rhs;
        }
        if (op == "&&") {
            return (*lhs != 0 && *rhs != 0) ? 1LL : 0LL;
        }
        if (op == "||") {
            return (*lhs != 0 || *rhs != 0) ? 1LL : 0LL;
        }
        if (op == "<") {
            return *lhs < *rhs ? 1LL : 0LL;
        }
        if (op == "<=") {
            return *lhs <= *rhs ? 1LL : 0LL;
        }
        if (op == ">") {
            return *lhs > *rhs ? 1LL : 0LL;
        }
        if (op == ">=") {
            return *lhs >= *rhs ? 1LL : 0LL;
        }
        if (op == "==") {
            return *lhs == *rhs ? 1LL : 0LL;
        }
        if (op == "!=") {
            return *lhs != *rhs ? 1LL : 0LL;
        }
        return std::nullopt;
    }
    case AstKind::ConditionalExpr: {
        const auto *conditional_expr =
            static_cast<const ConditionalExpr *>(expr);
        const auto condition = evaluate_scalar_numeric_expr(
            conditional_expr->get_condition(), semantic_model);
        if (!condition.has_value()) {
            return std::nullopt;
        }
        return evaluate_integer_expr(*condition != 0
                                         ? conditional_expr->get_true_expr()
                                         : conditional_expr->get_false_expr(),
                                     semantic_model);
    }
    default:
        return std::nullopt;
    }
}

std::optional<long double> ConstantEvaluator::evaluate_scalar_numeric_expr(
    const Expr *expr, const SemanticContext &semantic_context) const {
    return evaluate_scalar_numeric_expr(expr,
                                        semantic_context.get_semantic_model());
}

std::optional<long double> ConstantEvaluator::evaluate_scalar_numeric_expr(
    const Expr *expr, const SemanticModel &semantic_model) const {
    if (expr == nullptr) {
        return std::nullopt;
    }

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr: {
        const auto value = parse_integer_literal(
            static_cast<const IntegerLiteralExpr *>(expr)->get_value_text());
        return value.has_value() ? std::optional<long double>(*value)
                                 : std::nullopt;
    }
    case AstKind::FloatLiteralExpr: {
        const std::string &text =
            static_cast<const FloatLiteralExpr *>(expr)->get_value_text();
        char *end = nullptr;
        const long double value = std::strtold(text.c_str(), &end);
        if (end == nullptr || *end != '\0') {
            return std::nullopt;
        }
        return value;
    }
    case AstKind::CharLiteralExpr: {
        const auto value = parse_char_literal(
            static_cast<const CharLiteralExpr *>(expr)->get_value_text());
        return value.has_value() ? std::optional<long double>(*value)
                                 : std::nullopt;
    }
    case AstKind::IdentifierExpr: {
        const auto value = semantic_model.get_integer_constant_value(expr);
        return value.has_value() ? std::optional<long double>(*value)
                                 : std::nullopt;
    }
    case AstKind::SizeofTypeExpr: {
        const auto value = semantic_model.get_integer_constant_value(expr);
        return value.has_value() ? std::optional<long double>(*value)
                                 : std::nullopt;
    }
    case AstKind::CastExpr: {
        const auto *cast_expr = static_cast<const CastExpr *>(expr);
        const auto operand = evaluate_scalar_numeric_expr(
            cast_expr->get_operand(), semantic_model);
        if (!operand.has_value()) {
            return std::nullopt;
        }
        return apply_scalar_constant_cast(*operand,
                                          semantic_model.get_node_type(expr));
    }
    case AstKind::UnaryExpr: {
        const auto *unary_expr = static_cast<const UnaryExpr *>(expr);
        if (unary_expr->get_operator_text() == "&") {
            const auto address = evaluate_address_constant_expr(
                unary_expr->get_operand(), semantic_model);
            return address.has_value() ? std::optional<long double>(*address)
                                       : std::nullopt;
        }
        if (unary_expr->get_operator_text() == "sizeof") {
            const auto value = semantic_model.get_integer_constant_value(expr);
            return value.has_value() ? std::optional<long double>(*value)
                                     : std::nullopt;
        }
        const auto operand = evaluate_scalar_numeric_expr(
            unary_expr->get_operand(), semantic_model);
        if (!operand.has_value()) {
            return std::nullopt;
        }
        const auto &op = unary_expr->get_operator_text();
        if (op == "+") {
            return *operand;
        }
        if (op == "-") {
            return -(*operand);
        }
        if (op == "!") {
            return *operand == 0 ? 1.0L : 0.0L;
        }
        if (op == "~") {
            const auto integer_operand = evaluate_integer_expr(
                unary_expr->get_operand(), semantic_model);
            return integer_operand.has_value()
                       ? std::optional<long double>(~(*integer_operand))
                       : std::nullopt;
        }
        return std::nullopt;
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        const auto lhs = evaluate_scalar_numeric_expr(binary_expr->get_lhs(),
                                                      semantic_model);
        const auto rhs = evaluate_scalar_numeric_expr(binary_expr->get_rhs(),
                                                      semantic_model);
        if (!lhs.has_value() || !rhs.has_value()) {
            return std::nullopt;
        }
        const auto &op = binary_expr->get_operator_text();
        if (op == "+") {
            return *lhs + *rhs;
        }
        if (op == "-") {
            return *lhs - *rhs;
        }
        if (op == "*") {
            return *lhs * *rhs;
        }
        if (op == "/") {
            return *rhs == 0 ? std::nullopt
                             : std::optional<long double>(*lhs / *rhs);
        }
        if (op == "%") {
            const long long lhs_int = static_cast<long long>(*lhs);
            const long long rhs_int = static_cast<long long>(*rhs);
            if (rhs_int == 0) {
                return std::nullopt;
            }
            return static_cast<long double>(lhs_int % rhs_int);
        }
        if (op == "<<") {
            return static_cast<long double>(static_cast<long long>(*lhs)
                                            << static_cast<long long>(*rhs));
        }
        if (op == ">>") {
            return static_cast<long double>(static_cast<long long>(*lhs) >>
                                            static_cast<long long>(*rhs));
        }
        if (op == "&") {
            return static_cast<long double>(static_cast<long long>(*lhs) &
                                            static_cast<long long>(*rhs));
        }
        if (op == "|") {
            return static_cast<long double>(static_cast<long long>(*lhs) |
                                            static_cast<long long>(*rhs));
        }
        if (op == "^") {
            return static_cast<long double>(static_cast<long long>(*lhs) ^
                                            static_cast<long long>(*rhs));
        }
        if (op == "&&") {
            return (*lhs != 0 && *rhs != 0) ? 1.0L : 0.0L;
        }
        if (op == "||") {
            return (*lhs != 0 || *rhs != 0) ? 1.0L : 0.0L;
        }
        if (op == "<") {
            return *lhs < *rhs ? 1.0L : 0.0L;
        }
        if (op == "<=") {
            return *lhs <= *rhs ? 1.0L : 0.0L;
        }
        if (op == ">") {
            return *lhs > *rhs ? 1.0L : 0.0L;
        }
        if (op == ">=") {
            return *lhs >= *rhs ? 1.0L : 0.0L;
        }
        if (op == "==") {
            return *lhs == *rhs ? 1.0L : 0.0L;
        }
        if (op == "!=") {
            return *lhs != *rhs ? 1.0L : 0.0L;
        }
        return std::nullopt;
    }
    case AstKind::ConditionalExpr: {
        const auto *conditional_expr =
            static_cast<const ConditionalExpr *>(expr);
        const auto condition = evaluate_scalar_numeric_expr(
            conditional_expr->get_condition(), semantic_model);
        if (!condition.has_value()) {
            return std::nullopt;
        }
        const Expr *selected_expr = *condition != 0
                                        ? conditional_expr->get_true_expr()
                                        : conditional_expr->get_false_expr();
        const auto selected_value =
            evaluate_scalar_numeric_expr(selected_expr, semantic_model);
        if (!selected_value.has_value()) {
            return std::nullopt;
        }
        return apply_scalar_constant_cast(*selected_value,
                                          semantic_model.get_node_type(expr));
    }
    default:
        return std::nullopt;
    }
}

bool ConstantEvaluator::is_static_address_lvalue_expr(
    const Expr *expr, const SemanticModel &semantic_model) const {
    if (expr == nullptr) {
        return false;
    }
    if (const auto *cast_expr = dynamic_cast<const CastExpr *>(expr);
        cast_expr != nullptr) {
        return is_static_address_lvalue_expr(cast_expr->get_operand(),
                                             semantic_model);
    }
    if (const auto *identifier = dynamic_cast<const IdentifierExpr *>(expr);
        identifier != nullptr) {
        const SemanticSymbol *symbol =
            semantic_model.get_symbol_binding(identifier);
        return is_global_storage_symbol(symbol, semantic_model);
    }
    if (const auto *member_expr = dynamic_cast<const MemberExpr *>(expr);
        member_expr != nullptr) {
        return member_expr->get_operator_text() == "." &&
               is_static_address_lvalue_expr(member_expr->get_base(),
                                             semantic_model);
    }
    if (const auto *index_expr = dynamic_cast<const IndexExpr *>(expr);
        index_expr != nullptr) {
        return get_integer_constant_value(index_expr->get_index(),
                                          semantic_model)
                   .has_value() &&
               is_static_address_value_expr(index_expr->get_base(),
                                            semantic_model);
    }
    return false;
}

bool ConstantEvaluator::is_static_address_value_expr(
    const Expr *expr, const SemanticModel &semantic_model) const {
    if (expr == nullptr) {
        return false;
    }
    if (dynamic_cast<const StringLiteralExpr *>(expr) != nullptr) {
        return true;
    }
    if (const auto *cast_expr = dynamic_cast<const CastExpr *>(expr);
        cast_expr != nullptr) {
        return is_static_address_value_expr(cast_expr->get_operand(),
                                            semantic_model);
    }
    if (const auto *unary_expr = dynamic_cast<const UnaryExpr *>(expr);
        unary_expr != nullptr) {
        if (unary_expr->get_operator_text() == "&&") {
            return dynamic_cast<const IdentifierExpr *>(
                       unary_expr->get_operand()) != nullptr;
        }
        if (unary_expr->get_operator_text() == "&") {
            return is_static_address_lvalue_expr(unary_expr->get_operand(),
                                                 semantic_model);
        }
        return false;
    }
    if (const auto *binary_expr = dynamic_cast<const BinaryExpr *>(expr);
        binary_expr != nullptr) {
        const std::string &op = binary_expr->get_operator_text();
        if (op == "+" || op == "-") {
            const bool lhs_address = is_static_address_value_expr(
                binary_expr->get_lhs(), semantic_model);
            const bool rhs_address = is_static_address_value_expr(
                binary_expr->get_rhs(), semantic_model);
            const bool lhs_integer = get_integer_constant_value(
                                         binary_expr->get_lhs(), semantic_model)
                                         .has_value();
            const bool rhs_integer = get_integer_constant_value(
                                         binary_expr->get_rhs(), semantic_model)
                                         .has_value();
            if (op == "+") {
                return (lhs_address && rhs_integer) ||
                       (lhs_integer && rhs_address);
            }
            return lhs_address && rhs_integer;
        }
        return false;
    }

    const SemanticType *expr_type = semantic_model.get_node_type(expr);
    const SemanticType *unqualified_type = strip_qualifiers(expr_type);
    if (unqualified_type != nullptr &&
        (unqualified_type->get_kind() == SemanticTypeKind::Array ||
         unqualified_type->get_kind() == SemanticTypeKind::Function)) {
        return is_static_address_lvalue_expr(expr, semantic_model);
    }

    if (const auto *identifier = dynamic_cast<const IdentifierExpr *>(expr);
        identifier != nullptr) {
        const SemanticSymbol *symbol =
            semantic_model.get_symbol_binding(identifier);
        return symbol != nullptr && symbol->get_kind() == SymbolKind::Function;
    }

    return false;
}

bool ConstantEvaluator::is_static_storage_initializer_impl(
    const Expr *expr, const SemanticType *target_type,
    const SemanticModel &semantic_model) const {
    target_type = strip_qualifiers(target_type);
    if (target_type == nullptr) {
        return false;
    }

    const InitListExpr *scalar_init_list = nullptr;
    if (expr != nullptr && expr->get_kind() == AstKind::InitListExpr &&
        target_type->get_kind() != SemanticTypeKind::Array &&
        target_type->get_kind() != SemanticTypeKind::Struct &&
        target_type->get_kind() != SemanticTypeKind::Union) {
        scalar_init_list = static_cast<const InitListExpr *>(expr);
        if (scalar_init_list->get_elements().size() > 1) {
            return false;
        }
        expr = scalar_init_list->get_elements().empty()
                   ? nullptr
                   : scalar_init_list->get_elements().front().get();
    }

    if (expr == nullptr) {
        return true;
    }

    switch (target_type->get_kind()) {
    case SemanticTypeKind::Array: {
        const auto *array_type =
            static_cast<const ArraySemanticType *>(target_type);
        if (expr->get_kind() == AstKind::StringLiteralExpr &&
            is_character_semantic_type(array_type->get_element_type())) {
            return true;
        }
        if (expr->get_kind() != AstKind::InitListExpr) {
            return false;
        }
        const auto *init_list = static_cast<const InitListExpr *>(expr);
        std::function<bool(const ArraySemanticType *, const InitListExpr *,
                           std::size_t &)>
            validate_array_initializer =
                [&](const ArraySemanticType *current_array_type,
                    const InitListExpr *current_init_list,
                    std::size_t &cursor) -> bool {
            const auto *nested_array_type =
                dynamic_cast<const ArraySemanticType *>(
                    strip_qualifiers(current_array_type->get_element_type()));
            const std::vector<int> &dimensions =
                current_array_type->get_dimensions();
            const std::size_t element_count =
                !dimensions.empty() && dimensions.front() > 0
                    ? static_cast<std::size_t>(dimensions.front())
                    : current_init_list->get_elements().size();
            for (std::size_t index = 0; index < element_count; ++index) {
                const Expr *element_initializer =
                    cursor < current_init_list->get_elements().size()
                        ? current_init_list->get_elements()[cursor].get()
                        : nullptr;
                if (nested_array_type != nullptr &&
                    element_initializer != nullptr &&
                    !consumes_array_subinitializer_directly(
                        element_initializer, nested_array_type)) {
                    if (!validate_array_initializer(
                            nested_array_type, current_init_list, cursor)) {
                        return false;
                    }
                    continue;
                }
                if (element_initializer != nullptr) {
                    ++cursor;
                }
                if (!is_static_storage_initializer_impl(
                        element_initializer,
                        current_array_type->get_element_type(),
                        semantic_model)) {
                    return false;
                }
            }
            return true;
        };

        std::size_t cursor = 0;
        return validate_array_initializer(array_type, init_list, cursor) &&
               cursor == init_list->get_elements().size();
    }
    case SemanticTypeKind::Struct: {
        if (expr->get_kind() != AstKind::InitListExpr) {
            return false;
        }
        const auto *struct_type =
            static_cast<const StructSemanticType *>(target_type);
        const auto *init_list = static_cast<const InitListExpr *>(expr);
        std::size_t cursor = 0;
        bool sequential_initializer_is_valid = true;
        for (const auto &field : struct_type->get_fields()) {
            if (field.get_name().empty()) {
                continue;
            }
            const Expr *field_initializer =
                cursor < init_list->get_elements().size()
                    ? init_list->get_elements()[cursor].get()
                    : nullptr;
            if (field_initializer != nullptr) {
                ++cursor;
            }
            if (!is_static_storage_initializer_impl(
                    field_initializer, field.get_type(), semantic_model)) {
                sequential_initializer_is_valid = false;
                break;
            }
        }
        if (sequential_initializer_is_valid &&
            cursor == init_list->get_elements().size()) {
            return true;
        }

        // Designator metadata is currently erased by the AST compatibility
        // layer. Preserve GCC-compatible acceptance for static struct
        // initializers by allowing each provided value to match any field.
        for (const auto &element : init_list->get_elements()) {
            bool matched_field = false;
            for (const auto &field : struct_type->get_fields()) {
                if (field.get_name().empty()) {
                    continue;
                }
                if (is_static_storage_initializer_impl(
                        element.get(), field.get_type(), semantic_model)) {
                    matched_field = true;
                    break;
                }
            }
            if (!matched_field) {
                return false;
            }
        }
        return true;
    }
    case SemanticTypeKind::Union: {
        if (expr->get_kind() != AstKind::InitListExpr) {
            return false;
        }
        const auto *union_type =
            static_cast<const UnionSemanticType *>(target_type);
        const auto *init_list = static_cast<const InitListExpr *>(expr);
        if (init_list->get_elements().size() > 1) {
            return false;
        }
        if (union_type->get_fields().empty() ||
            init_list->get_elements().empty()) {
            return true;
        }
        return is_static_storage_initializer_impl(
            init_list->get_elements().front().get(),
            union_type->get_fields().front().get_type(), semantic_model);
    }
    default:
        break;
    }

    if (is_pointer_semantic_type(target_type)) {
        const auto integer_value =
            get_integer_constant_value(expr, semantic_model);
        if (integer_value.has_value() && *integer_value == 0) {
            return true;
        }
        if (const auto *cast_expr = dynamic_cast<const CastExpr *>(expr);
            cast_expr != nullptr &&
            is_pointer_semantic_type(semantic_model.get_node_type(cast_expr)) &&
            get_scalar_numeric_constant_value(cast_expr->get_operand(),
                                              semantic_model)
                .has_value()) {
            return true;
        }
        return is_static_address_value_expr(expr, semantic_model);
    }
    if (is_integer_like_semantic_type(target_type) ||
        is_float_semantic_type(target_type)) {
        return get_scalar_numeric_constant_value(expr, semantic_model)
            .has_value();
    }
    return false;
}

std::optional<long long>
ConstantEvaluator::convert_scalar_numeric_value_to_integer(
    long double value, const SemanticType *target_type) const {
    if (value >
            static_cast<long double>(std::numeric_limits<long long>::max()) ||
        value <
            static_cast<long double>(std::numeric_limits<long long>::min())) {
        return std::nullopt;
    }

    detail::IntegerConversionService integer_conversion_service;
    const auto type_info = integer_conversion_service.get_integer_type_info(
        strip_qualifiers(target_type));
    if (!type_info.has_value()) {
        return static_cast<long long>(value);
    }

    std::uint64_t bits =
        static_cast<std::uint64_t>(static_cast<long long>(value));
    if (type_info->get_bit_width() > 0 && type_info->get_bit_width() < 64) {
        const std::uint64_t mask =
            (std::uint64_t{1} << type_info->get_bit_width()) - 1;
        bits &= mask;
        if (type_info->get_is_signed()) {
            const std::uint64_t sign_bit = std::uint64_t{1}
                                           << (type_info->get_bit_width() - 1);
            if ((bits & sign_bit) != 0) {
                bits |= ~mask;
            }
        }
    }

    return static_cast<long long>(bits);
}

std::optional<long long>
ConstantEvaluator::parse_char_literal(const std::string &value_text) const {
    if (value_text.size() < 3 || value_text.front() != '\'' ||
        value_text.back() != '\'') {
        return std::nullopt;
    }
    if (value_text[1] != '\\') {
        return static_cast<long long>(
            static_cast<unsigned char>(value_text[1]));
    }
    if (value_text.size() < 4) {
        return std::nullopt;
    }
    switch (value_text[2]) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    case 'a':
        return '\a';
    case 'b':
        return '\b';
    case 'f':
        return '\f';
    case 'v':
        return '\v';
    case '\\':
        return '\\';
    case '\'':
        return '\'';
    case '"':
        return '"';
    case 'x': {
        unsigned int value = 0;
        bool consumed_digit = false;
        for (std::size_t index = 3; index + 1 < value_text.size(); ++index) {
            const int digit = hex_digit_value(value_text[index]);
            if (digit < 0) {
                break;
            }
            consumed_digit = true;
            value = (value << 4) | static_cast<unsigned int>(digit);
        }
        if (!consumed_digit) {
            return static_cast<long long>(
                static_cast<unsigned char>(value_text[2]));
        }
        return static_cast<long long>(value & 0xffU);
    }
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': {
        unsigned int value = static_cast<unsigned int>(value_text[2] - '0');
        int digits = 1;
        for (std::size_t index = 3;
             digits < 3 && index + 1 < value_text.size() &&
             is_octal_digit(value_text[index]);
             ++index, ++digits) {
            value = (value << 3) |
                    static_cast<unsigned int>(value_text[index] - '0');
        }
        return static_cast<long long>(value & 0xffU);
    }
    default:
        return static_cast<long long>(
            static_cast<unsigned char>(value_text[2]));
    }
}

} // namespace sysycc::detail
