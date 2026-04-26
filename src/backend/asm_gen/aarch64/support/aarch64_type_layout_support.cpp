#include "backend/asm_gen/aarch64/support/aarch64_type_layout_support.hpp"

#include <algorithm>

#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

std::size_t align_to(std::size_t value, std::size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

} // namespace

bool is_integer_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Integer;
}

bool is_pointer_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Pointer;
}

bool is_float_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Float;
}

bool is_vector_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Vector;
}

bool is_i32x4_vector_type(const CoreIrType *type) {
    const auto *vector_type = dynamic_cast<const CoreIrVectorType *>(type);
    const auto *element_type =
        vector_type == nullptr ? nullptr
                               : as_integer_type(vector_type->get_element_type());
    return element_type != nullptr && element_type->get_bit_width() == 32 &&
           vector_type->get_element_count() == 4;
}

bool is_void_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Void;
}

const CoreIrIntegerType *as_integer_type(const CoreIrType *type) {
    if (!is_integer_type(type)) {
        return nullptr;
    }
    return static_cast<const CoreIrIntegerType *>(type);
}

const CoreIrFloatType *as_float_type(const CoreIrType *type) {
    if (!is_float_type(type)) {
        return nullptr;
    }
    return static_cast<const CoreIrFloatType *>(type);
}

bool is_aggregate_type(const CoreIrType *type) {
    return type != nullptr &&
           (type->get_kind() == CoreIrTypeKind::Array ||
            type->get_kind() == CoreIrTypeKind::Struct);
}

bool type_contains_float_kind(const CoreIrType *type, CoreIrFloatKind float_kind) {
    if (const auto *float_type = as_float_type(type); float_type != nullptr) {
        return float_type->get_float_kind() == float_kind;
    }
    if (const auto *pointer_type = dynamic_cast<const CoreIrPointerType *>(type);
        pointer_type != nullptr) {
        return type_contains_float_kind(pointer_type->get_pointee_type(), float_kind);
    }
    if (const auto *vector_type = dynamic_cast<const CoreIrVectorType *>(type);
        vector_type != nullptr) {
        return type_contains_float_kind(vector_type->get_element_type(), float_kind);
    }
    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
        array_type != nullptr) {
        return type_contains_float_kind(array_type->get_element_type(), float_kind);
    }
    if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
        struct_type != nullptr) {
        for (const CoreIrType *element_type : struct_type->get_element_types()) {
            if (type_contains_float_kind(element_type, float_kind)) {
                return true;
            }
        }
    }
    if (const auto *function_type = dynamic_cast<const CoreIrFunctionType *>(type);
        function_type != nullptr) {
        if (type_contains_float_kind(function_type->get_return_type(), float_kind)) {
            return true;
        }
        for (const CoreIrType *parameter_type : function_type->get_parameter_types()) {
            if (type_contains_float_kind(parameter_type, float_kind)) {
                return true;
            }
        }
    }
    return false;
}

AArch64VirtualRegKind classify_float_reg_kind(CoreIrFloatKind float_kind) {
    switch (float_kind) {
    case CoreIrFloatKind::Float16:
        return AArch64VirtualRegKind::Float16;
    case CoreIrFloatKind::Float32:
        return AArch64VirtualRegKind::Float32;
    case CoreIrFloatKind::Float64:
        return AArch64VirtualRegKind::Float64;
    case CoreIrFloatKind::Float128:
        return AArch64VirtualRegKind::Float128;
    }
    return AArch64VirtualRegKind::Float32;
}

AArch64VirtualRegKind classify_virtual_reg_kind(const CoreIrType *type) {
    if (is_pointer_type(type)) {
        return AArch64VirtualRegKind::General64;
    }
    if (is_vector_type(type)) {
        return is_i32x4_vector_type(type) ? AArch64VirtualRegKind::Float128
                                          : AArch64VirtualRegKind::General64;
    }
    if (const auto *integer_type = as_integer_type(type); integer_type != nullptr) {
        return integer_type->get_bit_width() > 32 ? AArch64VirtualRegKind::General64
                                                  : AArch64VirtualRegKind::General32;
    }
    if (const auto *float_type = as_float_type(type); float_type != nullptr) {
        return classify_float_reg_kind(float_type->get_float_kind());
    }
    return AArch64VirtualRegKind::General32;
}

bool uses_general_64bit_register(const CoreIrType *type) {
    return classify_virtual_reg_kind(type) == AArch64VirtualRegKind::General64;
}

bool uses_64bit_register(const CoreIrType *type) {
    return uses_general_64bit_register(type);
}

std::size_t virtual_reg_size(AArch64VirtualRegKind kind) {
    switch (kind) {
    case AArch64VirtualRegKind::General32:
    case AArch64VirtualRegKind::Float32:
        return 4;
    case AArch64VirtualRegKind::General64:
    case AArch64VirtualRegKind::Float64:
        return 8;
    case AArch64VirtualRegKind::Float16:
        return 2;
    case AArch64VirtualRegKind::Float128:
        return 16;
    }
    return 4;
}

bool is_live_across_call_callee_saved_capable(AArch64VirtualRegKind kind) {
    return kind != AArch64VirtualRegKind::Float128;
}

