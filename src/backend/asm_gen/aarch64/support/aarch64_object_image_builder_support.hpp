#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_elf_object_writer_internal.hpp"

namespace sysycc {

class DiagnosticEngine;

bool build_data_object_section_images(
    const AArch64ObjectModule &object_module, std::vector<SectionImage> &sections,
    std::unordered_map<std::string, DefinedSymbol> &defined_symbols,
    DiagnosticEngine &diagnostic_engine);

bool build_full_symbol_entries(
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, DefinedSymbol> &defined_symbols,
    const std::vector<SectionImage> &sections,
    bool force_defined_symbols_global, std::vector<SymbolEntry> &symbols,
    std::unordered_map<std::string, std::uint32_t> &symbol_indices);

} // namespace sysycc
