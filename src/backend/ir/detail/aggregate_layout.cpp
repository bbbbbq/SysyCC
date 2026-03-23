#include "backend/ir/detail/aggregate_layout.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/type_system/integer_conversion_service.hpp"

namespace sysycc::detail {

namespace {

void append_padding(std::vector<AggregateLayoutElement> &elements,
                    std::size_t &current_offset, std::size_t padding_size) {
    if (padding_size == 0) {
        return;
    }
    elements.push_back(
        AggregateLayoutElement{AggregateLayoutElementKind::Padding, nullptr,
                               padding_size});
    current_offset += padding_size;
}

void align_offset(std::vector<AggregateLayoutElement> &elements,
                  std::size_t &current_offset, std::size_t alignment) {
    if (alignment == 0 || current_offset % alignment == 0) {
        return;
    }
    append_padding(elements, current_offset,
                   alignment - (current_offset % alignment));
}

bool have_same_unqualified_type(const SemanticType *lhs,
                                const SemanticType *rhs) {
    lhs = strip_qualifiers(lhs);
    rhs = strip_qualifiers(rhs);
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    if (lhs->get_kind() != rhs->get_kind()) {
        return false;
    }
    if (lhs->get_kind() != SemanticTypeKind::Builtin) {
        return lhs == rhs;
    }
    return static_cast<const BuiltinSemanticType *>(lhs)->get_name() ==
           static_cast<const BuiltinSemanticType *>(rhs)->get_name();
}

AggregateLayoutInfo compute_struct_layout(const StructSemanticType *struct_type) {
    AggregateLayoutInfo layout;
    if (struct_type == nullptr) {
        return layout;
    }

    layout.field_layouts.resize(struct_type->get_fields().size());
    IntegerConversionService integer_conversion_service;

    std::size_t current_offset = 0;
    std::size_t max_alignment = 1;
    bool active_bit_field_unit = false;
    const SemanticType *active_storage_type = nullptr;
    std::size_t active_storage_bits = 0;
    std::size_t active_used_bits = 0;
    std::size_t active_element_index = 0;

    for (std::size_t field_index = 0; field_index < struct_type->get_fields().size();
         ++field_index) {
        const auto &field = struct_type->get_fields()[field_index];
        const SemanticType *field_type = strip_qualifiers(field.get_type());
        const std::size_t field_alignment = get_type_alignment(field_type);
        const std::size_t field_size = get_type_size(field_type);
        max_alignment = std::max(max_alignment, field_alignment);

        if (field.get_is_bit_field()) {
            const auto integer_info =
                integer_conversion_service.get_integer_type_info(field_type);
            const std::size_t storage_bits =
                integer_info.has_value() ? integer_info->get_bit_width() : 0;
            const std::size_t bit_width =
                field.get_bit_width().has_value()
                    ? static_cast<std::size_t>(*field.get_bit_width())
                    : 0;

            if (!active_bit_field_unit || !have_same_unqualified_type(
                                              active_storage_type, field_type) ||
                active_used_bits + bit_width > active_storage_bits) {
                active_bit_field_unit = false;
                active_storage_type = nullptr;
                active_storage_bits = 0;
                active_used_bits = 0;
                align_offset(layout.elements, current_offset, field_alignment);
                active_element_index = layout.elements.size();
                layout.elements.push_back(
                    AggregateLayoutElement{AggregateLayoutElementKind::Direct,
                                           field_type, 0});
                current_offset += field_size;
                active_bit_field_unit = true;
                active_storage_type = field_type;
                active_storage_bits = storage_bits;
            }

            if (!field.get_name().empty()) {
                layout.field_layouts[field_index] = AggregateFieldLayout{
                    active_element_index, true, active_used_bits, bit_width,
                    field_type, field_type};
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

        align_offset(layout.elements, current_offset, field_alignment);
        const std::size_t element_index = layout.elements.size();
        layout.elements.push_back(
            AggregateLayoutElement{AggregateLayoutElementKind::Direct, field_type,
                                   0});
        current_offset += field_size;
        layout.field_layouts[field_index] = AggregateFieldLayout{
            element_index, false, 0, 0, field_type, field_type};
    }

    align_offset(layout.elements, current_offset, max_alignment);
    layout.size = current_offset;
    layout.alignment = max_alignment;
    return layout;
}

AggregateLayoutInfo compute_union_layout(const UnionSemanticType *union_type) {
    AggregateLayoutInfo layout;
    if (union_type == nullptr) {
        return layout;
    }

    layout.field_layouts.resize(union_type->get_fields().size());
    std::size_t max_size = 0;
    std::size_t max_alignment = 1;

    for (std::size_t field_index = 0; field_index < union_type->get_fields().size();
         ++field_index) {
        const auto &field = union_type->get_fields()[field_index];
        const SemanticType *field_type = strip_qualifiers(field.get_type());
        max_size = std::max(max_size, get_type_size(field_type));
        max_alignment = std::max(max_alignment, get_type_alignment(field_type));
        if (field.get_is_bit_field()) {
            layout.field_layouts[field_index] = AggregateFieldLayout{
                0, true, 0,
                field.get_bit_width().has_value()
                    ? static_cast<std::size_t>(*field.get_bit_width())
                    : 0,
                field_type, field_type};
        } else {
            layout.field_layouts[field_index] =
                AggregateFieldLayout{0, false, 0, 0, field_type, field_type};
        }
    }

    if (max_alignment > 0 && max_size % max_alignment != 0) {
        max_size += max_alignment - (max_size % max_alignment);
    }

    layout.size = max_size;
    layout.alignment = max_alignment;
    return layout;
}

} // namespace

const SemanticType *strip_qualifiers(const SemanticType *type) {
    const SemanticType *current = type;
    while (current != nullptr &&
           current->get_kind() == SemanticTypeKind::Qualified) {
        current =
            static_cast<const QualifiedSemanticType *>(current)->get_base_type();
    }
    return current;
}

AggregateLayoutInfo compute_aggregate_layout(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return {};
    }
    if (type->get_kind() == SemanticTypeKind::Struct) {
        return compute_struct_layout(static_cast<const StructSemanticType *>(type));
    }
    if (type->get_kind() == SemanticTypeKind::Union) {
        return compute_union_layout(static_cast<const UnionSemanticType *>(type));
    }
    return {};
}

std::optional<AggregateFieldLayout>
get_aggregate_field_layout(const SemanticType *owner_type,
                           std::size_t field_index) {
    const AggregateLayoutInfo layout = compute_aggregate_layout(owner_type);
    if (field_index >= layout.field_layouts.size()) {
        return std::nullopt;
    }
    return layout.field_layouts[field_index];
}

std::string get_llvm_type_name(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return "void";
    }

