#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_instruction_parse_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_constant_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_parse_common_support.hpp"
#include "backend/asm_gen/aarch64/api/aarch64_llvm_import_type_support.hpp"

#include <cctype>
#include <sstream>

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

std::vector<std::string> split_top_level(const std::string &text,
                                         char delimiter) {
    return llvm_import_split_top_level(text, delimiter);
}

std::size_t find_top_level_to_pos(const std::string &text,
                                  std::size_t start_pos) {
    int square_depth = 0;
    int brace_depth = 0;
    int paren_depth = 0;
    int angle_depth = 0;
    for (std::size_t index = start_pos; index + 3 < text.size(); ++index) {
        switch (text[index]) {
        case '[':
            ++square_depth;
            break;
        case ']':
            --square_depth;
            break;
        case '{':
            ++brace_depth;
            break;
        case '}':
            --brace_depth;
            break;
        case '(':
            ++paren_depth;
            break;
        case ')':
            --paren_depth;
            break;
        case '<':
            ++angle_depth;
            break;
        case '>':
            --angle_depth;
            break;
        default:
            break;
        }
        if (square_depth == 0 && brace_depth == 0 && paren_depth == 0 &&
            angle_depth == 0 && text[index] == ' ' && text[index + 1] == 't' &&
            text[index + 2] == 'o' && text[index + 3] == ' ') {
            return index;
        }
    }
    return std::string::npos;
}

bool is_modifier_token(const std::string &token) {
    return llvm_import_is_modifier_token(token);
}

std::string strip_leading_modifiers(const std::string &text) {
    return llvm_import_strip_leading_modifiers(text);
}

std::string strip_metadata_suffix(const std::string &text) {
    return llvm_import_strip_metadata_suffix(text);
}

std::optional<std::string> consume_type_token(const std::string &text,
                                              std::size_t &position) {
    return llvm_import_consume_type_token(text, position);
}

std::string payload_after_opcode(const AArch64LlvmImportInstruction &instruction) {
    const std::string normalized = strip_metadata_suffix(instruction.canonical_text);
    const std::size_t opcode_pos = normalized.find(instruction.opcode_text);
    if (opcode_pos == std::string::npos) {
        return {};
    }
    return trim_copy(normalized.substr(opcode_pos + instruction.opcode_text.size()));
}

bool is_raw_constant_expression_text(const std::string &text) {
    const std::string normalized = strip_leading_modifiers(trim_copy(text));
    return starts_with(normalized, "add ") || starts_with(normalized, "sub ") ||
           starts_with(normalized, "mul ") || starts_with(normalized, "sdiv ") ||
           starts_with(normalized, "udiv ") || starts_with(normalized, "srem ") ||
           starts_with(normalized, "urem ") || starts_with(normalized, "and ") ||
           starts_with(normalized, "or ") || starts_with(normalized, "xor ") ||
           starts_with(normalized, "shl ") || starts_with(normalized, "lshr ") ||
           starts_with(normalized, "ashr ");
}

std::optional<AArch64LlvmImportTypedValue>
parse_typed_value_with_known_type(const std::string &type_text,
                                  const AArch64LlvmImportType &type,
                                  const std::string &value_text) {
    AArch64LlvmImportTypedValue value;
    value.type_text = type_text;
    value.type = type;
    value.value_text = strip_leading_modifiers(trim_copy(value_text));
    if (!value.value_text.empty() && value.value_text.front() == '%') {
        value.kind = AArch64LlvmImportValueKind::Local;
        value.local_name = value.value_text.substr(1);
        return value;
    }
    if (!value.value_text.empty() && value.value_text.front() == '@') {
        value.kind = AArch64LlvmImportValueKind::Global;
        value.global_name = value.value_text.substr(1);
        return value;
    }
    if (const auto constant =
            parse_llvm_import_constant_text(value.type, value.value_text);
        constant.has_value()) {
        value.kind = AArch64LlvmImportValueKind::Constant;
        value.constant = *constant;
        return value;
    }
    if (is_raw_constant_expression_text(value.value_text)) {
        value.kind = AArch64LlvmImportValueKind::ConstantExpressionRaw;
        value.raw_constant_expression_text = value.value_text;
        return value;
    }
    return std::nullopt;
}

