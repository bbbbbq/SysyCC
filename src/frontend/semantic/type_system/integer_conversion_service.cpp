#include "frontend/semantic/type_system/integer_conversion_service.hpp"

#include <memory>
#include <string>
#include <string_view>

#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/model/semantic_model.hpp"

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

const SemanticType *make_builtin_type(SemanticModel &semantic_model,
                                      std::string_view name) {
    return semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>(std::string(name)));
}

std::optional<int> get_integer_rank_from_name(std::string_view name) {
    if (name == "char" || name == "signed char" || name == "unsigned char") {
        return 1;
    }
    if (name == "short" || name == "unsigned short") {
        return 2;
    }
    if (name == "int" || name == "unsigned int" || name == "ptrdiff_t" ||
        name == "enum") {
        return 3;
    }
    if (name == "long int") {
        return 4;
    }
    if (name == "long long int" || name == "unsigned long long") {
        return 5;
    }
    return std::nullopt;
}

std::optional<int> get_floating_rank_from_name(std::string_view name) {
    if (name == "_Float16") {
        return 1;
    }
    if (name == "float") {
        return 2;
    }
    if (name == "double") {
        return 3;
    }
    if (name == "long double") {
        return 4;
    }
    return std::nullopt;
}

const SemanticType *get_canonical_integer_type_for_info(
    const IntegerTypeInfo &info, const SemanticType *source_type,
    SemanticModel &semantic_model) {
    if (info.get_bit_width() <= 0) {
        return nullptr;
    }

    const SemanticType *stripped_source = strip_qualifiers(source_type);
    if (stripped_source != nullptr &&
        stripped_source->get_kind() == SemanticTypeKind::Builtin) {
        const auto &source_name =
            static_cast<const BuiltinSemanticType *>(stripped_source)->get_name();
        if (info.get_rank() <= 1) {
            if (source_name == "unsigned char") {
                return make_builtin_type(semantic_model, "unsigned char");
            }
            return make_builtin_type(semantic_model, "char");
        }
        if (info.get_rank() == 2) {
            if (source_name == "unsigned short") {
                return make_builtin_type(semantic_model, "unsigned short");
            }
            return make_builtin_type(semantic_model, "short");
        }
        if (info.get_rank() == 3) {
            if (source_name == "unsigned int") {
                return make_builtin_type(semantic_model, "unsigned int");
            }
            return make_builtin_type(semantic_model, "int");
        }
        if (info.get_rank() == 4) {
            if (source_name == "ptrdiff_t") {
                return make_builtin_type(semantic_model, "ptrdiff_t");
            }
            return make_builtin_type(semantic_model, "long int");
        }
        if (info.get_rank() == 5) {
            if (source_name == "unsigned long long") {
                return make_builtin_type(semantic_model, "unsigned long long");
            }
            return make_builtin_type(semantic_model, "long long int");
        }
    }

    if (!info.get_is_signed()) {
        if (info.get_rank() == 1) {
            return make_builtin_type(semantic_model, "unsigned char");
        }
        if (info.get_rank() == 2) {
            return make_builtin_type(semantic_model, "unsigned short");
        }
        if (info.get_rank() == 3) {
            return make_builtin_type(semantic_model, "unsigned int");
        }
        if (info.get_rank() == 5) {
            return make_builtin_type(semantic_model, "unsigned long long");
        }
    }

    if (info.get_rank() == 1) {
        return make_builtin_type(semantic_model, "char");
    }
    if (info.get_rank() == 2) {
        return make_builtin_type(semantic_model, "short");
    }
    if (info.get_rank() == 3) {
        return make_builtin_type(semantic_model, "int");
    }
    if (info.get_rank() == 4) {
        return make_builtin_type(semantic_model, "long int");
    }
    if (info.get_rank() == 5) {
        return make_builtin_type(semantic_model, "long long int");
    }
    return nullptr;
}

} // namespace

