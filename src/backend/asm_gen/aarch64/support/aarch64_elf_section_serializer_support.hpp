#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_elf_object_writer_internal.hpp"

namespace sysycc {

class DiagnosticEngine;

bool write_aarch64_elf_sectioned_object(
    const std::filesystem::path &object_file,
    const std::vector<SectionImage> &section_images,
    std::vector<SymbolEntry> symbols,
    const std::unordered_map<std::string, std::uint32_t> &symbol_indices,
    DiagnosticEngine &diagnostic_engine, const char *error_prefix);

} // namespace sysycc