std::optional<AArch64LlvmImportTypedValue>
parse_typed_value(const std::string &text) {
    const std::string normalized = strip_leading_modifiers(trim_copy(text));
    std::size_t position = 0;
    const std::optional<std::string> type_text =
        consume_type_token(normalized, position);
    if (!type_text.has_value()) {
        return std::nullopt;
    }
    const auto parsed_type = parse_llvm_import_type_text(*type_text);
    if (!parsed_type.has_value()) {
        return std::nullopt;
    }
    return parse_typed_value_with_known_type(*type_text, *parsed_type,
                                             normalized.substr(position));
}

std::optional<AArch64LlvmImportTypedValue>
parse_reference_value(const std::string &text) {
    AArch64LlvmImportTypedValue value;
    value.type_text = "ptr";
    value.type.kind = AArch64LlvmImportTypeKind::Pointer;
    value.value_text = strip_leading_modifiers(trim_copy(text));
    if (value.value_text.empty()) {
        return std::nullopt;
    }
    if (value.value_text.front() == '%') {
        value.kind = AArch64LlvmImportValueKind::Local;
        value.local_name = value.value_text.substr(1);
        return value;
    }
    if (value.value_text.front() == '@') {
        value.kind = AArch64LlvmImportValueKind::Global;
        value.global_name = value.value_text.substr(1);
        return value;
    }
    return std::nullopt;
}

std::optional<std::string> parse_branch_target(const std::string &text) {
    std::string normalized = trim_copy(text);
    if (!starts_with(normalized, "label ")) {
        return std::nullopt;
    }
    normalized = trim_copy(normalized.substr(6));
    if (!normalized.empty() && normalized.front() == '%') {
        normalized.erase(normalized.begin());
    }
    return normalized.empty() ? std::nullopt
                              : std::optional<std::string>(normalized);
}

std::size_t parse_optional_alignment(const std::vector<std::string> &operands,
                                     std::size_t index) {
    if (index >= operands.size()) {
        return 0;
    }
    const std::string alignment_text = trim_copy(operands[index]);
    if (!starts_with(alignment_text, "align ")) {
        return 0;
    }
    try {
        return static_cast<std::size_t>(
            std::stoull(trim_copy(alignment_text.substr(6))));
    } catch (...) {
        return 0;
    }
}

} // namespace

std::optional<AArch64LlvmImportCompareSpec>
parse_llvm_import_compare_spec(const AArch64LlvmImportInstruction &instruction) {
    const bool is_float_compare = instruction.opcode_text == "fcmp";
    const bool is_integer_compare = instruction.opcode_text == "icmp";
    if (!is_float_compare && !is_integer_compare) {
        return std::nullopt;
    }

    std::string payload = strip_leading_modifiers(payload_after_opcode(instruction));
    const std::size_t predicate_end = payload.find(' ');
    if (predicate_end == std::string::npos) {
        return std::nullopt;
    }

    AArch64LlvmImportCompareSpec spec;
    spec.is_float_compare = is_float_compare;
    spec.predicate_text = payload.substr(0, predicate_end);
    payload = trim_copy(payload.substr(predicate_end + 1));

    std::size_t type_position = 0;
    const std::optional<std::string> operand_type_text =
        consume_type_token(payload, type_position);
    if (!operand_type_text.has_value()) {
        return std::nullopt;
    }
    const auto operand_type = parse_llvm_import_type_text(*operand_type_text);
    if (!operand_type.has_value()) {
        return std::nullopt;
    }
    const std::vector<std::string> operands =
        split_top_level(trim_copy(payload.substr(type_position)), ',');
    if (operands.size() != 2) {
        return std::nullopt;
    }

    auto lhs = parse_typed_value_with_known_type(*operand_type_text, *operand_type,
                                                 operands[0]);
    auto rhs = parse_typed_value_with_known_type(*operand_type_text, *operand_type,
                                                 operands[1]);
    if (!lhs.has_value() || !rhs.has_value()) {
        return std::nullopt;
    }
    spec.lhs = std::move(*lhs);
    spec.rhs = std::move(*rhs);
    return spec;
}

