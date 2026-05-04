#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_elf_object_writer_internal.hpp"

namespace sysycc {

class DiagnosticEngine;

bool build_eh_frame_section_image(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    std::vector<SectionImage> &sections);

bool build_debug_line_section_image(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    std::vector<SectionImage> &sections);

bool build_debug_info_section_images(
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    std::vector<SectionImage> &sections);

} // namespace sysycc