    if (type->get_kind() == SemanticTypeKind::Builtin) {
        const auto *builtin_type = static_cast<const BuiltinSemanticType *>(type);
        if (builtin_type->get_name() == "int" ||
            builtin_type->get_name() == "unsigned int") {
            return "i32";
        }
        if (builtin_type->get_name() == "ptrdiff_t") {
            return "i64";
        }
        if (builtin_type->get_name() == "char" ||
            builtin_type->get_name() == "signed char" ||
            builtin_type->get_name() == "unsigned char") {
            return "i8";
        }
        if (builtin_type->get_name() == "short" ||
            builtin_type->get_name() == "unsigned short") {
            return "i16";
        }
        if (builtin_type->get_name() == "long int" ||
            builtin_type->get_name() == "unsigned long" ||
            builtin_type->get_name() == "long long int" ||
            builtin_type->get_name() == "unsigned long long") {
            return "i64";
        }
        if (builtin_type->get_name() == "void") {
            return "void";
        }
        if (builtin_type->get_name() == "float") {
            return "float";
        }
        if (builtin_type->get_name() == "double") {
            return "double";
        }
        if (builtin_type->get_name() == "_Float16") {
            return "half";
        }
        if (builtin_type->get_name() == "long double") {
            return "fp128";
        }
    }

    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return "ptr";
    }

    if (type->get_kind() == SemanticTypeKind::Array) {
        const auto *array_type = static_cast<const ArraySemanticType *>(type);
        const auto &dimensions = array_type->get_dimensions();
        std::string element_type_name =
            get_llvm_type_name(array_type->get_element_type());
        for (auto it = dimensions.rbegin(); it != dimensions.rend(); ++it) {
            element_type_name = "[" + std::to_string(*it) + " x " +
                                element_type_name + "]";
        }
        return element_type_name;
    }