std::optional<AArch64LlvmImportBinarySpec>
parse_llvm_import_binary_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.kind != AArch64LlvmImportInstructionKind::Binary) {
        return std::nullopt;
    }
    const std::string payload = strip_leading_modifiers(payload_after_opcode(instruction));
    std::size_t type_position = 0;
    const std::optional<std::string> type_text =
        consume_type_token(payload, type_position);
    if (!type_text.has_value()) {
        return std::nullopt;
    }
    const std::vector<std::string> operands =
        split_top_level(trim_copy(payload.substr(type_position)), ',');
    if (operands.size() != 2) {
        return std::nullopt;
    }
    const auto parsed_type = parse_llvm_import_type_text(*type_text);
    if (!parsed_type.has_value()) {
        return std::nullopt;
    }
    auto lhs =
        parse_typed_value_with_known_type(*type_text, *parsed_type, operands[0]);
    auto rhs =
        parse_typed_value_with_known_type(*type_text, *parsed_type, operands[1]);
    if (!lhs.has_value() || !rhs.has_value()) {
        return std::nullopt;
    }
    return AArch64LlvmImportBinarySpec{*type_text, *parsed_type, *lhs, *rhs};
}

std::optional<AArch64LlvmImportUnarySpec>
parse_llvm_import_unary_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.kind != AArch64LlvmImportInstructionKind::Unary) {
        return std::nullopt;
    }
    const auto operand = parse_typed_value(payload_after_opcode(instruction));
    if (!operand.has_value()) {
        return std::nullopt;
    }
    return AArch64LlvmImportUnarySpec{operand->type_text, operand->type, *operand};
}

std::optional<AArch64LlvmImportCastSpec>
parse_llvm_import_cast_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.kind != AArch64LlvmImportInstructionKind::Cast) {
        return std::nullopt;
    }
    const std::string payload = strip_leading_modifiers(payload_after_opcode(instruction));
    std::size_t source_type_position = 0;
    const std::optional<std::string> source_type_text =
        consume_type_token(payload, source_type_position);
    if (!source_type_text.has_value()) {
        return std::nullopt;
    }
    const std::size_t to_pos =
        find_top_level_to_pos(payload, source_type_position);
    if (to_pos == std::string::npos) {
        return std::nullopt;
    }
    const auto source_type = parse_llvm_import_type_text(*source_type_text);
    if (!source_type.has_value()) {
        return std::nullopt;
    }
    const auto target_type_text = trim_copy(payload.substr(to_pos + 4));
    const auto target_type = parse_llvm_import_type_text(target_type_text);
    if (!target_type.has_value()) {
        return std::nullopt;
    }
    const auto source_value = parse_typed_value_with_known_type(
        *source_type_text, *source_type,
        payload.substr(source_type_position, to_pos - source_type_position));
    if (!source_value.has_value()) {
        return std::nullopt;
    }
    return AArch64LlvmImportCastSpec{
        *source_type_text,
        *source_type,
        *source_value,
        target_type_text,
        *target_type};
}

std::optional<AArch64LlvmImportAllocaSpec>
parse_llvm_import_alloca_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "alloca") {
        return std::nullopt;
    }
    const std::string payload = payload_after_opcode(instruction);
    const std::vector<std::string> operands = split_top_level(payload, ',');
    if (operands.empty()) {
        return std::nullopt;
    }
    const std::string first_operand = trim_copy(operands.front());
    std::size_t type_position = 0;
    const std::optional<std::string> allocated_type_text =
        consume_type_token(first_operand, type_position);
    if (!allocated_type_text.has_value()) {
        return std::nullopt;
    }

    AArch64LlvmImportAllocaSpec spec;
    spec.allocated_type_text = *allocated_type_text;
    const auto allocated_type = parse_llvm_import_type_text(*allocated_type_text);
    if (!allocated_type.has_value()) {
        return std::nullopt;
    }
    spec.allocated_type = *allocated_type;
    for (std::size_t index = 1; index < operands.size(); ++index) {
        const std::string operand = trim_copy(operands[index]);
        if (operand.empty()) {
            continue;
        }
        if (starts_with(operand, "align ")) {
            try {
                spec.alignment = static_cast<std::size_t>(
                    std::stoull(trim_copy(operand.substr(6))));
            } catch (...) {
                return std::nullopt;
            }
            continue;
        }
        auto count = parse_typed_value(operand);
        if (!count.has_value()) {
            return std::nullopt;
        }
        spec.element_count = std::move(*count);
    }
    return spec;
}

