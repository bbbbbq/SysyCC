#pragma once

#include <optional>
#include <string>
#include <utility>

#include "backend/asm_gen/aarch64/model/aarch64_meta_model.hpp"

namespace sysycc {

enum class AArch64SymbolKind : unsigned char {
    Function,
    Object,
    Helper,
};

enum class AArch64SymbolBinding : unsigned char {
    Unknown,
    Local,
    Global,
};

struct AArch64SymbolDescriptor {
    std::string name;
    AArch64SymbolKind kind = AArch64SymbolKind::Object;
    std::optional<AArch64SectionKind> section_kind;
    AArch64SymbolBinding binding = AArch64SymbolBinding::Unknown;
    bool is_defined = false;

    static AArch64SymbolDescriptor named(
        std::string name, AArch64SymbolKind kind = AArch64SymbolKind::Object,
        AArch64SymbolBinding binding = AArch64SymbolBinding::Unknown,
        std::optional<AArch64SectionKind> section_kind = std::nullopt,
        bool is_defined = false) {
        return AArch64SymbolDescriptor{
            std::move(name), kind, section_kind, binding, is_defined};
    }
};

struct AArch64SymbolReference {
    AArch64SymbolDescriptor symbol;
    long long addend = 0;

    static AArch64SymbolReference direct(
        std::string name, AArch64SymbolKind kind = AArch64SymbolKind::Object,
        AArch64SymbolBinding binding = AArch64SymbolBinding::Unknown,
        std::optional<AArch64SectionKind> section_kind = std::nullopt,
        long long addend = 0, bool is_defined = false) {
        return AArch64SymbolReference{
            AArch64SymbolDescriptor::named(std::move(name), kind, binding,
                                           section_kind, is_defined),
            addend};
    }

    static AArch64SymbolReference from_descriptor(
        AArch64SymbolDescriptor descriptor, long long addend = 0) {
        return AArch64SymbolReference{std::move(descriptor), addend};
    }

    const std::string &get_name() const noexcept { return symbol.name; }
};

} // namespace sysycc
