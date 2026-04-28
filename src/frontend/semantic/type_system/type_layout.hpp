#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>

#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"

namespace sysycc::detail {

inline const SemanticType *strip_layout_qualifiers(const SemanticType *type) {
    const SemanticType *current = type;
    while (current != nullptr &&
           current->get_kind() == SemanticTypeKind::Qualified) {
        current =
            static_cast<const QualifiedSemanticType *>(current)->get_base_type();
    }
    return current;
}

inline std::size_t align_layout_offset(std::size_t offset, std::size_t alignment) {
    if (alignment == 0 || offset % alignment == 0) {
        return offset;
    }
    return offset + (alignment - (offset % alignment));
}

inline std::size_t floor_layout_offset(std::size_t offset,
                                       std::size_t alignment) {
    if (alignment == 0) {
        return offset;
    }
    return offset - (offset % alignment);
}

inline std::size_t round_up_layout_bits_to_bytes(std::size_t bit_width) {
    return (bit_width + 7U) / 8U;
}

inline std::size_t choose_layout_bit_field_storage_bits(
    std::size_t available_bits, std::size_t required_bits) {
    if (available_bits <= 8U) {
        return available_bits;
    }
    if (available_bits <= 16U || required_bits <= 16U) {
        return std::min<std::size_t>(available_bits, 16U);
    }
    if (available_bits <= 32U || required_bits <= 32U) {
        return std::min<std::size_t>(available_bits, 32U);
    }
    return available_bits;
}

inline bool have_same_layout_unqualified_type(const SemanticType *lhs,
                                              const SemanticType *rhs) {
    lhs = strip_layout_qualifiers(lhs);
    rhs = strip_layout_qualifiers(rhs);
    if (lhs == nullptr || rhs == nullptr || lhs->get_kind() != rhs->get_kind()) {
        return false;
    }
    if (lhs->get_kind() != SemanticTypeKind::Builtin) {
        return lhs == rhs;
    }
    return static_cast<const BuiltinSemanticType *>(lhs)->get_name() ==
           static_cast<const BuiltinSemanticType *>(rhs)->get_name();
}

inline std::optional<std::size_t>
get_semantic_type_alignment(const SemanticType *type);

inline std::optional<std::size_t> get_semantic_type_size(const SemanticType *type);

inline bool is_flexible_array_member_type(const SemanticType *type) {
    type = strip_layout_qualifiers(type);
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Array) {
        return false;
    }
    const auto *array_type = static_cast<const ArraySemanticType *>(type);
    return array_type->get_dimensions().size() == 1 &&
           array_type->get_dimensions().front() == 0;
}

inline std::optional<std::size_t>
get_builtin_type_alignment(const BuiltinSemanticType *builtin_type) {
    if (builtin_type == nullptr) {
        return std::nullopt;
    }
    const std::string &name = builtin_type->get_name();
    if (name == "char" || name == "signed char" || name == "unsigned char") {
        return 1;
    }
    if (name == "short" || name == "unsigned short") {
        return 2;
    }
    if (name == "int" || name == "unsigned int" || name == "float") {
        return 4;
    }
    if (name == "_Float16") {
        return 2;
    }
    if (name == "double") {
        return 8;
    }
    if (name == "long int" || name == "unsigned long" ||
        name == "long long int" || name == "unsigned long long" ||
        name == "size_t" || name == "ptrdiff_t") {
        return 8;
    }
    if (name == "long double") {
        return 16;
    }
    return std::nullopt;
}

inline std::optional<std::size_t>
get_builtin_type_size(const BuiltinSemanticType *builtin_type) {
    if (builtin_type == nullptr) {
        return std::nullopt;
    }
    const std::string &name = builtin_type->get_name();
    if (name == "void") {
        return std::nullopt;
    }
    return get_builtin_type_alignment(builtin_type);
}

inline std::optional<std::size_t>
get_semantic_type_alignment(const SemanticType *type) {
    type = strip_layout_qualifiers(type);
    if (type == nullptr) {
        return std::nullopt;
    }

    switch (type->get_kind()) {
    case SemanticTypeKind::Builtin:
        return get_builtin_type_alignment(
            static_cast<const BuiltinSemanticType *>(type));
    case SemanticTypeKind::Qualified:
        return get_semantic_type_alignment(type);
    case SemanticTypeKind::Pointer:
        return std::size_t{8};
    case SemanticTypeKind::Array:
        return get_semantic_type_alignment(
            static_cast<const ArraySemanticType *>(type)->get_element_type());
    case SemanticTypeKind::Function:
        return std::nullopt;
    case SemanticTypeKind::Enum:
        return std::size_t{4};
    case SemanticTypeKind::Struct: {
        const auto *struct_type = static_cast<const StructSemanticType *>(type);
        if (struct_type->get_fields().empty()) {
            return std::nullopt;
        }
        std::size_t max_alignment = 1;
        for (const auto &field : struct_type->get_fields()) {
            const auto field_alignment = get_semantic_type_alignment(field.get_type());
            if (!field_alignment.has_value()) {
                return std::nullopt;
            }
            max_alignment = std::max(max_alignment, *field_alignment);
        }
        return max_alignment;
    }
    case SemanticTypeKind::Union: {
        const auto *union_type = static_cast<const UnionSemanticType *>(type);
        if (union_type->get_fields().empty()) {
            return std::nullopt;
        }
        std::size_t max_alignment = 1;
        for (const auto &field : union_type->get_fields()) {
            const auto field_alignment = get_semantic_type_alignment(field.get_type());
            if (!field_alignment.has_value()) {
                return std::nullopt;
            }
            max_alignment = std::max(max_alignment, *field_alignment);
        }
        return max_alignment;
    }
    }

    return std::nullopt;
}

