#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace sysycc {

class SemanticType;

namespace detail {

enum class AggregateLayoutElementKind : unsigned char {
    Direct,
    Padding,
};

struct AggregateLayoutElement {
    AggregateLayoutElementKind kind = AggregateLayoutElementKind::Direct;
    const SemanticType *type = nullptr;
    std::size_t padding_size = 0;
};

struct AggregateFieldLayout {
    std::size_t llvm_element_index = 0;
    bool is_bit_field = false;
    std::size_t bit_offset = 0;
    std::size_t bit_width = 0;
    const SemanticType *storage_type = nullptr;
    const SemanticType *field_type = nullptr;
};

struct AggregateLayoutInfo {
    std::vector<AggregateLayoutElement> elements;
    std::vector<std::optional<AggregateFieldLayout>> field_layouts;
    std::size_t size = 0;
    std::size_t alignment = 1;
};

const SemanticType *strip_qualifiers(const SemanticType *type);

std::string get_llvm_type_name(const SemanticType *type);

std::size_t get_type_alignment(const SemanticType *type);

std::size_t get_type_size(const SemanticType *type);

AggregateLayoutInfo compute_aggregate_layout(const SemanticType *type);

std::optional<AggregateFieldLayout>
get_aggregate_field_layout(const SemanticType *owner_type,
                           std::size_t field_index);

} // namespace detail
} // namespace sysycc