std::optional<AArch64LlvmImportLoadSpec>
parse_llvm_import_load_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "load") {
        return std::nullopt;
    }
    const std::vector<std::string> operands =
        split_top_level(payload_after_opcode(instruction), ',');
    if (operands.size() < 2) {
        return std::nullopt;
    }

    auto address = parse_typed_value(operands[1]);
    if (!address.has_value()) {
        return std::nullopt;
    }

    AArch64LlvmImportLoadSpec spec;
    spec.load_type_text = strip_leading_modifiers(trim_copy(operands[0]));
    const auto load_type = parse_llvm_import_type_text(spec.load_type_text);
    if (!load_type.has_value()) {
        return std::nullopt;
    }
    spec.load_type = *load_type;
    spec.address = std::move(*address);
    spec.alignment = parse_optional_alignment(operands, 2);
    if (operands.size() > 2 && starts_with(trim_copy(operands[2]), "align ") &&
        spec.alignment == 0) {
        return std::nullopt;
    }
    return spec;
}

std::optional<AArch64LlvmImportStoreSpec>
parse_llvm_import_store_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "store") {
        return std::nullopt;
    }
    const std::vector<std::string> operands =
        split_top_level(payload_after_opcode(instruction), ',');
    if (operands.size() < 2) {
        return std::nullopt;
    }

    auto value = parse_typed_value(strip_leading_modifiers(operands[0]));
    auto address = parse_typed_value(strip_leading_modifiers(operands[1]));
    if (!value.has_value() || !address.has_value()) {
        return std::nullopt;
    }

    AArch64LlvmImportStoreSpec spec;
    spec.value = std::move(*value);
    spec.address = std::move(*address);
    spec.alignment = parse_optional_alignment(operands, 2);
    if (operands.size() > 2 && starts_with(trim_copy(operands[2]), "align ") &&
        spec.alignment == 0) {
        return std::nullopt;
    }
    return spec;
}

std::optional<AArch64LlvmImportGetElementPtrSpec>
parse_llvm_import_gep_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "getelementptr") {
        return std::nullopt;
    }
    std::string payload = payload_after_opcode(instruction);

    AArch64LlvmImportGetElementPtrSpec spec;
    if (starts_with(payload, "inbounds ")) {
        spec.is_inbounds = true;
        payload = trim_copy(payload.substr(9));
    }

    const std::vector<std::string> operands = split_top_level(payload, ',');
    if (operands.size() < 2) {
        return std::nullopt;
    }

    auto base = parse_typed_value(operands[1]);
    if (!base.has_value()) {
        return std::nullopt;
    }

    spec.source_type_text = trim_copy(operands[0]);
    const auto source_type = parse_llvm_import_type_text(spec.source_type_text);
    if (!source_type.has_value()) {
        return std::nullopt;
    }
    spec.source_type = *source_type;
    spec.base = std::move(*base);
    for (std::size_t index = 2; index < operands.size(); ++index) {
        auto typed_index = parse_typed_value(operands[index]);
        if (!typed_index.has_value()) {
            return std::nullopt;
        }
        spec.indices.push_back(std::move(*typed_index));
    }
    return spec;
}