std::optional<IntegerTypeInfo>
IntegerConversionService::get_integer_type_info(const SemanticType *type) const {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return std::nullopt;
    }

    if (type->get_kind() == SemanticTypeKind::Enum) {
        return IntegerTypeInfo(true, 32, 4);
    }

    if (type->get_kind() != SemanticTypeKind::Builtin) {
        return std::nullopt;
    }

    const std::string &name =
        static_cast<const BuiltinSemanticType *>(type)->get_name();
    if (name == "char" || name == "signed char") {
        return IntegerTypeInfo(true, 8, 1);
    }
    if (name == "unsigned char") {
        return IntegerTypeInfo(false, 8, 1);
    }
    if (name == "short") {
        return IntegerTypeInfo(true, 16, 2);
    }
    if (name == "unsigned short") {
        return IntegerTypeInfo(false, 16, 2);
    }
    if (name == "int") {
        return IntegerTypeInfo(true, 32, 3);
    }
    if (name == "unsigned int") {
        return IntegerTypeInfo(false, 32, 3);
    }
    if (name == "long int") {
        return IntegerTypeInfo(true, 64, 4);
    }
    if (name == "long long int") {
        return IntegerTypeInfo(true, 64, 5);
    }
    if (name == "unsigned long long") {
        return IntegerTypeInfo(false, 64, 5);
    }
    if (name == "ptrdiff_t") {
        return IntegerTypeInfo(true, 64, 4);
    }

    return std::nullopt;
}

std::optional<int>
IntegerConversionService::get_floating_type_rank(const SemanticType *type) const {
    type = strip_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Builtin) {
        return std::nullopt;
    }
    return get_floating_rank_from_name(
        static_cast<const BuiltinSemanticType *>(type)->get_name());
}

const SemanticType *IntegerConversionService::get_integer_promotion_type(
    const SemanticType *type, SemanticModel &semantic_model) const {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return nullptr;
    }
    if (type->get_kind() == SemanticTypeKind::Enum) {
        return make_builtin_type(semantic_model, "int");
    }

    const auto info = get_integer_type_info(type);
    if (!info.has_value()) {
        return nullptr;
    }

    if (info->get_rank() < 3) {
        return make_builtin_type(semantic_model, "int");
    }
    return type;
}

const SemanticType *IntegerConversionService::get_common_integer_type(
    const SemanticType *lhs, const SemanticType *rhs,
    SemanticModel &semantic_model) const {
    const SemanticType *promoted_lhs = get_integer_promotion_type(lhs, semantic_model);
    const SemanticType *promoted_rhs = get_integer_promotion_type(rhs, semantic_model);
    if (promoted_lhs == nullptr || promoted_rhs == nullptr) {
        return nullptr;
    }

    const auto lhs_info = get_integer_type_info(promoted_lhs);
    const auto rhs_info = get_integer_type_info(promoted_rhs);
    if (!lhs_info.has_value() || !rhs_info.has_value()) {
        return nullptr;
    }

    if (lhs_info->get_rank() == rhs_info->get_rank() &&
        lhs_info->get_is_signed() == rhs_info->get_is_signed()) {
        if (lhs_info->get_bit_width() == rhs_info->get_bit_width()) {
            if (lhs_info->get_is_signed()) {
                if (strip_qualifiers(promoted_lhs) != nullptr &&
                    strip_qualifiers(promoted_lhs)->get_kind() ==
                        SemanticTypeKind::Builtin) {
                    const auto &lhs_name = static_cast<const BuiltinSemanticType *>(
                                               strip_qualifiers(promoted_lhs))
                                               ->get_name();
                    const auto &rhs_name = static_cast<const BuiltinSemanticType *>(
                                               strip_qualifiers(promoted_rhs))
                                               ->get_name();
                    if (lhs_name == "ptrdiff_t" || rhs_name == "ptrdiff_t") {
                        return make_builtin_type(semantic_model, "ptrdiff_t");
                    }
                    if (lhs_name == "long int" || rhs_name == "long int") {
                        return make_builtin_type(semantic_model, "long int");
                    }
                    if (lhs_name == "long long int" || rhs_name == "long long int") {
                        return make_builtin_type(semantic_model, "long long int");
                    }
                }
            }
            return promoted_lhs;
        }
    }

    if (lhs_info->get_is_signed() == rhs_info->get_is_signed()) {
        const IntegerTypeInfo &preferred_info =
            lhs_info->get_rank() >= rhs_info->get_rank() ? *lhs_info : *rhs_info;
        const SemanticType *preferred_source =
            lhs_info->get_rank() >= rhs_info->get_rank() ? promoted_lhs : promoted_rhs;
        return get_canonical_integer_type_for_info(preferred_info, preferred_source,
                                                   semantic_model);
    }

    const IntegerTypeInfo *signed_info = lhs_info->get_is_signed() ? &*lhs_info
                                                                    : &*rhs_info;
    const IntegerTypeInfo *unsigned_info = lhs_info->get_is_signed() ? &*rhs_info
                                                                      : &*lhs_info;
    const SemanticType *signed_type = lhs_info->get_is_signed() ? promoted_lhs
                                                                : promoted_rhs;
    const SemanticType *unsigned_type = lhs_info->get_is_signed() ? promoted_rhs
                                                                  : promoted_lhs;

    if (signed_info->get_bit_width() > unsigned_info->get_bit_width()) {
        return signed_type;
    }

    if (signed_info->get_bit_width() == unsigned_info->get_bit_width() &&
        signed_info->get_rank() == unsigned_info->get_rank()) {
        return unsigned_type;
    }

    if (signed_info->get_rank() == 4 && unsigned_info->get_rank() == 5) {
        return make_builtin_type(semantic_model, "unsigned long long");
    }
    if (signed_info->get_rank() == 3 && unsigned_info->get_rank() == 5) {
        return make_builtin_type(semantic_model, "unsigned long long");
    }
    if (signed_info->get_rank() == 2 && unsigned_info->get_rank() == 5) {
        return make_builtin_type(semantic_model, "unsigned long long");
    }
    if (signed_info->get_rank() == 1 && unsigned_info->get_rank() >= 3) {
        return unsigned_type;
    }

    if (unsigned_info->get_rank() > signed_info->get_rank()) {
        return unsigned_type;
    }

    return signed_type;
}

