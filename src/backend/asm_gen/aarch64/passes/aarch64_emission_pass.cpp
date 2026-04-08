#include "backend/asm_gen/aarch64/passes/aarch64_emission_pass.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
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

std::filesystem::path make_temp_output_path(const std::filesystem::path &output_file,
                                            const char *suffix) {
    const std::string file_name =
        output_file.filename().string() + std::string(suffix);
    if (output_file.parent_path().empty()) {
        return std::filesystem::path(file_name);
    }
    return output_file.parent_path() / file_name;
}

void remove_file_if_present(const std::filesystem::path &file_path) {
    std::error_code ignored_error;
    std::filesystem::remove(file_path, ignored_error);
}

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

bool write_text_file(const std::filesystem::path &file_path, const std::string &text,
                     DiagnosticEngine &diagnostic_engine, const char *error_message) {
    std::ofstream ofs(file_path);
    if (!ofs.is_open()) {
        diagnostic_engine.add_error(DiagnosticStage::Compiler, error_message);
        return false;
    }
    ofs << text;
    return ofs.good();
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

bool merge_relocatable_objects(const std::filesystem::path &output_file,
                               const std::vector<std::filesystem::path> &input_files,
                               DiagnosticEngine &diagnostic_engine) {
    std::optional<std::filesystem::path> linker =
        find_executable_in_path("aarch64-linux-gnu-ld");
    bool needs_explicit_emulation = false;
    if (!linker.has_value()) {
        linker = find_executable_in_path("ld.lld");
        needs_explicit_emulation = linker.has_value();
    }
    if (!linker.has_value()) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "failed to find an AArch64 relocatable linker; looked for aarch64-linux-gnu-ld and ld.lld");
        return false;
    }

    std::string command = shell_quote(linker->string());
    if (needs_explicit_emulation) {
        command += " -m aarch64elf";
    }
    command += " -r -o " + shell_quote(output_file.string());
    for (const std::filesystem::path &input_file : input_files) {
        command += " " + shell_quote(input_file.string());
    }

    if (std::system(command.c_str()) != 0) {
        diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                    "failed to merge native AArch64 relocatable objects");
        return false;
    }
    return true;
}

bool localize_symbols_in_object(const std::filesystem::path &object_file,
                                const std::vector<std::string> &symbol_names,
                                DiagnosticEngine &diagnostic_engine) {
    if (symbol_names.empty()) {
        return true;
    }

    std::optional<std::filesystem::path> objcopy =
        find_executable_in_path("aarch64-linux-gnu-objcopy");
    if (!objcopy.has_value()) {
        objcopy = find_executable_in_path("llvm-objcopy");
    }
    if (!objcopy.has_value()) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "failed to find an objcopy tool for relocalizing intermediate AArch64 symbols");
        return false;
    }

    std::string command = shell_quote(objcopy->string());
    for (const std::string &symbol_name : symbol_names) {
        command += " --localize-symbol=" + shell_quote(symbol_name);
    }
    command += " " + shell_quote(object_file.string());
    if (std::system(command.c_str()) != 0) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "failed to relocalize native AArch64 intermediate symbols after object merge");
        return false;
    }
    return true;
}

bool move_file_into_place(const std::filesystem::path &source_file,
                          const std::filesystem::path &destination_file,
                          DiagnosticEngine &diagnostic_engine,
                          const char *error_message) {
    remove_file_if_present(destination_file);
    std::error_code rename_error;
    std::filesystem::rename(source_file, destination_file, rename_error);
    if (!rename_error) {
        return true;
    }
    std::error_code copy_error;
    std::filesystem::copy_file(source_file, destination_file,
                               std::filesystem::copy_options::overwrite_existing,
                               copy_error);
    if (!copy_error) {
        remove_file_if_present(source_file);
        return true;
    }
    diagnostic_engine.add_error(DiagnosticStage::Compiler, error_message);
    return false;
}

std::vector<std::string>
collect_internal_defined_symbols(const AArch64ObjectModule &object_module) {
    std::vector<std::string> symbol_names;
    for (const auto &[name, symbol] : object_module.get_symbols()) {
        if (symbol.get_is_defined() && !symbol.get_is_global()) {
            symbol_names.push_back(name);
        }
    }
    return symbol_names;
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

std::string print_module_with_options(const AArch64MachineModule &machine_module,
                                      const AArch64ObjectModule &object_module,
                                      const AsmPrintOptions &options) {
    std::ostringstream output;
    for (const std::string &line : object_module.get_preamble_lines()) {
        output << line << "\n";
    }
    for (const AArch64DebugFileEntry &entry : object_module.get_debug_file_entries()) {
        output << ".file " << entry.index << " " << quote_asm_string(entry.path)
               << "\n";
    }

    bool emitted_anything = !object_module.get_preamble_lines().empty() ||
                            !object_module.get_debug_file_entries().empty();
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
                for (const AArch64MachineInstr &instruction : block.get_instructions()) {
                    append_rendered_instruction(output, instruction, function);
                }
            }
            output << ".size " << function.get_name() << ", .-" << function.get_name()
                   << "\n";
        }
    }
    return output.str();
}