std::optional<AArch64LlvmImportCallSpec>
parse_llvm_import_call_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "call") {
        return std::nullopt;
    }
    const std::string payload =
        strip_leading_modifiers(payload_after_opcode(instruction));
    std::size_t return_type_position = 0;
    const std::optional<std::string> return_type_text =
        consume_type_token(payload, return_type_position);
    if (!return_type_text.has_value()) {
        return std::nullopt;
    }

    std::size_t callee_position = return_type_position;
    while (callee_position < payload.size() &&
           std::isspace(static_cast<unsigned char>(payload[callee_position])) != 0) {
        ++callee_position;
    }
    if (callee_position < payload.size() && payload[callee_position] == '(') {
        int depth = 0;
        do {
            if (payload[callee_position] == '(') {
                ++depth;
            } else if (payload[callee_position] == ')') {
                --depth;
            }
            ++callee_position;
        } while (callee_position < payload.size() && depth > 0);
        if (depth != 0) {
            return std::nullopt;
        }
        while (callee_position < payload.size() &&
               (std::isspace(static_cast<unsigned char>(payload[callee_position])) != 0 ||
                payload[callee_position] == '*')) {
            ++callee_position;
        }
    }

    const std::size_t open_paren_pos = payload.find('(', callee_position);
    const std::size_t close_paren_pos = payload.rfind(')');
    if (open_paren_pos == std::string::npos || close_paren_pos == std::string::npos ||
        close_paren_pos < open_paren_pos) {
        return std::nullopt;
    }

    AArch64LlvmImportCallSpec spec;
    spec.return_type_text = *return_type_text;
    const auto return_type = parse_llvm_import_type_text(*return_type_text);
    if (!return_type.has_value()) {
        return std::nullopt;
    }
    spec.return_type = *return_type;
    auto callee = parse_reference_value(
        payload.substr(callee_position, open_paren_pos - callee_position));
    if (!callee.has_value()) {
        return std::nullopt;
    }
    spec.callee = std::move(*callee);
    for (const std::string &argument_entry :
         split_top_level(payload.substr(open_paren_pos + 1,
                                        close_paren_pos - open_paren_pos - 1),
                         ',')) {
        if (argument_entry.empty()) {
            continue;
        }
        auto argument = parse_typed_value(argument_entry);
        if (!argument.has_value()) {
            return std::nullopt;
        }
        spec.arguments.push_back(std::move(*argument));
    }
    return spec;
}

std::optional<AArch64LlvmImportExtractElementSpec>
parse_llvm_import_extractelement_spec(
    const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "extractelement") {
        return std::nullopt;
    }
    const std::vector<std::string> operands =
        split_top_level(payload_after_opcode(instruction), ',');
    if (operands.size() != 2) {
        return std::nullopt;
    }
    auto vector_value = parse_typed_value(operands[0]);
    auto index_value = parse_typed_value(operands[1]);
    if (!vector_value.has_value() || !index_value.has_value()) {
        return std::nullopt;
    }
    return AArch64LlvmImportExtractElementSpec{std::move(*vector_value),
                                               std::move(*index_value)};
}

std::optional<AArch64LlvmImportInsertElementSpec>
parse_llvm_import_insertelement_spec(
    const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "insertelement") {
        return std::nullopt;
    }
    const std::vector<std::string> operands =
        split_top_level(payload_after_opcode(instruction), ',');
    if (operands.size() != 3) {
        return std::nullopt;
    }
    auto vector_value = parse_typed_value(operands[0]);
    auto element_value = parse_typed_value(operands[1]);
    auto index_value = parse_typed_value(operands[2]);
    if (!vector_value.has_value() || !element_value.has_value() ||
        !index_value.has_value()) {
        return std::nullopt;
    }
    return AArch64LlvmImportInsertElementSpec{
        std::move(*vector_value), std::move(*element_value),
        std::move(*index_value)};
}

std::optional<AArch64LlvmImportShuffleVectorSpec>
parse_llvm_import_shufflevector_spec(
    const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "shufflevector") {
        return std::nullopt;
    }
    const std::vector<std::string> operands =
        split_top_level(payload_after_opcode(instruction), ',');
    if (operands.size() != 3) {
        return std::nullopt;
    }
    auto lhs_value = parse_typed_value(operands[0]);
    auto rhs_value = parse_typed_value(operands[1]);
    auto mask_value = parse_typed_value(operands[2]);
    if (!lhs_value.has_value() || !rhs_value.has_value() ||
        !mask_value.has_value()) {
        return std::nullopt;
    }
    return AArch64LlvmImportShuffleVectorSpec{
        std::move(*lhs_value), std::move(*rhs_value), std::move(*mask_value)};
}

