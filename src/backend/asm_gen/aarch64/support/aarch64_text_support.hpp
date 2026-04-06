#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_codegen_model.hpp"

namespace sysycc {

bool is_float_physical_reg(unsigned reg);
unsigned dwarf_register_number(unsigned physical_reg);
const char *section_name(AArch64SectionKind section_kind);
std::string quote_asm_string(const std::string &text);
bool uses_general_64bit_register(AArch64VirtualRegKind kind);
char virtual_reg_suffix(AArch64VirtualRegKind kind);
AArch64VirtualRegKind virtual_reg_kind_from_suffix(char suffix);
std::pair<std::string, std::vector<AArch64MachineOperand>>
parse_machine_instruction_text(std::string text);
std::string render_physical_register(unsigned reg_number, AArch64VirtualRegKind kind);
std::string render_physical_register(unsigned reg_number, bool use_64bit);
std::string zero_register_name(bool use_64bit);
std::string fp_move_mnemonic(AArch64VirtualRegKind kind);
std::string use_vreg(const AArch64VirtualReg &reg);
std::string def_vreg(const AArch64VirtualReg &reg);
std::string use_vreg_as(const AArch64VirtualReg &reg, bool use_64bit);
std::string def_vreg_as(const AArch64VirtualReg &reg, bool use_64bit);
std::string use_vreg_as_kind(const AArch64VirtualReg &reg,
                             AArch64VirtualRegKind kind);
std::string def_vreg_as_kind(const AArch64VirtualReg &reg,
                             AArch64VirtualRegKind kind);
void append_register_copy(AArch64MachineBlock &machine_block,
                          const AArch64VirtualReg &dst_reg,
                          const AArch64VirtualReg &src_reg);

struct ParsedVirtualRegRef {
    std::size_t id = 0;
    AArch64VirtualRegKind kind = AArch64VirtualRegKind::General32;
    bool is_def = false;
    std::size_t offset = 0;
    std::size_t length = 0;
};

std::vector<ParsedVirtualRegRef> parse_virtual_reg_refs(const std::string &text);
std::vector<std::size_t> collect_explicit_vreg_ids(
    const std::vector<AArch64MachineOperand> &operands, bool defs);
std::string substitute_virtual_registers(const std::string &text,
                                         const AArch64MachineFunction &function);
std::string render_vector_move_operand(const std::string &text);

} // namespace sysycc
