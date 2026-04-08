#pragma once

#include <cstddef>
#include <string>
#include <string_view>
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
std::string render_physical_register(unsigned reg_number, AArch64VirtualRegKind kind);
std::string render_physical_register(unsigned reg_number, bool use_64bit);
std::string render_symbol_reference_for_asm(const AArch64SymbolReference &reference);
std::string
render_symbol_reference_for_asm(const AArch64MachineSymbolReference &reference);
std::string stack_pointer_name(bool use_64bit);
std::string zero_register_name(bool use_64bit);
AArch64MachineOperand stack_pointer_operand(bool use_64bit = true);
AArch64MachineOperand zero_register_operand(bool use_64bit);
AArch64MachineOperand condition_code_operand(std::string_view condition);
AArch64MachineOperand shift_operand(std::string_view mnemonic, unsigned amount);
std::string fp_move_mnemonic(AArch64VirtualRegKind kind);
std::string use_vreg(const AArch64VirtualReg &reg);
std::string def_vreg(const AArch64VirtualReg &reg);
std::string use_vreg_as(const AArch64VirtualReg &reg, bool use_64bit);
std::string def_vreg_as(const AArch64VirtualReg &reg, bool use_64bit);
std::string use_vreg_as_kind(const AArch64VirtualReg &reg,
                             AArch64VirtualRegKind kind);
std::string def_vreg_as_kind(const AArch64VirtualReg &reg,
                             AArch64VirtualRegKind kind);
AArch64MachineOperand use_vreg_operand(const AArch64VirtualReg &reg);
AArch64MachineOperand def_vreg_operand(const AArch64VirtualReg &reg);
AArch64MachineOperand use_vreg_operand_as(const AArch64VirtualReg &reg, bool use_64bit);
AArch64MachineOperand def_vreg_operand_as(const AArch64VirtualReg &reg, bool use_64bit);
AArch64MachineOperand use_vreg_operand_as_kind(const AArch64VirtualReg &reg,
                                               AArch64VirtualRegKind kind);
AArch64MachineOperand def_vreg_operand_as_kind(const AArch64VirtualReg &reg,
                                               AArch64VirtualRegKind kind);
void append_register_copy(AArch64MachineBlock &machine_block,
                          const AArch64VirtualReg &dst_reg,
                          const AArch64VirtualReg &src_reg);
void append_copy_from_physical_reg(AArch64MachineBlock &machine_block,
                                   const AArch64VirtualReg &dst_reg,
                                   unsigned physical_reg,
                                   AArch64VirtualRegKind physical_kind);
void append_copy_to_physical_reg(AArch64MachineBlock &machine_block,
                                 unsigned physical_reg,
                                 AArch64VirtualRegKind physical_kind,
                                 const AArch64VirtualReg &src_reg);

struct ParsedVirtualRegRef {
    std::size_t id = 0;
    AArch64VirtualRegKind kind = AArch64VirtualRegKind::General32;
    bool is_def = false;
};

std::vector<ParsedVirtualRegRef>
collect_virtual_reg_refs(const AArch64MachineOperand &operand);
std::vector<std::size_t> collect_explicit_vreg_ids(
    const std::vector<AArch64MachineOperand> &operands, bool defs);
std::string render_data_fragment_for_asm(const AArch64DataFragment &fragment);
std::string render_machine_operand_for_asm(const AArch64MachineOperand &operand,
                                           const AArch64MachineFunction &function);
std::string render_vector_move_operand(const std::string &text);

} // namespace sysycc