std::optional<AArch64LlvmImportVectorReduceAddSpec>
parse_llvm_import_vector_reduce_add_spec(
    const AArch64LlvmImportInstruction &instruction) {
    if (instruction.kind != AArch64LlvmImportInstructionKind::VectorReduceAdd) {
        return std::nullopt;
    }
    const std::optional<AArch64LlvmImportCallSpec> call_spec =
        parse_llvm_import_call_spec(instruction);
    if (!call_spec.has_value() || call_spec->arguments.size() != 1) {
        return std::nullopt;
    }
    return AArch64LlvmImportVectorReduceAddSpec{
        call_spec->return_type_text, call_spec->return_type,
        call_spec->arguments.front()};
}

std::optional<AArch64LlvmImportSelectSpec>
parse_llvm_import_select_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "select") {
        return std::nullopt;
    }
    const std::vector<std::string> operands =
        split_top_level(payload_after_opcode(instruction), ',');
    if (operands.size() != 3) {
        return std::nullopt;
    }

    AArch64LlvmImportSelectSpec spec;
    auto condition = parse_typed_value(operands[0]);
    auto true_value = parse_typed_value(operands[1]);
    auto false_value = parse_typed_value(operands[2]);
    if (!condition.has_value() || !true_value.has_value() ||
        !false_value.has_value()) {
        return std::nullopt;
    }
    spec.condition = std::move(*condition);
    spec.true_value = std::move(*true_value);
    spec.false_value = std::move(*false_value);
    return spec;
}

std::optional<AArch64LlvmImportPhiSpec>
parse_llvm_import_phi_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "phi") {
        return std::nullopt;
    }
    const std::string payload = payload_after_opcode(instruction);
    std::size_t type_position = 0;
    const std::optional<std::string> type_text =
        consume_type_token(payload, type_position);
    if (!type_text.has_value()) {
        return std::nullopt;
    }

    AArch64LlvmImportPhiSpec spec;
    spec.type_text = *type_text;
    const auto parsed_type = parse_llvm_import_type_text(*type_text);
    if (!parsed_type.has_value()) {
        return std::nullopt;
    }
    spec.type = *parsed_type;
    for (const std::string &incoming_entry :
         split_top_level(trim_copy(payload.substr(type_position)), ',')) {
        const std::string trimmed_entry = trim_copy(incoming_entry);
        if (trimmed_entry.empty()) {
            continue;
        }
        if (trimmed_entry.front() != '[' || trimmed_entry.back() != ']') {
            return std::nullopt;
        }
        const std::vector<std::string> incoming_parts = split_top_level(
            trim_copy(trimmed_entry.substr(1, trimmed_entry.size() - 2)), ',');
        if (incoming_parts.size() != 2) {
            return std::nullopt;
        }
        std::string block_label = trim_copy(incoming_parts[1]);
        if (!block_label.empty() && block_label.front() == '%') {
            block_label.erase(block_label.begin());
        }
        auto incoming_value = parse_typed_value_with_known_type(
            spec.type_text, spec.type, incoming_parts[0]);
        if (!incoming_value.has_value()) {
            return std::nullopt;
        }
        spec.incoming_values.push_back(
            AArch64LlvmImportPhiIncoming{std::move(*incoming_value),
                                         std::move(block_label)});
    }
    return spec;
}

std::optional<AArch64LlvmImportBranchSpec>
parse_llvm_import_branch_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "br") {
        return std::nullopt;
    }
    const std::string payload = payload_after_opcode(instruction);

    AArch64LlvmImportBranchSpec spec;
    if (starts_with(payload, "label ")) {
        auto target = parse_branch_target(payload);
        if (!target.has_value()) {
            return std::nullopt;
        }
        spec.true_target_label = std::move(*target);
        return spec;
    }

    const std::vector<std::string> operands = split_top_level(payload, ',');
    if (operands.size() != 3) {
        return std::nullopt;
    }
    auto condition = parse_typed_value(operands[0]);
    auto true_target = parse_branch_target(operands[1]);
    auto false_target = parse_branch_target(operands[2]);
    if (!condition.has_value() || !true_target.has_value() ||
        !false_target.has_value()) {
        return std::nullopt;
    }
    spec.is_conditional = true;
    spec.condition = std::move(*condition);
    spec.true_target_label = std::move(*true_target);
    spec.false_target_label = std::move(*false_target);
    return spec;
}