bool is_narrow_integer_type(const CoreIrType *type) {
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return false;
    }
    const std::size_t bit_width = integer_type->get_bit_width();
    return bit_width == 1 || bit_width == 8 || bit_width == 16;
}

bool is_supported_scalar_storage_type(const CoreIrType *type) {
    if (is_i32x4_vector_type(type)) {
        return true;
    }
    if (is_pointer_type(type)) {
        return true;
    }
    if (const auto *float_type = as_float_type(type); float_type != nullptr) {
        switch (float_type->get_float_kind()) {
        case CoreIrFloatKind::Float16:
        case CoreIrFloatKind::Float32:
        case CoreIrFloatKind::Float64:
        case CoreIrFloatKind::Float128:
            return true;
        }
    }
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return false;
    }
    const std::size_t bit_width = integer_type->get_bit_width();
    return bit_width >= 1 && bit_width <= 64;
}

bool is_supported_object_type(const CoreIrType *type) {
    if (type == nullptr) {
        return false;
    }
    if (is_vector_type(type)) {
        return is_i32x4_vector_type(type);
    }
    if (is_supported_scalar_storage_type(type)) {
        return true;
    }
    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
        array_type != nullptr) {
        return is_supported_object_type(array_type->get_element_type());
    }
    if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
        struct_type != nullptr) {
        for (const CoreIrType *element_type : struct_type->get_element_types()) {
            if (!is_supported_object_type(element_type)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool is_supported_value_type(const CoreIrType *type) {
    return is_void_type(type) || is_supported_scalar_storage_type(type);
}

std::size_t get_storage_size(const CoreIrType *type) {
    if (is_pointer_type(type)) {
        return 8;
    }
    if (const auto *vector_type = dynamic_cast<const CoreIrVectorType *>(type);
        vector_type != nullptr) {
        return get_storage_size(vector_type->get_element_type()) *
               vector_type->get_element_count();
    }
    if (const auto *float_type = as_float_type(type); float_type != nullptr) {
        switch (float_type->get_float_kind()) {
        case CoreIrFloatKind::Float16:
            return 2;
        case CoreIrFloatKind::Float32:
            return 4;
        case CoreIrFloatKind::Float64:
            return 8;
        case CoreIrFloatKind::Float128:
            return 16;
        }
    }
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return 0;
    }
    if (integer_type->get_bit_width() == 1 || integer_type->get_bit_width() == 8) {
        return 1;
    }
    if (integer_type->get_bit_width() == 16) {
        return 2;
    }
    if (integer_type->get_bit_width() <= 32) {
        return 4;
    }
    return 8;
}

std::size_t get_storage_alignment(const CoreIrType *type) {
    if (const auto *vector_type = dynamic_cast<const CoreIrVectorType *>(type);
        vector_type != nullptr) {
        return std::max<std::size_t>(get_storage_alignment(vector_type->get_element_type()),
                                     get_storage_size(type));
    }
    const std::size_t size = get_storage_size(type);
    if (size == 0) {
        return 0;
    }
    return std::min<std::size_t>(8, size);
}

std::size_t get_type_size(const CoreIrType *type) {
    if (type == nullptr) {
        return 0;
    }
    if (is_supported_scalar_storage_type(type) || is_i32x4_vector_type(type)) {
        return get_storage_size(type);
    }
    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
        array_type != nullptr) {
        return get_type_size(array_type->get_element_type()) *
               array_type->get_element_count();
    }
    if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
        struct_type != nullptr) {
        std::size_t offset = 0;
        std::size_t max_alignment = 1;
        for (const CoreIrType *element_type : struct_type->get_element_types()) {
            const std::size_t alignment = get_type_alignment(element_type);
            max_alignment = std::max(max_alignment, alignment);
            if (!struct_type->get_is_packed()) {
                offset = align_to(offset, alignment);
            }
            offset += get_type_size(element_type);
        }
        return struct_type->get_is_packed() ? offset
                                            : align_to(offset, max_alignment);
    }
    return 0;
}

std::size_t get_type_alignment(const CoreIrType *type) {
    if (type == nullptr) {
        return 0;
    }
    if (is_supported_scalar_storage_type(type) || is_i32x4_vector_type(type)) {
        return get_storage_alignment(type);
    }
    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
        array_type != nullptr) {
        return get_type_alignment(array_type->get_element_type());
    }
    if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
        struct_type != nullptr) {
        if (struct_type->get_is_packed()) {
            return 1;
        }
        std::size_t max_alignment = 1;
        for (const CoreIrType *element_type : struct_type->get_element_types()) {
            max_alignment = std::max(max_alignment, get_type_alignment(element_type));
        }
        return max_alignment;
    }
    return 0;
}

std::size_t get_struct_member_offset(const CoreIrStructType *struct_type,
                                     std::size_t index) {
    if (struct_type == nullptr || index >= struct_type->get_element_types().size()) {
        return 0;
    }
    std::size_t offset = 0;
    for (std::size_t current = 0; current < index; ++current) {
        const CoreIrType *element_type = struct_type->get_element_types()[current];
        if (!struct_type->get_is_packed()) {
            offset = align_to(offset, get_type_alignment(element_type));
        }
        offset += get_type_size(element_type);
    }
    return struct_type->get_is_packed()
               ? offset
               : align_to(offset,
                          get_type_alignment(struct_type->get_element_types()[index]));
}

} // namespace sysycc