    if (type->get_kind() == SemanticTypeKind::Struct) {
        const AggregateLayoutInfo layout = compute_aggregate_layout(type);
        std::string result = "{ ";
        for (std::size_t index = 0; index < layout.elements.size(); ++index) {
            if (index > 0) {
                result += ", ";
            }
            const auto &element = layout.elements[index];
            if (element.kind == AggregateLayoutElementKind::Padding) {
                result += "[" + std::to_string(element.padding_size) + " x i8]";
            } else {
                result += get_llvm_type_name(element.type);
            }
        }
        result += " }";
        return result;
    }

    if (type->get_kind() == SemanticTypeKind::Union) {
        const auto *union_type = static_cast<const UnionSemanticType *>(type);
        for (const auto &field : union_type->get_fields()) {
            if (field.get_name().empty()) {
                continue;
            }
            return get_padded_storage_llvm_type_name(
                field.get_type(), get_type_size(type));
        }
        return "{}";
    }

    return "void";
}

std::string get_padded_storage_llvm_type_name(const SemanticType *type,
                                              std::size_t total_size) {
    const std::string base_type_name = get_llvm_type_name(type);
    const std::size_t base_size = get_type_size(type);
    if (base_size >= total_size) {
        return base_type_name;
    }

    return "{ " + base_type_name + ", [" +
           std::to_string(total_size - base_size) + " x i8] }";
}

std::size_t get_type_alignment(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return 1;
    }
    if (type->get_kind() == SemanticTypeKind::Builtin) {
        const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
        if (name == "char" || name == "signed char" || name == "unsigned char") {
            return 1;
        }
        if (name == "short" || name == "unsigned short" || name == "_Float16") {
            return 2;
        }
        if (name == "int" || name == "unsigned int" || name == "float") {
            return 4;
        }
        if (name == "double" || name == "ptrdiff_t" || name == "long int" ||
            name == "unsigned long" || name == "long long int" ||
            name == "unsigned long long") {
            return 8;
        }
        if (name == "long double") {
            return 16;
        }
    }
    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return 8;
    }
    if (type->get_kind() == SemanticTypeKind::Array) {
        return get_type_alignment(
            static_cast<const ArraySemanticType *>(type)->get_element_type());
    }
    if (type->get_kind() == SemanticTypeKind::Struct ||
        type->get_kind() == SemanticTypeKind::Union) {
        return compute_aggregate_layout(type).alignment;
    }
    return 1;
}

std::size_t get_type_size(const SemanticType *type) {
    type = strip_qualifiers(type);
    if (type == nullptr) {
        return 0;
    }
    if (type->get_kind() == SemanticTypeKind::Builtin) {
        const auto &name = static_cast<const BuiltinSemanticType *>(type)->get_name();
        if (name == "char" || name == "signed char" || name == "unsigned char") {
            return 1;
        }
        if (name == "short" || name == "unsigned short" || name == "_Float16") {
            return 2;
        }
        if (name == "int" || name == "unsigned int" || name == "float") {
            return 4;
        }
        if (name == "double" || name == "ptrdiff_t" || name == "long int" ||
            name == "unsigned long" || name == "long long int" ||
            name == "unsigned long long") {
            return 8;
        }
        if (name == "long double") {
            return 16;
        }
        return 0;
    }
    if (type->get_kind() == SemanticTypeKind::Pointer) {
        return 8;
    }
    if (type->get_kind() == SemanticTypeKind::Array) {
        const auto *array_type = static_cast<const ArraySemanticType *>(type);
        std::size_t total_size = get_type_size(array_type->get_element_type());
        for (const int dimension : array_type->get_dimensions()) {
            total_size *= static_cast<std::size_t>(dimension);
        }
        return total_size;
    }
    if (type->get_kind() == SemanticTypeKind::Struct ||
        type->get_kind() == SemanticTypeKind::Union) {
        return compute_aggregate_layout(type).size;
    }
    return 0;
}

} // namespace sysycc::detail