std::optional<AArch64LlvmImportIndirectBranchSpec>
parse_llvm_import_indirect_branch_spec(
    const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "indirectbr") {
        return std::nullopt;
    }
    const std::vector<std::string> operands =
        split_top_level(payload_after_opcode(instruction), ',');
    if (operands.size() < 2) {
        return std::nullopt;
    }
    auto address = parse_typed_value(operands[0]);
    if (!address.has_value()) {
        return std::nullopt;
    }

    const std::string target_list_text = trim_copy(operands[1]);
    if (target_list_text.size() < 2 || target_list_text.front() != '[' ||
        target_list_text.back() != ']') {
        return std::nullopt;
    }

    AArch64LlvmImportIndirectBranchSpec spec;
    spec.address = std::move(*address);
    for (const std::string &entry : split_top_level(
             trim_copy(target_list_text.substr(1, target_list_text.size() - 2)),
             ',')) {
        auto target = parse_branch_target(entry);
        if (!target.has_value()) {
            return std::nullopt;
        }
        spec.target_labels.push_back(std::move(*target));
    }
    return spec.target_labels.empty() ? std::nullopt : std::optional(std::move(spec));
}

std::optional<AArch64LlvmImportSwitchSpec>
parse_llvm_import_switch_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.kind != AArch64LlvmImportInstructionKind::Switch) {
        return std::nullopt;
    }
    const std::string payload = payload_after_opcode(instruction);
    const std::size_t default_separator = payload.find(", label ");
    const std::size_t case_list_begin = payload.find('[', default_separator);
    const std::size_t case_list_end = payload.rfind(']');
    if (default_separator == std::string::npos ||
        case_list_begin == std::string::npos ||
        case_list_end == std::string::npos ||
        case_list_end < case_list_begin) {
        return std::nullopt;
    }

    auto selector = parse_typed_value(payload.substr(0, default_separator));
    if (!selector.has_value()) {
        return std::nullopt;
    }
    auto default_target = parse_branch_target(
        payload.substr(default_separator + 2,
                       case_list_begin - (default_separator + 2)));
    if (!default_target.has_value()) {
        return std::nullopt;
    }

    AArch64LlvmImportSwitchSpec spec;
    spec.selector = std::move(*selector);
    spec.default_target_label = std::move(*default_target);

    std::stringstream entries_stream(
        payload.substr(case_list_begin + 1,
                       case_list_end - case_list_begin - 1));
    std::string entry_line;
    while (std::getline(entries_stream, entry_line)) {
        const std::string trimmed = trim_copy(entry_line);
        if (trimmed.empty()) {
            continue;
        }
        const std::vector<std::string> parts = split_top_level(trimmed, ',');
        if (parts.size() != 2) {
            return std::nullopt;
        }
        auto case_value = parse_typed_value(parts[0]);
        auto target_label = parse_branch_target(parts[1]);
        if (!case_value.has_value() || !target_label.has_value() ||
            case_value->kind != AArch64LlvmImportValueKind::Constant ||
            case_value->constant.kind != AArch64LlvmImportConstantKind::Integer) {
            return std::nullopt;
        }
        spec.cases.push_back(AArch64LlvmImportSwitchCase{
            case_value->constant.integer_value, std::move(*target_label)});
    }
    return spec;
}

std::optional<AArch64LlvmImportReturnSpec>
parse_llvm_import_return_spec(const AArch64LlvmImportInstruction &instruction) {
    if (instruction.opcode_text != "ret") {
        return std::nullopt;
    }
    const std::string payload = payload_after_opcode(instruction);

    AArch64LlvmImportReturnSpec spec;
    if (payload == "void") {
        spec.is_void = true;
        return spec;
    }
    auto value = parse_typed_value(payload);
    if (!value.has_value()) {
        return std::nullopt;
    }
    spec.value = std::move(*value);
    return spec;
}

} // namespace sysycc
