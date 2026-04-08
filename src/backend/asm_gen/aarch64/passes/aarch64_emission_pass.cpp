#include "backend/asm_gen/aarch64/passes/aarch64_emission_pass.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_elf_object_writer_support.hpp"
#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

struct AsmPrintOptions {
    bool include_data_objects = true;
    bool include_functions = true;
    bool force_global_function_symbols = false;
};

const char *arch_directive(AArch64AsmArchProfile arch_profile) {
    switch (arch_profile) {
    case AArch64AsmArchProfile::Armv82AWithFp16:
        return ".arch armv8.2-a+fp16";
    case AArch64AsmArchProfile::Armv8A:
    default:
        return ".arch armv8-a";
    }
}

bool read_binary_file(const std::filesystem::path &file_path,
                      std::vector<std::uint8_t> &bytes) {
    std::ifstream ifs(file_path, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return true;
}

bool same_debug_location(const AArch64DebugLocation &lhs,
                         const AArch64DebugLocation &rhs) {
    return lhs.file_id == rhs.file_id && lhs.line == rhs.line &&
           lhs.column == rhs.column;
}

void append_rendered_instruction(std::ostringstream &output,
                                 const AArch64MachineInstr &instruction,
                                 const AArch64MachineFunction &function) {
    output << "  " << instruction.get_mnemonic();
    if (instruction.get_operands().empty()) {
        output << "\n";
        return;
    }

    const bool use_space_separated_operands = instruction.get_mnemonic() == ".loc";
    output << " ";
    for (std::size_t index = 0; index < instruction.get_operands().size(); ++index) {
        if (index > 0) {
            output << (use_space_separated_operands ? " " : ", ");
        }
        std::string rendered_operand =
            render_machine_operand_for_asm(instruction.get_operands()[index], function);
        if (instruction.get_mnemonic() == "mov") {
            rendered_operand = render_vector_move_operand(rendered_operand);
        }
        output << rendered_operand;
    }
    output << "\n";
}

std::string print_module_with_options(const AArch64AsmModule &asm_module,
                                      const AArch64MachineModule &machine_module,
                                      const AArch64ObjectModule &object_module,
                                      const AsmPrintOptions &options) {
    std::ostringstream output;
    output << arch_directive(asm_module.get_arch_profile()) << "\n";
    for (const AArch64DebugFileEntry &entry : object_module.get_debug_file_entries()) {
        output << ".file " << entry.index << " " << quote_asm_string(entry.path)
               << "\n";
    }

    bool emitted_anything = true;
    if (options.include_data_objects) {
        for (const AArch64DataObject &data_object : object_module.get_data_objects()) {
            if (emitted_anything) {
                output << "\n";
            }
            emitted_anything = true;
            output << section_name(data_object.get_section_kind()) << "\n";
            output << ".p2align " << data_object.get_align_log2() << "\n";
            if (data_object.get_is_global_symbol()) {
                output << ".globl " << data_object.get_symbol_name() << "\n";
            }
            output << data_object.get_symbol_name() << ":\n";
            for (const AArch64DataFragment &fragment : data_object.get_fragments()) {
                output << render_data_fragment_for_asm(fragment) << "\n";
            }
        }
    }
    if (options.include_functions) {
        for (const AArch64MachineFunction &function : machine_module.get_functions()) {
            if (emitted_anything) {
                output << "\n";
            }
            emitted_anything = true;
            output << section_name(function.get_section_kind()) << "\n";
            if (options.force_global_function_symbols || function.get_is_global_symbol()) {
                output << ".globl " << function.get_name() << "\n";
            }
            output << ".p2align 2\n";
            output << ".type " << function.get_name() << ", %function\n";
            for (const AArch64MachineBlock &block : function.get_blocks()) {
                output << block.get_label() << ":\n";
                std::optional<AArch64DebugLocation> last_debug_location;
                for (const AArch64MachineInstr &instruction : block.get_instructions()) {
                    if (instruction.get_debug_location().has_value() &&
                        (!last_debug_location.has_value() ||
                         !same_debug_location(*instruction.get_debug_location(),
                                              *last_debug_location))) {
                        output << "  .loc " << instruction.get_debug_location()->file_id
                               << " " << instruction.get_debug_location()->line << " "
                               << instruction.get_debug_location()->column << "\n";
                        last_debug_location = instruction.get_debug_location();
                    }
                    append_rendered_instruction(output, instruction, function);
                }
            }
            output << ".size " << function.get_name() << ", .-" << function.get_name()
                   << "\n";
        }
    }
    return output.str();
}

} // namespace

std::string AArch64EmissionPass::print_module(
    const AArch64AsmModule &asm_module,
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module) const {
    return print_module_with_options(asm_module, machine_module, object_module,
                                     AsmPrintOptions{});
}

std::unique_ptr<AsmResult>
AArch64EmissionPass::emit_asm_result(
    const AArch64AsmModule &asm_module,
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module) const {
    return std::make_unique<AsmResult>(AsmTargetKind::AArch64,
                                       print_module(asm_module, machine_module,
                                                    object_module));
}

std::unique_ptr<ObjectResult> AArch64EmissionPass::emit_object_result(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const BackendOptions &backend_options,
    const std::filesystem::path &object_file,
    DiagnosticEngine &diagnostic_engine) const {
    (void)backend_options;
    if (object_file.has_parent_path()) {
        std::filesystem::create_directories(object_file.parent_path());
    }

    if (!write_aarch64_elf_object(
            machine_module, object_module, object_file,
            AArch64ElfObjectWriterOptions{
                .force_defined_symbols_global = false},
            diagnostic_engine)) {
        return nullptr;
    }

    std::vector<std::uint8_t> object_bytes;
    if (!read_binary_file(object_file, object_bytes)) {
        diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                    "failed to read native AArch64 object output");
        return nullptr;
    }
    return std::make_unique<ObjectResult>(ObjectTargetKind::ElfAArch64,
                                          std::move(object_bytes));
}

} // namespace sysycc