const SemanticType *IntegerConversionService::get_usual_arithmetic_conversion_type(
    const SemanticType *lhs, const SemanticType *rhs,
    SemanticModel &semantic_model) const {
    lhs = strip_qualifiers(lhs);
    rhs = strip_qualifiers(rhs);
    if (lhs == nullptr || rhs == nullptr) {
        return nullptr;
    }

    const auto lhs_float_rank = get_floating_type_rank(lhs);
    const auto rhs_float_rank = get_floating_type_rank(rhs);
    if (lhs_float_rank.has_value() || rhs_float_rank.has_value()) {
        const int lhs_rank = lhs_float_rank.value_or(0);
        const int rhs_rank = rhs_float_rank.value_or(0);
        if (lhs_rank >= rhs_rank) {
            if (lhs_rank == 1) {
                return make_builtin_type(semantic_model, "_Float16");
            }
            if (lhs_rank == 2) {
                return make_builtin_type(semantic_model, "float");
            }
            if (lhs_rank == 3) {
                return make_builtin_type(semantic_model, "double");
            }
            if (lhs_rank == 4) {
                return make_builtin_type(semantic_model, "long double");
            }
        }
        if (rhs_rank == 1) {
            return make_builtin_type(semantic_model, "_Float16");
        }
        if (rhs_rank == 2) {
            return make_builtin_type(semantic_model, "float");
        }
        if (rhs_rank == 3) {
            return make_builtin_type(semantic_model, "double");
        }
        if (rhs_rank == 4) {
            return make_builtin_type(semantic_model, "long double");
        }
        return nullptr;
    }

    return get_common_integer_type(lhs, rhs, semantic_model);
}

IntegerConversionPlan IntegerConversionService::get_integer_conversion_plan(
    const SemanticType *source_type, const SemanticType *target_type) const
    noexcept {
    const auto source_info = get_integer_type_info(source_type);
    const auto target_info = get_integer_type_info(target_type);
    if (!source_info.has_value() || !target_info.has_value()) {
        return {IntegerConversionKind::Unsupported, source_type, target_type};
    }

    if (source_info->get_bit_width() == target_info->get_bit_width()) {
        return {IntegerConversionKind::None, source_type, target_type};
    }

    if (source_info->get_bit_width() > target_info->get_bit_width()) {
        return {IntegerConversionKind::Truncate, source_type, target_type};
    }

    if (source_info->get_is_signed()) {
        return {IntegerConversionKind::SignExtend, source_type, target_type};
    }

    return {IntegerConversionKind::ZeroExtend, source_type, target_type};
}

} // namespace sysycc::detail
