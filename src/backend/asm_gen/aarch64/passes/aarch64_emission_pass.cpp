#include "backend/asm_gen/aarch64/passes/aarch64_emission_pass.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

std::string shell_quote(const std::string &text) {
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

std::optional<std::filesystem::path> find_executable_in_path(const std::string &name) {
    const char *path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return std::nullopt;
    }
    std::stringstream stream(path_env);
    std::string path_entry;
    while (std::getline(stream, path_entry, ':')) {
        if (path_entry.empty()) {
            continue;
        }
        const std::filesystem::path candidate =
            std::filesystem::path(path_entry) / name;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
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

bool assemble_aarch64_object(const std::filesystem::path &asm_file,
                             const std::filesystem::path &object_file,
                             bool emit_debug_info,
                             DiagnosticEngine &diagnostic_engine) {
    std::optional<std::filesystem::path> assembler =
        find_executable_in_path("aarch64-linux-gnu-as");
    std::string command;
    if (assembler.has_value()) {
        command = shell_quote(assembler->string()) + (emit_debug_info ? " -g -o " : " -o ") +
                  shell_quote(object_file.string()) + " " +
                  shell_quote(asm_file.string());
    } else {
        std::optional<std::filesystem::path> clang =
            find_executable_in_path("clang");
        if (!clang.has_value()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "failed to find an AArch64 assembler; looked for aarch64-linux-gnu-as and clang");
            return false;
        }
        command = shell_quote(clang->string()) +
                  " --target=aarch64-unknown-linux-gnu " +
                  (emit_debug_info ? "-g " : "") +
                  "-c -x assembler -o " + shell_quote(object_file.string()) + " " +
                  shell_quote(asm_file.string());
    }

    const int exit_code = std::system(command.c_str());
    if (exit_code != 0) {
        diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                    "failed to assemble native AArch64 object file");
        return false;
    }
    return true;
}

} // namespace

std::string AArch64EmissionPass::print_module(const AArch64MachineModule &module) const {
    std::ostringstream output;
    for (const std::string &line : module.get_preamble_lines()) {
        output << line << "\n";
    }
    for (const AArch64DebugFileEntry &entry : module.get_debug_file_entries()) {
        output << ".file " << entry.index << " " << quote_asm_string(entry.path)
               << "\n";
    }
    for (const AArch64DataObject &data_object : module.get_data_objects()) {
        if (!module.get_preamble_lines().empty() ||
            !module.get_debug_file_entries().empty() ||
            &data_object != &module.get_data_objects().front()) {
            output << "\n";
        }
        output << section_name(data_object.get_section_kind()) << "\n";
        output << ".p2align " << data_object.get_align_log2() << "\n";
        if (data_object.get_is_global_symbol()) {
            output << ".globl " << data_object.get_symbol_name() << "\n";
        }
        output << data_object.get_symbol_name() << ":\n";
        for (const AArch64DataFragment &fragment : data_object.get_fragments()) {
            output << fragment.get_text() << "\n";
        }
    }
    for (const AArch64MachineFunction &function : module.get_functions()) {
        if (!module.get_preamble_lines().empty() || !module.get_debug_file_entries().empty() ||
            !module.get_data_objects().empty() ||
            &function != &module.get_functions().front()) {
            output << "\n";
        }
        output << section_name(function.get_section_kind()) << "\n";
        if (function.get_is_global_symbol()) {
            output << ".globl " << function.get_name() << "\n";
        }
        output << ".p2align 2\n";
        output << ".type " << function.get_name() << ", %function\n";
        for (const AArch64MachineBlock &block : function.get_blocks()) {
            output << block.get_label() << ":\n";
            for (const AArch64MachineInstr &instruction : block.get_instructions()) {
                output << "  " << instruction.get_mnemonic();
                if (!instruction.get_operands().empty()) {
                    output << " ";
                    for (std::size_t index = 0;
                         index < instruction.get_operands().size(); ++index) {
                        if (index > 0) {
                            output << ", ";
                        }
                        std::string rendered_operand = substitute_virtual_registers(
                            instruction.get_operands()[index].get_text(), function);
                        if (instruction.get_mnemonic() == "mov") {
                            rendered_operand = render_vector_move_operand(rendered_operand);
                        }
                        output << rendered_operand;
                    }
                }
                output << "\n";
            }
        }
        output << ".size " << function.get_name() << ", .-" << function.get_name()
               << "\n";
    }
    return output.str();
}

std::unique_ptr<AsmResult>
AArch64EmissionPass::emit_asm_result(const AArch64MachineModule &module) const {
    return std::make_unique<AsmResult>(AsmTargetKind::AArch64, print_module(module));
}

std::unique_ptr<ObjectResult> AArch64EmissionPass::emit_object_result(
    const std::string &asm_text, const BackendOptions &backend_options,
    const std::filesystem::path &object_file,
    DiagnosticEngine &diagnostic_engine) const {
    if (object_file.has_parent_path()) {
        std::filesystem::create_directories(object_file.parent_path());
    }
    const std::filesystem::path temp_asm =
        object_file.parent_path().empty()
            ? std::filesystem::path(object_file.filename().string() + ".tmp.s")
            : object_file.parent_path() /
                  (object_file.filename().string() + ".tmp.s");
    {
        std::ofstream ofs(temp_asm);
        if (!ofs.is_open()) {
            diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                        "failed to open temporary asm output file");
            return nullptr;
        }
        ofs << asm_text;
    }
    if (!assemble_aarch64_object(temp_asm, object_file,
                                 backend_options.get_debug_info(),
                                 diagnostic_engine)) {
        std::error_code ignored_error;
        std::filesystem::remove(temp_asm, ignored_error);
        return nullptr;
    }
    std::error_code ignored_error;
    std::filesystem::remove(temp_asm, ignored_error);

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
