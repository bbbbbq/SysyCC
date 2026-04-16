#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_type_support.hpp"

#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_parse_common_support.hpp"

namespace sysycc {

namespace {

std::optional<AArch64LlvmImportType>
parse_llvm_import_type_impl(const std::string &text) {
    const std::string normalized = llvm_import_trim_copy(text);
    if (normalized.empty()) {
        return std::nullopt;
    }

    AArch64LlvmImportType type;
    if (normalized == "void") {
        type.kind = AArch64LlvmImportTypeKind::Void;
        return type;
    }
    if (llvm_import_starts_with(normalized, "ptr")) {
        const std::string remainder =
            llvm_import_trim_copy(normalized.substr(3));
        type.kind = AArch64LlvmImportTypeKind::Pointer;
        if (remainder.empty()) {
            return type;
        }
        if (!llvm_import_starts_with(remainder, "addrspace(") ||
            remainder.back() != ')') {
            return std::nullopt;
        }
        try {
            type.pointer_address_space = static_cast<std::size_t>(
                std::stoull(remainder.substr(10, remainder.size() - 11)));
            return type;
        } catch (...) {
            return std::nullopt;
        }
    }
    if (normalized == "half") {
        type.kind = AArch64LlvmImportTypeKind::Float16;
        return type;
    }
    if (normalized == "float") {
        type.kind = AArch64LlvmImportTypeKind::Float32;
        return type;
    }
    if (normalized == "double") {
        type.kind = AArch64LlvmImportTypeKind::Float64;
        return type;
    }
    if (normalized == "fp128") {
        type.kind = AArch64LlvmImportTypeKind::Float128;
        return type;
    }
    if (normalized.size() > 1 && normalized.front() == 'i') {
        try {
            type.kind = AArch64LlvmImportTypeKind::Integer;
            type.integer_bit_width =
                static_cast<std::size_t>(std::stoull(normalized.substr(1)));
            return type;
        } catch (...) {
            return std::nullopt;
        }
    }
    if (normalized.front() == '%') {
        type.kind = AArch64LlvmImportTypeKind::Named;
        type.named_type_name = normalized.substr(1);
        return type;
    }
    if (normalized.front() == '<' && normalized.size() >= 4 &&
        normalized[1] == '{' && normalized[normalized.size() - 2] == '}' &&
        normalized.back() == '>') {
        const std::string inner = llvm_import_trim_copy(
            normalized.substr(2, normalized.size() - 4));
        type.kind = AArch64LlvmImportTypeKind::Struct;
        type.struct_is_packed = true;
        for (const std::string &element_text :
             llvm_import_split_top_level(inner, ',')) {
            if (element_text.empty()) {
                continue;
            }
            const auto element_type =
                parse_llvm_import_type_impl(llvm_import_trim_copy(element_text));
            if (!element_type.has_value()) {
                return std::nullopt;
            }
            type.element_types.push_back(*element_type);
        }
        return type;
    }
    if ((normalized.front() == '[' || normalized.front() == '<') &&
        normalized.back() == (normalized.front() == '[' ? ']' : '>')) {
        const bool is_vector_syntax = normalized.front() == '<';
        const std::string inner = llvm_import_trim_copy(
            normalized.substr(1, normalized.size() - 2));
        const std::size_t x_pos = inner.find('x');
        if (x_pos == std::string::npos) {
            return std::nullopt;
        }
        std::size_t element_count = 0;
        try {
            element_count = static_cast<std::size_t>(
                std::stoull(llvm_import_trim_copy(inner.substr(0, x_pos))));
        } catch (...) {
            return std::nullopt;
        }
        const auto element_type =
            parse_llvm_import_type_impl(llvm_import_trim_copy(inner.substr(x_pos + 1)));
        if (!element_type.has_value()) {
            return std::nullopt;
        }
        type.kind = AArch64LlvmImportTypeKind::Array;
        type.array_element_count = element_count;
        type.array_uses_vector_syntax = is_vector_syntax;
        type.element_types.push_back(*element_type);
        return type;
    }
    if (normalized.front() == '{' && normalized.back() == '}') {
        const std::string inner = llvm_import_trim_copy(
            normalized.substr(1, normalized.size() - 2));
        type.kind = AArch64LlvmImportTypeKind::Struct;
        type.struct_is_packed = false;
        for (const std::string &element_text :
             llvm_import_split_top_level(inner, ',')) {
            if (element_text.empty()) {
                continue;
            }
            const auto element_type =
                parse_llvm_import_type_impl(llvm_import_trim_copy(element_text));
            if (!element_type.has_value()) {
                return std::nullopt;
            }
            type.element_types.push_back(*element_type);
        }
        return type;
    }

    return std::nullopt;
}

} // namespace

std::optional<AArch64LlvmImportType>
parse_llvm_import_type_text(const std::string &text) {
    return parse_llvm_import_type_impl(text);
}

} // namespace sysycc
