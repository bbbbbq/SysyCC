#include "backend/asm_gen/aarch64/api/aarch64_restricted_llvm_ir_parser.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_constant_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_parse_common_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_type_support.hpp"

#include <algorithm>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sysycc {

namespace {

std::string trim_copy(const std::string &text) {
    return llvm_import_trim_copy(text);
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return llvm_import_starts_with(text, prefix);
}

bool is_identifier_char(char ch) {
    return llvm_import_is_identifier_char(ch);
}

std::string strip_comment(const std::string &line) {
    return llvm_import_strip_comment(line);
}

std::optional<std::string> unquote_llvm_string_literal(const std::string &text) {
    return llvm_import_unquote_string_literal(text);
}

std::vector<std::string> split_top_level(const std::string &text,
                                         char delimiter) {
    return llvm_import_split_top_level(text, delimiter);
}

std::string strip_trailing_alignment_suffix(const std::string &text) {
    return llvm_import_strip_trailing_alignment_suffix(text);
}

bool is_global_initializer_trailer(std::string_view text) {
    return starts_with(text, "align ") || starts_with(text, "section ") ||
           starts_with(text, "comdat") ||
           starts_with(text, "no_sanitize") ||
           starts_with(text, "sanitize_") ||
           starts_with(text, "partition ") ||
           starts_with(text, "!dbg ");
}

std::string strip_trailing_global_initializer_suffixes(const std::string &text) {
    const std::vector<std::string> parts = split_top_level(text, ',');
    if (parts.empty()) {
        return trim_copy(text);
    }

    std::string result = parts.front();
    for (std::size_t index = 1; index < parts.size(); ++index) {
        if (is_global_initializer_trailer(parts[index])) {
            break;
        }
        result += ", " + parts[index];
    }
    return strip_trailing_alignment_suffix(result);
}

bool looks_like_blockaddress_difference_global_initializer(
    std::string_view text) {
    return text.find("blockaddress(") != std::string_view::npos &&
           text.find("ptrtoint") != std::string_view::npos &&
           text.find(" sub ") != std::string_view::npos;
}

std::optional<std::string> consume_type_token(const std::string &text,
                                              std::size_t &position) {
    return llvm_import_consume_type_token(text, position);
}

std::optional<std::string> parse_symbol_name(const std::string &text,
                                             std::size_t &position,
                                             char prefix) {
    return llvm_import_parse_symbol_name(text, position, prefix);
}

std::optional<AArch64LlvmImportType>
resolve_import_type(const AArch64LlvmImportModule &module,
                    const AArch64LlvmImportType &type, int depth = 0) {
    if (depth > 32) {
        return std::nullopt;
    }
    if (type.kind != AArch64LlvmImportTypeKind::Named) {
        return type;
    }
    for (auto it = module.named_types.rbegin(); it != module.named_types.rend(); ++it) {
        if (it->name != type.named_type_name) {
            continue;
        }
        if (it->is_opaque || !it->body_type.is_valid()) {
            return std::nullopt;
        }
        return resolve_import_type(module, it->body_type, depth + 1);
    }
    return std::nullopt;
}

void add_error(AArch64LlvmImportModule &module, std::string message, int line = 0,
               int column = 0) {
    AArch64CodegenDiagnostic diagnostic;
    diagnostic.severity = AArch64CodegenDiagnosticSeverity::Error;
    diagnostic.stage_name = "llvm-parse";
    diagnostic.message = std::move(message);
    diagnostic.file_path = module.source_name;
    diagnostic.line = line;
    diagnostic.column = column;
    module.diagnostics.push_back(std::move(diagnostic));
}

bool is_modifier_token(const std::string &token) {
    return llvm_import_is_modifier_token(token);
}

std::string strip_leading_modifiers(const std::string &text) {
    return llvm_import_strip_leading_modifiers(text);
}

std::string instruction_opcode_text(const std::string &instruction_text) {
    const std::string normalized =
        strip_leading_modifiers(trim_copy(instruction_text));
    const std::size_t token_end = normalized.find(' ');
    return token_end == std::string::npos ? normalized
                                          : normalized.substr(0, token_end);
}

AArch64LlvmImportInstructionKind classify_instruction_kind(
    const std::string &instruction_text) {
    const std::string normalized =
        strip_leading_modifiers(trim_copy(instruction_text));
    if (starts_with(normalized, "add ") || starts_with(normalized, "sub ") ||
        starts_with(normalized, "mul ") || starts_with(normalized, "sdiv ") ||
        starts_with(normalized, "udiv ") || starts_with(normalized, "srem ") ||
        starts_with(normalized, "urem ") || starts_with(normalized, "and ") ||
        starts_with(normalized, "or ") || starts_with(normalized, "xor ") ||
        starts_with(normalized, "shl ") || starts_with(normalized, "lshr ") ||
        starts_with(normalized, "ashr ") || starts_with(normalized, "fadd ") ||
        starts_with(normalized, "fsub ") || starts_with(normalized, "fmul ") ||
        starts_with(normalized, "fdiv ")) {
        return AArch64LlvmImportInstructionKind::Binary;
    }
    if (starts_with(normalized, "fneg ") ||
        starts_with(normalized, "freeze ")) {
        return AArch64LlvmImportInstructionKind::Unary;
    }
    if (starts_with(normalized, "icmp ") || starts_with(normalized, "fcmp ")) {
        return AArch64LlvmImportInstructionKind::Compare;
    }
    if (starts_with(normalized, "sext ") || starts_with(normalized, "zext ") ||
        starts_with(normalized, "trunc ") ||
        starts_with(normalized, "sitofp ") ||
        starts_with(normalized, "uitofp ") ||
        starts_with(normalized, "fptosi ") ||
        starts_with(normalized, "fptoui ") ||
        starts_with(normalized, "fpext ") ||
        starts_with(normalized, "fptrunc ") ||
        starts_with(normalized, "bitcast ") ||
        starts_with(normalized, "addrspacecast ") ||
        starts_with(normalized, "inttoptr ") ||
        starts_with(normalized, "ptrtoint ")) {
        return AArch64LlvmImportInstructionKind::Cast;
    }
    if (starts_with(normalized, "alloca ")) {
        return AArch64LlvmImportInstructionKind::Alloca;
    }
    if (starts_with(normalized, "load ")) {
        return AArch64LlvmImportInstructionKind::Load;
    }
    if (starts_with(normalized, "store ")) {
        return AArch64LlvmImportInstructionKind::Store;
    }
    if (starts_with(normalized, "getelementptr ")) {
        return AArch64LlvmImportInstructionKind::GetElementPtr;
    }
    if (starts_with(normalized, "select ")) {
        return AArch64LlvmImportInstructionKind::Select;
    }
    if (starts_with(normalized, "extractelement ")) {
        return AArch64LlvmImportInstructionKind::ExtractElement;
    }
    if (starts_with(normalized, "insertelement ")) {
        return AArch64LlvmImportInstructionKind::InsertElement;
    }
    if (starts_with(normalized, "shufflevector ")) {
        return AArch64LlvmImportInstructionKind::ShuffleVector;
    }
    if (starts_with(normalized, "call ") &&
        normalized.find("@llvm.vector.reduce.add.") != std::string::npos) {
        return AArch64LlvmImportInstructionKind::VectorReduceAdd;
    }
    if (starts_with(normalized, "call ")) {
        return AArch64LlvmImportInstructionKind::Call;
    }
    if (starts_with(normalized, "phi ")) {
        return AArch64LlvmImportInstructionKind::Phi;
    }
    if (starts_with(normalized, "br label ")) {
        return AArch64LlvmImportInstructionKind::Branch;
    }
    if (starts_with(normalized, "indirectbr ")) {
        return AArch64LlvmImportInstructionKind::IndirectBranch;
    }
    if (starts_with(normalized, "switch ")) {
        return AArch64LlvmImportInstructionKind::Switch;
    }
    if (starts_with(normalized, "br ")) {
        return AArch64LlvmImportInstructionKind::CondBranch;
    }
    if (starts_with(normalized, "unreachable")) {
        return AArch64LlvmImportInstructionKind::Unreachable;
    }
    if (starts_with(normalized, "ret ")) {
        return AArch64LlvmImportInstructionKind::Return;
    }
    return AArch64LlvmImportInstructionKind::Unknown;
}

struct ParsedBodyLine {
    std::string text;
    int line = 0;
};

std::vector<AArch64LlvmImportBasicBlock> split_basic_blocks(
    AArch64LlvmImportModule &module, const std::vector<ParsedBodyLine> &body_lines,
    int line_number) {
    std::vector<AArch64LlvmImportBasicBlock> blocks;
    AArch64LlvmImportBasicBlock *current_block = nullptr;
    (void)module;
    (void)line_number;
    for (std::size_t index = 0; index < body_lines.size(); ++index) {
        const ParsedBodyLine &body_line = body_lines[index];
        const std::string &line = body_line.text;
        if (!line.empty() && line.back() == ':') {
            AArch64LlvmImportBasicBlock block;
            block.label = trim_copy(line.substr(0, line.size() - 1));
            blocks.push_back(std::move(block));
            current_block = &blocks.back();
            continue;
        }
        if (current_block == nullptr) {
            AArch64LlvmImportBasicBlock block;
            block.label = "0";
            blocks.push_back(std::move(block));
            current_block = &blocks.back();
        }

        std::string result_name;
        std::string instruction_text = line;
        if (starts_with(trim_copy(line), "switch ") && line.find(']') == std::string::npos) {
            while (index + 1 < body_lines.size()) {
                instruction_text += "\n" + body_lines[index + 1].text;
                ++index;
                if (body_lines[index].text.find(']') != std::string::npos) {
                    break;
                }
            }
        }
        const std::size_t equal_pos = line.find('=');
        if (!line.empty() && line.front() == '%' && equal_pos != std::string::npos) {
            result_name = trim_copy(line.substr(1, equal_pos - 1));
            instruction_text = trim_copy(line.substr(equal_pos + 1));
        }

        AArch64LlvmImportInstruction instruction;
        instruction.kind = classify_instruction_kind(instruction_text);
        instruction.result_name = std::move(result_name);
        instruction.opcode_text = instruction_opcode_text(instruction_text);
        instruction.canonical_text = instruction_text;
        instruction.line = body_line.line;
        current_block->instructions.push_back(std::move(instruction));
    }
    return blocks;
}

bool parse_named_type_definition(AArch64LlvmImportModule &module,
                                 const std::string &line, int line_number) {
    std::size_t position = 0;
    const std::optional<std::string> name =
        parse_symbol_name(line, position, '%');
    if (!name.has_value()) {
        add_error(module, "failed to parse LLVM named type definition",
                  line_number, 1);
        return false;
    }
    const std::size_t equal_pos = line.find('=', position);
    if (equal_pos == std::string::npos) {
        add_error(module, "failed to parse LLVM named type definition",
                  line_number, 1);
        return false;
    }
    std::string remainder = trim_copy(line.substr(equal_pos + 1));
    if (!starts_with(remainder, "type ")) {
        add_error(module, "unsupported LLVM named type declaration: " + line,
                  line_number, 1);
        return false;
    }
    module.named_types.push_back(AArch64LlvmImportNamedType{
        name.value(),
        trim_copy(remainder.substr(5)),
        trim_copy(remainder.substr(5)) == "opaque"
            ? AArch64LlvmImportType{}
            : parse_llvm_import_type_text(trim_copy(remainder.substr(5)))
                  .value_or(AArch64LlvmImportType{}),
        trim_copy(remainder.substr(5)) == "opaque",
        line_number});
    return true;
}

bool parse_global_definition(AArch64LlvmImportModule &module,
                             const std::string &line, int line_number) {
    std::size_t position = 0;
    const std::optional<std::string> name =
        parse_symbol_name(line, position, '@');
    if (!name.has_value()) {
        add_error(module, "failed to parse LLVM global name", line_number, 1);
        return false;
    }
    const std::size_t equal_pos = line.find('=', position);
    if (equal_pos == std::string::npos) {
        add_error(module, "failed to parse LLVM global definition", line_number, 1);
        return false;
    }

    if ((name.value() == "llvm.compiler.used" || name.value() == "llvm.used") &&
        line.find("appending global") != std::string::npos) {
        return true;
    }

    std::string remainder = trim_copy(line.substr(equal_pos + 1));
    bool is_internal_linkage = false;
    bool is_constant = false;
    bool is_external_declaration = false;
    while (true) {
        if (starts_with(remainder, "internal ")) {
            is_internal_linkage = true;
            remainder = trim_copy(remainder.substr(9));
            continue;
        }
        if (starts_with(remainder, "dso_local ")) {
            remainder = trim_copy(remainder.substr(10));
            continue;
        }
        if (starts_with(remainder, "private ")) {
            is_internal_linkage = true;
            remainder = trim_copy(remainder.substr(8));
            continue;
        }
        if (starts_with(remainder, "unnamed_addr ")) {
            remainder = trim_copy(remainder.substr(13));
            continue;
        }
        if (starts_with(remainder, "local_unnamed_addr ")) {
            remainder = trim_copy(remainder.substr(19));
            continue;
        }
        if (starts_with(remainder, "appending ")) {
            is_internal_linkage = true;
            remainder = trim_copy(remainder.substr(10));
            continue;
        }
        if (starts_with(remainder, "external ")) {
            is_external_declaration = true;
            remainder = trim_copy(remainder.substr(9));
            continue;
        }
        break;
    }
    if (starts_with(remainder, "global ")) {
        is_constant = false;
        remainder = trim_copy(remainder.substr(7));
    } else if (starts_with(remainder, "constant ")) {
        is_constant = true;
        remainder = trim_copy(remainder.substr(9));
    } else {
        add_error(module, "unsupported LLVM global linkage or storage class",
                  line_number, 1);
        return false;
    }

    std::size_t type_position = 0;
    const std::optional<std::string> type_text =
        consume_type_token(remainder, type_position);
    if (!type_text.has_value()) {
        add_error(module, "failed to parse LLVM global type", line_number, 1);
        return false;
    }

    const auto parsed_type = parse_llvm_import_type_text(type_text.value());
    if (!parsed_type.has_value()) {
        add_error(module, "failed to parse LLVM global type", line_number, 1);
        return false;
    }

    std::string initializer_text;
    AArch64LlvmImportConstant initializer;
    if (!is_external_declaration) {
        initializer_text = strip_trailing_global_initializer_suffixes(
            trim_copy(remainder.substr(type_position)));
        const AArch64LlvmImportType constant_type =
            resolve_import_type(module, *parsed_type).value_or(*parsed_type);
        const auto parsed_initializer =
            parse_llvm_import_constant_text(constant_type, initializer_text);
        if (!parsed_initializer.has_value()) {
            if (!looks_like_blockaddress_difference_global_initializer(
                    initializer_text)) {
                add_error(module, "failed to parse LLVM global initializer: " +
                                      initializer_text,
                          line_number, 1);
                return false;
            }
        } else {
            initializer = *parsed_initializer;
        }
    }

    module.globals.push_back(AArch64LlvmImportGlobal{
        name.value(),
        type_text.value(),
        *parsed_type,
        initializer_text,
        initializer,
        is_internal_linkage,
        is_constant,
        is_external_declaration,
        line_number});
    return true;
}

bool parse_alias_definition(AArch64LlvmImportModule &module,
                            const std::string &line, int line_number) {
    std::size_t position = 0;
    const std::optional<std::string> name =
        parse_symbol_name(line, position, '@');
    if (!name.has_value()) {
        add_error(module, "failed to parse LLVM alias name", line_number, 1);
        return false;
    }
    const std::size_t equal_pos = line.find('=', position);
    if (equal_pos == std::string::npos) {
        add_error(module, "failed to parse LLVM alias target", line_number, 1);
        return false;
    }

    std::string remainder = trim_copy(line.substr(equal_pos + 1));
    while (true) {
        if (starts_with(remainder, "internal ")) {
            remainder = trim_copy(remainder.substr(9));
            continue;
        }
        if (starts_with(remainder, "dso_local ")) {
            remainder = trim_copy(remainder.substr(10));
            continue;
        }
        if (starts_with(remainder, "private ")) {
            remainder = trim_copy(remainder.substr(8));
            continue;
        }
        if (starts_with(remainder, "unnamed_addr ")) {
            remainder = trim_copy(remainder.substr(13));
            continue;
        }
        if (starts_with(remainder, "local_unnamed_addr ")) {
            remainder = trim_copy(remainder.substr(19));
            continue;
        }
        break;
    }
    if (!starts_with(remainder, "alias ")) {
        add_error(module, "failed to parse LLVM alias target", line_number, 1);
        return false;
    }
    remainder = trim_copy(remainder.substr(6));
    const std::vector<std::string> operands = split_top_level(remainder, ',');
    if (operands.size() != 2) {
        add_error(module, "failed to parse LLVM alias target", line_number, 1);
        return false;
    }

    const std::string target_operand_text = trim_copy(operands[1]);
    std::size_t target_position = 0;
    const std::optional<std::string> target_type_text =
        consume_type_token(target_operand_text, target_position);
    if (!target_type_text.has_value()) {
        add_error(module, "failed to parse LLVM alias target", line_number, 1);
        return false;
    }

    const auto parsed_target_type =
        parse_llvm_import_type_text(*target_type_text);
    if (!parsed_target_type.has_value()) {
        add_error(module, "failed to parse LLVM alias target", line_number, 1);
        return false;
    }

    const AArch64LlvmImportType resolved_target_type =
        resolve_import_type(module, *parsed_target_type)
            .value_or(*parsed_target_type);
    const std::string target_text =
        trim_copy(target_operand_text.substr(target_position));
    const auto target =
        parse_llvm_import_constant_text(resolved_target_type, target_text);
    if (!target.has_value()) {
        add_error(module, "failed to parse LLVM alias target", line_number, 1);
        return false;
    }

    module.aliases.push_back(AArch64LlvmImportAlias{name.value(),
                                                    *target_type_text,
                                                    *parsed_target_type,
                                                    target_text,
                                                    *target,
                                                    line_number});
    return true;
}

bool parse_function_signature(AArch64LlvmImportModule &module,
                              const std::string &line, bool is_definition,
                              AArch64LlvmImportFunction &function) {
    std::string remainder = trim_copy(line.substr(is_definition ? 6 : 7));
    function.is_definition = is_definition;
    if (starts_with(remainder, "internal ")) {
        function.is_internal_linkage = true;
        remainder = trim_copy(remainder.substr(9));
    }
    if (starts_with(remainder, "extern_weak ")) {
        function.is_extern_weak = true;
        remainder = trim_copy(remainder.substr(12));
    }
    remainder = strip_leading_modifiers(remainder);

    std::size_t type_position = 0;
    const std::optional<std::string> return_type_text =
        consume_type_token(remainder, type_position);
    if (!return_type_text.has_value()) {
        add_error(module, "failed to parse LLVM function return type",
                  function.line, 1);
        return false;
    }

    const std::size_t at_pos = remainder.find('@', type_position);
    const std::size_t open_paren_pos = remainder.find('(', at_pos);
    const std::size_t close_paren_pos = remainder.rfind(')');
    if (at_pos == std::string::npos || open_paren_pos == std::string::npos ||
        close_paren_pos == std::string::npos || close_paren_pos < open_paren_pos) {
        add_error(module, "failed to parse LLVM function signature",
                  function.line, 1);
        return false;
    }

    function.name = trim_copy(
        remainder.substr(at_pos + 1, open_paren_pos - at_pos - 1));
    function.return_type_text = *return_type_text;
    function.return_type =
        parse_llvm_import_type_text(*return_type_text).value_or(
            AArch64LlvmImportType{});
    const std::string parameter_text =
        remainder.substr(open_paren_pos + 1,
                         close_paren_pos - open_paren_pos - 1);
    for (const std::string &parameter_entry :
         split_top_level(parameter_text, ',')) {
        if (parameter_entry.empty()) {
            continue;
        }
        if (parameter_entry == "...") {
            function.is_variadic = true;
            continue;
        }
        const std::string trimmed_parameter =
            strip_leading_modifiers(trim_copy(parameter_entry));
        std::size_t position = 0;
        const std::optional<std::string> type_text =
            consume_type_token(trimmed_parameter, position);
        if (!type_text.has_value()) {
            add_error(module, "failed to parse LLVM function parameter",
                      function.line, 1);
            return false;
        }

        AArch64LlvmImportParameter parameter;
        parameter.type_text = *type_text;
        parameter.type =
            parse_llvm_import_type_text(*type_text).value_or(
                AArch64LlvmImportType{});
        if (!parameter.type.is_valid()) {
            add_error(module, "failed to parse LLVM function parameter type",
                      function.line, 1);
            return false;
        }
        std::string remainder_text =
            strip_leading_modifiers(trim_copy(trimmed_parameter.substr(position)));
        while (!remainder_text.empty()) {
            std::size_t token_end = 0;
            while (token_end < remainder_text.size() &&
                   std::isspace(
                       static_cast<unsigned char>(remainder_text[token_end])) == 0) {
                ++token_end;
            }
            const std::string token = remainder_text.substr(0, token_end);
            if (!token.empty() && token.front() == '%') {
                parameter.name = token.substr(1);
                break;
            }
            if (!is_modifier_token(token)) {
                break;
            }
            remainder_text = trim_copy(remainder_text.substr(token_end));
            if (token == "align") {
                std::size_t align_value_end = 0;
                while (align_value_end < remainder_text.size() &&
                       std::isspace(static_cast<unsigned char>(
                           remainder_text[align_value_end])) == 0) {
                    ++align_value_end;
                }
                const std::string align_value =
                    remainder_text.substr(0, align_value_end);
                if (!align_value.empty() &&
                    std::all_of(align_value.begin(), align_value.end(),
                                [](unsigned char ch) {
                                    return std::isdigit(ch) != 0;
                                })) {
                    remainder_text =
                        trim_copy(remainder_text.substr(align_value_end));
                }
            }
        }
        function.parameters.push_back(std::move(parameter));
    }
    return true;
}

AArch64LlvmImportModule parse_lines(const std::string &source_name,
                                    const std::vector<std::string> &lines) {
    AArch64LlvmImportModule module;
    module.source_name = source_name;

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const int line_number = static_cast<int>(index + 1);
        const std::string current = strip_comment(lines[index]);
        if (current.empty()) {
            continue;
        }
        if (starts_with(current, "source_filename = ") ||
            starts_with(current, "attributes #") || starts_with(current, "!")) {
            continue;
        }
        if (starts_with(current, "module asm ")) {
            const std::optional<std::string> module_asm =
                unquote_llvm_string_literal(trim_copy(current.substr(11)));
            if (!module_asm.has_value()) {
                add_error(module, "failed to parse LLVM module asm line",
                          line_number, 1);
                break;
            }
            module.module_asm_lines.push_back(*module_asm);
            continue;
        }
        if (!current.empty() && current.front() == '$' &&
            current.find("comdat") != std::string::npos) {
            continue;
        }
        if (starts_with(current, "target datalayout = ")) {
            continue;
        }
        if (starts_with(current, "target triple = ")) {
            const std::size_t quote_begin = current.find('"');
            const std::size_t quote_end = current.rfind('"');
            if (quote_begin != std::string::npos &&
                quote_end != std::string::npos && quote_end > quote_begin) {
                module.source_target_triple =
                    current.substr(quote_begin + 1,
                                   quote_end - quote_begin - 1);
            }
            continue;
        }
        if (!current.empty() && current.front() == '%' &&
            current.find(" = type ") != std::string::npos) {
            if (!parse_named_type_definition(module, current, line_number)) {
                break;
            }
            continue;
        }
        if (!current.empty() && current.front() == '@') {
            if (current.find(" alias ") != std::string::npos) {
                if (!parse_alias_definition(module, current, line_number)) {
                    break;
                }
                continue;
            }
            if (!parse_global_definition(module, current, line_number)) {
                break;
            }
            continue;
        }
        if (starts_with(current, "declare ")) {
            AArch64LlvmImportFunction function;
            function.line = line_number;
            if (!parse_function_signature(module, current, false, function)) {
                break;
            }
            module.functions.push_back(std::move(function));
            continue;
        }
        if (starts_with(current, "define ")) {
            AArch64LlvmImportFunction function;
            function.line = line_number;
            if (!parse_function_signature(module, current, true, function)) {
                break;
            }
            std::vector<ParsedBodyLine> body_lines;
            ++index;
            for (; index < lines.size(); ++index) {
                const std::string body_line = strip_comment(lines[index]);
                if (body_line == "}") {
                    break;
                }
                if (!body_line.empty()) {
                    body_lines.push_back(
                        ParsedBodyLine{body_line, static_cast<int>(index + 1)});
                }
            }
            function.basic_blocks =
                split_basic_blocks(module, body_lines, line_number);
            if (!module.diagnostics.empty()) {
                break;
            }
            module.functions.push_back(std::move(function));
            continue;
        }

        add_error(module, "unsupported top-level LLVM IR statement: " + current,
                  line_number, 1);
        break;
    }

    return module;
}

} // namespace

AArch64LlvmImportModule
parse_restricted_llvm_ir_file(const std::string &file_path) {
    std::ifstream input(file_path);
    std::vector<std::string> lines;
    if (!input.is_open()) {
        AArch64LlvmImportModule module;
        module.source_name = file_path;
        add_error(module, "failed to open LLVM IR input file");
        return module;
    }

    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return parse_lines(file_path, lines);
}

AArch64LlvmImportModule parse_restricted_llvm_ir_text(
    const std::string &source_name, const std::string &text) {
    std::vector<std::string> lines;
    std::string current_line;
    for (char ch : text) {
        if (ch == '\n') {
            lines.push_back(current_line);
            current_line.clear();
            continue;
        }
        if (ch != '\r') {
            current_line.push_back(ch);
        }
    }
    if (!current_line.empty() || (!text.empty() && text.back() == '\n')) {
        lines.push_back(current_line);
    }
    return parse_lines(source_name, lines);
}

} // namespace sysycc