bool emit_single_assembled_object(const AArch64MachineModule &machine_module,
                                  const AArch64ObjectModule &object_module,
                                  const AsmPrintOptions &print_options,
                                  const BackendOptions &backend_options,
                                  const std::filesystem::path &object_file,
                                  DiagnosticEngine &diagnostic_engine) {
    const std::filesystem::path temp_asm =
        make_temp_output_path(object_file, ".tmp.s");
    if (!write_text_file(temp_asm,
                         print_module_with_options(machine_module, object_module,
                                                   print_options),
                         diagnostic_engine,
                         "failed to open temporary asm output file")) {
        return false;
    }
    if (!assemble_aarch64_object(temp_asm, object_file,
                                 backend_options.get_debug_info(),
                                 diagnostic_engine)) {
        remove_file_if_present(temp_asm);
        return false;
    }
    remove_file_if_present(temp_asm);
    return true;
}

} // namespace

std::string AArch64EmissionPass::print_module(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module) const {
    return print_module_with_options(machine_module, object_module,
                                     AsmPrintOptions{});
}

std::unique_ptr<AsmResult>
AArch64EmissionPass::emit_asm_result(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module) const {
    return std::make_unique<AsmResult>(AsmTargetKind::AArch64,
                                       print_module(machine_module, object_module));
}

std::unique_ptr<ObjectResult> AArch64EmissionPass::emit_object_result(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const BackendOptions &backend_options,
    const std::filesystem::path &object_file,
    DiagnosticEngine &diagnostic_engine) const {
    if (object_file.has_parent_path()) {
        std::filesystem::create_directories(object_file.parent_path());
    }

    const bool has_data_objects = !object_module.get_data_objects().empty();
    const bool has_functions = !machine_module.get_functions().empty();
    const std::vector<std::string> internal_defined_symbols =
        collect_internal_defined_symbols(object_module);
    const bool needs_intermediate_symbol_globalization =
        has_data_objects && has_functions && !internal_defined_symbols.empty();

    if (!has_data_objects) {
        if (!emit_single_assembled_object(machine_module, object_module,
                                          AsmPrintOptions{}, backend_options,
                                          object_file, diagnostic_engine)) {
            return nullptr;
        }
    } else if (!has_functions) {
        if (!write_aarch64_data_only_object(
                object_module, object_file,
                AArch64DataOnlyObjectWriterOptions{
                    .force_defined_symbols_global = false},
                diagnostic_engine)) {
            return nullptr;
        }
    } else {
        const std::filesystem::path temp_functions_asm =
            make_temp_output_path(object_file, ".functions.tmp.s");
        const std::filesystem::path temp_functions_object =
            make_temp_output_path(object_file, ".functions.tmp.o");
        const std::filesystem::path temp_data_object =
            make_temp_output_path(object_file, ".data.tmp.o");
        const std::filesystem::path temp_merged_object =
            make_temp_output_path(object_file, ".merged.tmp.o");

        if (!write_text_file(
                temp_functions_asm,
                print_module_with_options(
                    machine_module, object_module,
                    AsmPrintOptions{
                        .include_data_objects = false,
                        .include_functions = true,
                        .force_global_function_symbols =
                            needs_intermediate_symbol_globalization}),
                diagnostic_engine, "failed to open temporary function asm output file")) {
            return nullptr;
        }
        if (!assemble_aarch64_object(temp_functions_asm, temp_functions_object,
                                     backend_options.get_debug_info(),
                                     diagnostic_engine)) {
            remove_file_if_present(temp_functions_asm);
            return nullptr;
        }
        remove_file_if_present(temp_functions_asm);

        if (!write_aarch64_data_only_object(
                object_module, temp_data_object,
                AArch64DataOnlyObjectWriterOptions{
                    .force_defined_symbols_global =
                        needs_intermediate_symbol_globalization},
                diagnostic_engine)) {
            remove_file_if_present(temp_functions_object);
            return nullptr;
        }
        if (!merge_relocatable_objects(temp_merged_object,
                                       {temp_functions_object, temp_data_object},
                                       diagnostic_engine)) {
            remove_file_if_present(temp_functions_object);
            remove_file_if_present(temp_data_object);
            return nullptr;
        }
        remove_file_if_present(temp_functions_object);
        remove_file_if_present(temp_data_object);

        if (needs_intermediate_symbol_globalization &&
            !localize_symbols_in_object(temp_merged_object, internal_defined_symbols,
                                        diagnostic_engine)) {
            remove_file_if_present(temp_merged_object);
            return nullptr;
        }
        if (!move_file_into_place(temp_merged_object, object_file, diagnostic_engine,
                                  "failed to place the merged native AArch64 object file")) {
            remove_file_if_present(temp_merged_object);
            return nullptr;
        }
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
