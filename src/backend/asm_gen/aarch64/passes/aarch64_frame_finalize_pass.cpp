#include "backend/asm_gen/aarch64/passes/aarch64_frame_finalize_pass.hpp"

#include <string>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_frame_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"

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

void allocate_saved_physical_reg_slots(AArch64MachineFunction &function) {
    std::size_t local_size = function.get_frame_info().get_local_size();
    for (unsigned reg : function.get_frame_info().get_saved_physical_regs()) {
        if (function.get_frame_info().has_saved_physical_reg_offset(reg)) {
            continue;
        }
        local_size = align_to(local_size, 8);
        local_size += 8;
        function.get_frame_info().set_saved_physical_reg_offset(reg, local_size);
    }
    function.get_frame_info().set_local_size(local_size);
    function.get_frame_info().set_frame_size(align_to(local_size, 16));
}

void append_saved_reg_store(std::vector<AArch64MachineInstr> &instructions,
                            unsigned physical_reg, std::size_t offset) {
    append_physical_frame_store(
        instructions, physical_reg,
        is_float_physical_reg(physical_reg) ? AArch64VirtualRegKind::Float64
                                            : AArch64VirtualRegKind::General64,
        8, offset, static_cast<unsigned>(AArch64PhysicalReg::X9));
}

void append_saved_reg_load(std::vector<AArch64MachineInstr> &instructions,
                           unsigned physical_reg, std::size_t offset) {
    append_physical_frame_load(
        instructions, physical_reg,
        is_float_physical_reg(physical_reg) ? AArch64VirtualRegKind::Float64
                                            : AArch64VirtualRegKind::General64,
        8, offset, static_cast<unsigned>(AArch64PhysicalReg::X9));
}

} // namespace

void AArch64FrameFinalizePass::run(AArch64MachineFunction &function) const {
    if (function.get_blocks().empty()) {
        return;
    }
    allocate_saved_physical_reg_slots(function);

    const std::vector<AArch64MachineInstr> existing_prologue =
        function.get_blocks().front().get_instructions();
    std::size_t prologue_prefix_size = 0;
    if (existing_prologue.size() >= 2 &&
        existing_prologue[0].get_mnemonic() == "stp" &&
        existing_prologue[1].get_mnemonic() == "mov") {
        prologue_prefix_size = 2;
        if (existing_prologue.size() >= 3 &&
            existing_prologue[2].get_mnemonic() == "sub") {
            prologue_prefix_size = 3;
        }
    }

    std::vector<AArch64MachineInstr> prologue;
    prologue.emplace_back(".cfi_startproc");
    prologue.emplace_back("stp x29, x30, [sp, #-16]!");
    prologue.emplace_back(".cfi_def_cfa_offset 16");
    prologue.emplace_back(".cfi_offset 29, -16");
    prologue.emplace_back(".cfi_offset 30, -8");
    prologue.emplace_back("mov x29, sp");
    prologue.emplace_back(".cfi_def_cfa_register 29");
    if (function.get_frame_info().get_frame_size() > 0) {
        prologue.emplace_back("sub sp, sp, #" +
                              std::to_string(function.get_frame_info().get_frame_size()));
        prologue.emplace_back(".cfi_def_cfa_offset " +
                              std::to_string(function.get_frame_info().get_frame_size() + 16));
    }
    for (unsigned reg : function.get_frame_info().get_saved_physical_regs()) {
        append_saved_reg_store(prologue, reg,
                               function.get_frame_info().get_saved_physical_reg_offset(reg));
        prologue.emplace_back(".cfi_offset " +
                              std::to_string(dwarf_register_number(reg)) + ", -" +
                              std::to_string(16 + function.get_frame_info()
                                                     .get_saved_physical_reg_offset(reg)));
    }
    for (std::size_t index = prologue_prefix_size; index < existing_prologue.size();
         ++index) {
        prologue.push_back(existing_prologue[index]);
    }
    function.get_blocks().front().get_instructions() = std::move(prologue);

    std::vector<AArch64MachineInstr> epilogue;
    for (auto it = function.get_frame_info().get_saved_physical_regs().rbegin();
         it != function.get_frame_info().get_saved_physical_regs().rend(); ++it) {
        append_saved_reg_load(epilogue, *it,
                              function.get_frame_info().get_saved_physical_reg_offset(*it));
        epilogue.emplace_back(".cfi_restore " +
                              std::to_string(dwarf_register_number(*it)));
    }
    if (function.get_frame_info().get_frame_size() > 0) {
        epilogue.emplace_back("add sp, sp, #" +
                              std::to_string(function.get_frame_info().get_frame_size()));
        epilogue.emplace_back(".cfi_def_cfa_offset 16");
    }
    epilogue.emplace_back("ldp x29, x30, [sp], #16");
    epilogue.emplace_back(".cfi_restore 29");
    epilogue.emplace_back(".cfi_restore 30");
    epilogue.emplace_back(".cfi_def_cfa sp, 0");
    epilogue.emplace_back("ret");
    epilogue.emplace_back(".cfi_endproc");
    function.get_blocks().back().get_instructions() = std::move(epilogue);
}

} // namespace sysycc