inline std::optional<std::size_t> get_semantic_type_size(const SemanticType *type) {
    type = strip_layout_qualifiers(type);
    if (type == nullptr) {
        return std::nullopt;
    }

    switch (type->get_kind()) {
    case SemanticTypeKind::Builtin:
        return get_builtin_type_size(static_cast<const BuiltinSemanticType *>(type));
    case SemanticTypeKind::Qualified:
        return get_semantic_type_size(type);
    case SemanticTypeKind::Pointer:
        return std::size_t{8};
    case SemanticTypeKind::Array: {
        const auto *array_type = static_cast<const ArraySemanticType *>(type);
        const auto element_size = get_semantic_type_size(array_type->get_element_type());
        if (!element_size.has_value()) {
            return std::nullopt;
        }
        std::size_t total_size = *element_size;
        for (int dimension : array_type->get_dimensions()) {
            if (dimension <= 0) {
                return std::nullopt;
            }
            total_size *= static_cast<std::size_t>(dimension);
        }
        return total_size;
    }
    case SemanticTypeKind::Function:
        return std::nullopt;
    case SemanticTypeKind::Enum:
        return std::size_t{4};
    case SemanticTypeKind::Struct: {
        const auto *struct_type = static_cast<const StructSemanticType *>(type);
        if (struct_type->get_fields().empty()) {
            return std::nullopt;
        }
        IntegerConversionService conversion_service;
        std::size_t offset = 0;
        std::size_t max_alignment = 1;
        bool active_bit_field_unit = false;
        const SemanticType *active_storage_type = nullptr;
        std::size_t active_storage_bits = 0;
        std::size_t active_used_bits = 0;
        std::size_t active_storage_start_offset = 0;

        const auto &fields = struct_type->get_fields();
        for (std::size_t field_index = 0; field_index < fields.size();
             ++field_index) {
            const auto &field = fields[field_index];
            const SemanticType *field_type = strip_layout_qualifiers(field.get_type());
            const auto field_alignment = get_semantic_type_alignment(field_type);
            std::optional<std::size_t> field_size =
                get_semantic_type_size(field_type);
            if (!field_size.has_value() &&
                field_index + 1 == fields.size() &&
                is_flexible_array_member_type(field_type)) {
                field_size = 0U;
            }
            if (!field_alignment.has_value() || !field_size.has_value()) {
                return std::nullopt;
            }
            max_alignment = std::max(max_alignment, *field_alignment);

            if (field.get_is_bit_field()) {
                const auto integer_info =
                    conversion_service.get_integer_type_info(field_type);
                if (!integer_info.has_value()) {
                    return std::nullopt;
                }
                const std::size_t storage_bits =
                    static_cast<std::size_t>(integer_info->get_bit_width());
                const std::size_t bit_width = static_cast<std::size_t>(
                    field.get_bit_width().value_or(0));

                if (bit_width == 0) {
                    active_bit_field_unit = false;
                    active_storage_type = nullptr;
                    active_storage_bits = 0;
                    active_used_bits = 0;
                    active_storage_start_offset = 0;
                    offset = align_layout_offset(offset, *field_alignment);
                    continue;
                }

                const bool needs_new_unit =
                    !active_bit_field_unit ||
                    !have_same_layout_unqualified_type(active_storage_type,
                                                       field_type) ||
                    active_used_bits + bit_width > active_storage_bits;
                if (needs_new_unit) {
                    active_storage_start_offset =
                        floor_layout_offset(offset, *field_alignment);
                    if (offset == active_storage_start_offset ||
                        offset + round_up_layout_bits_to_bytes(bit_width) >
                            active_storage_start_offset + *field_size) {
                        offset = align_layout_offset(offset, *field_alignment);
                        active_storage_start_offset = offset;
                        active_storage_bits = storage_bits;
                    } else {
                        const std::size_t available_bits =
                            (active_storage_start_offset + *field_size - offset) *
                            8U;
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
                    active_storage_start_offset = 0;
                }
                continue;
            }

            active_bit_field_unit = false;
            active_storage_type = nullptr;
            active_storage_bits = 0;
            active_used_bits = 0;
            active_storage_start_offset = 0;

            offset = align_layout_offset(offset, *field_alignment);
            offset += *field_size;
        }

        return align_layout_offset(offset, max_alignment);
    }
    case SemanticTypeKind::Union: {
        const auto *union_type = static_cast<const UnionSemanticType *>(type);
        if (union_type->get_fields().empty()) {
            return std::nullopt;
        }
        std::size_t max_size = 0;
        std::size_t max_alignment = 1;
        for (const auto &field : union_type->get_fields()) {
            const auto field_alignment = get_semantic_type_alignment(field.get_type());
            std::optional<std::size_t> field_size =
                get_semantic_type_size(field.get_type());
            if (!field_size.has_value() &&
                is_flexible_array_member_type(field.get_type())) {
                field_size = 0U;
            }
            if (!field_alignment.has_value() || !field_size.has_value()) {
                return std::nullopt;
            }
            max_size = std::max(max_size, *field_size);
            max_alignment = std::max(max_alignment, *field_alignment);
        }
        return align_layout_offset(max_size, max_alignment);
    }
    }

    return std::nullopt;
}

} // namespace sysycc::detail
