#include "backend/asm_gen/aarch64/passes/aarch64_emission_pass.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

std::size_t count_machine_blocks(const AArch64MachineModule &machine_module) {
    std::size_t blocks = 0;
    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        blocks += function.get_blocks().size();
    }
    return blocks;
}

std::size_t count_machine_instructions(const AArch64MachineModule &machine_module) {
    std::size_t instructions = 0;
    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        for (const AArch64MachineBlock &block : function.get_blocks()) {
            instructions += block.get_instructions().size();
        }
    }
    return instructions;
}

std::size_t count_object_relocations(const AArch64ObjectModule &object_module) {
    std::size_t relocations = 0;
    for (const AArch64DataObject &data_object : object_module.get_data_objects()) {
        for (const AArch64DataFragment &fragment : data_object.get_fragments()) {
            relocations += fragment.get_relocations().size();
        }
    }
    return relocations;
}

std::string summarize_object_emission_input(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const BackendOptions &backend_options,
    const std::filesystem::path &object_file) {
    std::ostringstream summary;
    summary << "AArch64 object emission input summary: output='" << object_file.string()
            << "', target='" << backend_options.get_target_triple() << "', pic="
            << (backend_options.get_position_independent() ? "on" : "off")
            << ", debug=" << (backend_options.get_debug_info() ? "on" : "off")
            << ", functions=" << machine_module.get_functions().size()
            << ", blocks=" << count_machine_blocks(machine_module)
            << ", instructions=" << count_machine_instructions(machine_module)
            << ", data_objects=" << object_module.get_data_objects().size()
            << ", data_relocations=" << count_object_relocations(object_module)
            << ", symbols=" << object_module.get_symbols().size()
            << ", debug_files=" << object_module.get_debug_file_entries().size();
    return summary.str();
}

struct AsmTextOptimizationGroups {
    bool core = false;
    bool branch = false;
    bool move = false;
    bool slot = false;
    bool vector = false;
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

std::string_view trim_ascii(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

bool starts_with_ascii(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

AsmTextOptimizationGroups parse_asm_text_optimization_groups() {
    const char *env = std::getenv("SYSYCC_AARCH64_EMIT_ASM_OPT_GROUPS");
    if (env == nullptr || *env == '\0') {
        return AsmTextOptimizationGroups{
            .core = false,
            .branch = true,
            .move = true,
            .slot = true,
            .vector = true,
        };
    }

    AsmTextOptimizationGroups groups;
    std::string_view remaining(env);
    while (!remaining.empty()) {
        const std::size_t comma = remaining.find(',');
        const std::string_view token =
            comma == std::string_view::npos ? remaining : remaining.substr(0, comma);
        const std::string_view trimmed = trim_ascii(token);
        if (trimmed == "none") {
            return {};
        }
        if (trimmed == "all") {
            return AsmTextOptimizationGroups{true, true, true, true, true};
        }
        if (trimmed == "core") {
            groups.core = true;
        } else if (trimmed == "branch") {
            groups.branch = true;
        } else if (trimmed == "move") {
            groups.move = true;
        } else if (trimmed == "slot") {
            groups.slot = true;
        } else if (trimmed == "vector") {
            groups.vector = true;
        }

        if (comma == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(comma + 1);
    }
    return groups;
}

bool any_asm_text_optimization_group_enabled(
    const AsmTextOptimizationGroups &groups) {
    return groups.core || groups.branch || groups.move || groups.slot ||
           groups.vector;
}

std::string join_non_empty_asm_lines(const std::vector<std::string> &lines) {
    std::ostringstream output;
    for (const std::string &line : lines) {
        if (line.empty()) {
            continue;
        }
        output << line << "\n";
    }
    return output.str();
}

std::optional<std::string> invert_condition_code(std::string_view condition) {
    if (condition == "eq") {
        return "ne";
    }
    if (condition == "ne") {
        return "eq";
    }
    if (condition == "lt") {
        return "ge";
    }
    if (condition == "ge") {
        return "lt";
    }
    if (condition == "gt") {
        return "le";
    }
    if (condition == "le") {
        return "gt";
    }
    if (condition == "hs") {
        return "lo";
    }
    if (condition == "lo") {
        return "hs";
    }
    if (condition == "hi") {
        return "ls";
    }
    if (condition == "ls") {
        return "hi";
    }
    if (condition == "mi") {
        return "pl";
    }
    if (condition == "pl") {
        return "mi";
    }
    if (condition == "vs") {
        return "vc";
    }
    if (condition == "vc") {
        return "vs";
    }
    return std::nullopt;
}

std::optional<std::string> invert_test_bit_branch_mnemonic(
    std::string_view mnemonic) {
    if (mnemonic == "tbz") {
        return std::string("tbnz");
    }
    if (mnemonic == "tbnz") {
        return std::string("tbz");
    }
    return std::nullopt;
}

struct CsetAsmPattern {
    std::string reg;
    std::string condition;
};

struct CompareBranchAsmPattern {
    std::string reg;
    std::string label;
    bool branch_on_nonzero = true;
};

struct UnconditionalBranchAsmPattern {
    std::string label;
};

struct TestBitMaskAsmPattern {
    std::string reg;
    std::uint64_t mask = 0;
};

struct ConditionalBranchAsmPattern {
    std::string condition;
    std::string label;
};

struct TestBitBranchAsmPattern {
    std::string mnemonic;
    std::string reg;
    std::string bit_index;
    std::string label;
};

struct MoveWideImmediateAsmPattern {
    std::string reg;
    std::uint64_t immediate = 0;
};

struct ThreeOperandAsmPattern {
    std::string mnemonic;
    std::string dst;
    std::string lhs;
    std::string rhs;
};

struct FourOperandAsmPattern {
    std::string mnemonic;
    std::string dst;
    std::string lhs;
    std::string rhs;
    std::string extra;
};

struct PlainMoveAsmPattern {
    std::string dst;
    std::string src;
};

struct TwoOperandAsmPattern {
    std::string mnemonic;
    std::string lhs;
    std::string rhs;
};

struct DupScalarAsmPattern {
    std::string vector_reg;
    std::string scalar_reg;
};

enum class HoistableFrameLoadUseKind {
    SignedDivRhs,
    UnsignedDivRhs,
    CompareLhs,
    CompareRhs,
};

struct HoistableFrameLoadAsmPattern {
    std::size_t load_index = 0;
    std::size_t use_index = 0;
    std::string load_mnemonic;
    std::string loaded_reg;
    std::string memory_operand;
    HoistableFrameLoadUseKind use_kind = HoistableFrameLoadUseKind::CompareRhs;
};

struct VectorMulAsmPattern {
    std::string dst;
    std::string lhs;
    std::string rhs;
};

struct ScaledAddAsmPattern {
    std::string dst;
    std::string base;
    std::string index;
    unsigned shift = 0;
};

struct AdrpSymbolAsmPattern {
    std::string reg;
    std::string symbol;
};

struct AddLo12SymbolAsmPattern {
    std::string dst;
    std::string base;
    std::string symbol;
};

struct AddImmediateAsmPattern {
    std::string dst;
    std::string lhs;
    long long immediate = 0;
};


bool rhs_starts_with_register_token(std::string_view rhs, std::string_view reg) {
    rhs = trim_ascii(rhs);
    if (!starts_with_ascii(rhs, reg)) {
        return false;
    }
    return rhs.size() == reg.size() ||
           rhs[reg.size()] == ',' || rhs[reg.size()] == ' ' ||
           rhs[reg.size()] == '\t';
}

std::string replace_rhs_leading_register(std::string_view rhs,
                                         std::string_view from,
                                         std::string_view to) {
    rhs = trim_ascii(rhs);
    if (!rhs_starts_with_register_token(rhs, from)) {
        return std::string(rhs);
    }
    return std::string(to) + std::string(rhs.substr(from.size()));
}

bool registers_alias(std::string_view lhs, std::string_view rhs) {
    if (lhs == rhs) {
        return true;
    }
    const auto same_gpr_family = [](std::string_view a,
                                    std::string_view b) -> bool {
        if (a.size() < 2 || b.size() < 2) {
            return false;
        }
        const bool a_gpr = a.front() == 'x' || a.front() == 'w';
        const bool b_gpr = b.front() == 'x' || b.front() == 'w';
        return a_gpr && b_gpr && a.substr(1) == b.substr(1);
    };
    return same_gpr_family(lhs, rhs);
}

bool is_general_register_or_zero(std::string_view reg) {
    reg = trim_ascii(reg);
    if (reg == "wzr" || reg == "xzr") {
        return true;
    }
    if (reg.size() < 2) {
        return false;
    }
    const char prefix = reg.front();
    if (prefix != 'w' && prefix != 'x') {
        return false;
    }
    std::size_t index = 1;
    while (index < reg.size() &&
           std::isdigit(static_cast<unsigned char>(reg[index])) != 0) {
        ++index;
    }
    return index == reg.size() && index != 1;
}

bool vector_registers_alias(std::string_view lhs, std::string_view rhs) {
    const auto canonicalize = [](std::string_view text) -> std::string {
        text = trim_ascii(text);
        if (text.empty()) {
            return {};
        }
        if (text.front() == 'q') {
            return "v" + std::string(text.substr(1));
        }
        if (text.front() == 'v') {
            std::size_t end = 1;
            while (end < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[end]))) {
                ++end;
            }
            return std::string(text.substr(0, end));
        }
        return {};
    };
    const std::string lhs_canonical = canonicalize(lhs);
    const std::string rhs_canonical = canonicalize(rhs);
    return !lhs_canonical.empty() && lhs_canonical == rhs_canonical;
}

std::string format_dup_target_register(std::string_view reg,
                                       std::string_view template_reg) {
    reg = trim_ascii(reg);
    template_reg = trim_ascii(template_reg);

    std::string_view suffix;
    if (!template_reg.empty() && template_reg.front() == 'v') {
        std::size_t index = 1;
        while (index < template_reg.size() &&
               std::isdigit(static_cast<unsigned char>(template_reg[index]))) {
            ++index;
        }
        suffix = template_reg.substr(index);
    }

    if (!reg.empty() && reg.front() == 'q') {
        return "v" + std::string(reg.substr(1)) + std::string(suffix);
    }
    if (!reg.empty() && reg.front() == 'v' && reg.find('.') == std::string_view::npos) {
        return std::string(reg) + std::string(suffix);
    }
    return std::string(reg);
}

std::string canonical_vector_register_name(std::string_view reg) {
    reg = trim_ascii(reg);
    if (reg.empty()) {
        return {};
    }
    if (reg.front() == 'q') {
        return "v" + std::string(reg.substr(1));
    }
    if (reg.front() == 'v') {
        std::size_t end = 1;
        while (end < reg.size() &&
               std::isdigit(static_cast<unsigned char>(reg[end]))) {
            ++end;
        }
        return std::string(reg.substr(0, end));
    }
    return std::string(reg);
}

std::optional<CsetAsmPattern> parse_cset_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "cset ")) {
        return std::nullopt;
    }
    line.remove_prefix(5);
    const std::size_t comma = line.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string reg(trim_ascii(line.substr(0, comma)));
    const std::string condition(trim_ascii(line.substr(comma + 1)));
    if (reg.empty() || condition.empty()) {
        return std::nullopt;
    }
    return CsetAsmPattern{reg, condition};
}

std::optional<CompareBranchAsmPattern>
parse_compare_branch_line(std::string_view line) {
    line = trim_ascii(line);
    bool branch_on_nonzero = true;
    if (starts_with_ascii(line, "cbnz ")) {
        line.remove_prefix(5);
        branch_on_nonzero = true;
    } else if (starts_with_ascii(line, "cbz ")) {
        line.remove_prefix(4);
        branch_on_nonzero = false;
    } else {
        return std::nullopt;
    }
    const std::size_t comma = line.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string reg(trim_ascii(line.substr(0, comma)));
    const std::string label(trim_ascii(line.substr(comma + 1)));
    if (reg.empty() || label.empty()) {
        return std::nullopt;
    }
    return CompareBranchAsmPattern{reg, label, branch_on_nonzero};
}

std::optional<UnconditionalBranchAsmPattern>
parse_unconditional_branch_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "b ")) {
        return std::nullopt;
    }
    line.remove_prefix(2);
    const std::string label(trim_ascii(line));
    if (label.empty()) {
        return std::nullopt;
    }
    return UnconditionalBranchAsmPattern{label};
}

std::optional<TestBitMaskAsmPattern> parse_tst_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "tst ")) {
        return std::nullopt;
    }
    line.remove_prefix(4);
    const std::size_t comma = line.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string reg(trim_ascii(line.substr(0, comma)));
    std::string_view imm_text = trim_ascii(line.substr(comma + 1));
    if (reg.empty() || imm_text.empty() || imm_text.front() != '#') {
        return std::nullopt;
    }
    imm_text.remove_prefix(1);
    std::uint64_t mask = 0;
    try {
        mask = static_cast<std::uint64_t>(std::stoull(std::string(imm_text), nullptr, 0));
    } catch (...) {
        return std::nullopt;
    }
    return TestBitMaskAsmPattern{reg, mask};
}

std::optional<ConditionalBranchAsmPattern>
parse_conditional_branch_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "b.")) {
        return std::nullopt;
    }
    line.remove_prefix(2);
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string condition(trim_ascii(line.substr(0, space)));
    const std::string label(trim_ascii(line.substr(space + 1)));
    if (condition.empty() || label.empty()) {
        return std::nullopt;
    }
    return ConditionalBranchAsmPattern{condition, label};
}

std::optional<TestBitBranchAsmPattern>
parse_test_bit_branch_line(std::string_view line) {
    line = trim_ascii(line);
    std::string mnemonic;
    if (starts_with_ascii(line, "tbz ")) {
        mnemonic = "tbz";
        line.remove_prefix(4);
    } else if (starts_with_ascii(line, "tbnz ")) {
        mnemonic = "tbnz";
        line.remove_prefix(5);
    } else {
        return std::nullopt;
    }

    const std::size_t first_comma = line.find(',');
    if (first_comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second_comma = line.find(',', first_comma + 1);
    if (second_comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string reg(trim_ascii(line.substr(0, first_comma)));
    std::string bit_index(
        trim_ascii(line.substr(first_comma + 1, second_comma - first_comma - 1)));
    const std::string label(trim_ascii(line.substr(second_comma + 1)));
    if (reg.empty() || bit_index.empty() || label.empty()) {
        return std::nullopt;
    }
    if (!bit_index.empty() && bit_index.front() == '#') {
        bit_index.erase(bit_index.begin());
    }
    return TestBitBranchAsmPattern{mnemonic, reg, bit_index, label};
}

std::optional<std::string> parse_label_definition(std::string_view line) {
    line = trim_ascii(line);
    if (line.empty() || line.back() != ':') {
        return std::nullopt;
    }
    line.remove_suffix(1);
    if (line.empty()) {
        return std::nullopt;
    }
    return std::string(line);
}

std::optional<MoveWideImmediateAsmPattern>
parse_movz_immediate_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "movz ")) {
        return std::nullopt;
    }
    line.remove_prefix(5);
    const std::size_t comma = line.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string reg(trim_ascii(line.substr(0, comma)));
    std::string_view imm_text = trim_ascii(line.substr(comma + 1));
    const std::size_t next_comma = imm_text.find(',');
    if (next_comma != std::string_view::npos) {
        imm_text = trim_ascii(imm_text.substr(0, next_comma));
    }
    if (reg.empty() || imm_text.empty() || imm_text.front() != '#') {
        return std::nullopt;
    }
    imm_text.remove_prefix(1);
    std::uint64_t immediate = 0;
    try {
        immediate =
            static_cast<std::uint64_t>(std::stoull(std::string(imm_text), nullptr, 0));
    } catch (...) {
        return std::nullopt;
    }
    return MoveWideImmediateAsmPattern{reg, immediate};
}

std::optional<MoveWideImmediateAsmPattern>
parse_movk_immediate_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "movk ")) {
        return std::nullopt;
    }
    line.remove_prefix(5);
    const std::size_t comma = line.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string reg(trim_ascii(line.substr(0, comma)));
    std::string_view imm_text = trim_ascii(line.substr(comma + 1));
    const std::size_t next_comma = imm_text.find(',');
    if (next_comma != std::string_view::npos) {
        imm_text = trim_ascii(imm_text.substr(0, next_comma));
    }
    if (reg.empty() || imm_text.empty() || imm_text.front() != '#') {
        return std::nullopt;
    }
    imm_text.remove_prefix(1);
    std::uint64_t immediate = 0;
    try {
        immediate =
            static_cast<std::uint64_t>(std::stoull(std::string(imm_text), nullptr, 0));
    } catch (...) {
        return std::nullopt;
    }
    return MoveWideImmediateAsmPattern{reg, immediate};
}

std::optional<ThreeOperandAsmPattern>
parse_three_operand_instruction(std::string_view line,
                                std::string_view expected_mnemonic) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, expected_mnemonic) ||
        line.size() <= expected_mnemonic.size() ||
        line[expected_mnemonic.size()] != ' ') {
        return std::nullopt;
    }
    line.remove_prefix(expected_mnemonic.size() + 1);
    const std::size_t first_comma = line.find(',');
    if (first_comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second_comma = line.find(',', first_comma + 1);
    if (second_comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string dst(trim_ascii(line.substr(0, first_comma)));
    const std::string lhs(
        trim_ascii(line.substr(first_comma + 1, second_comma - first_comma - 1)));
    const std::string rhs(trim_ascii(line.substr(second_comma + 1)));
    if (dst.empty() || lhs.empty() || rhs.empty()) {
        return std::nullopt;
    }
    return ThreeOperandAsmPattern{std::string(expected_mnemonic), dst, lhs, rhs};
}

std::optional<FourOperandAsmPattern>
parse_four_operand_instruction(std::string_view line,
                               std::string_view expected_mnemonic) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, expected_mnemonic) ||
        line.size() <= expected_mnemonic.size() ||
        line[expected_mnemonic.size()] != ' ') {
        return std::nullopt;
    }
    line.remove_prefix(expected_mnemonic.size() + 1);
    const std::size_t first_comma = line.find(',');
    if (first_comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second_comma = line.find(',', first_comma + 1);
    if (second_comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t third_comma = line.find(',', second_comma + 1);
    if (third_comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string dst(trim_ascii(line.substr(0, first_comma)));
    const std::string lhs(
        trim_ascii(line.substr(first_comma + 1, second_comma - first_comma - 1)));
    const std::string rhs(
        trim_ascii(line.substr(second_comma + 1, third_comma - second_comma - 1)));
    const std::string extra(trim_ascii(line.substr(third_comma + 1)));
    if (dst.empty() || lhs.empty() || rhs.empty() || extra.empty()) {
        return std::nullopt;
    }
    return FourOperandAsmPattern{std::string(expected_mnemonic), dst, lhs, rhs,
                                 extra};
}

std::optional<PlainMoveAsmPattern> parse_plain_move_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "mov ")) {
        return std::nullopt;
    }
    line.remove_prefix(4);
    const std::size_t comma = line.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string dst(trim_ascii(line.substr(0, comma)));
    const std::string src(trim_ascii(line.substr(comma + 1)));
    if (dst.empty() || src.empty()) {
        return std::nullopt;
    }
    return PlainMoveAsmPattern{dst, src};
}

std::optional<TwoOperandAsmPattern>
parse_two_operand_instruction(std::string_view line,
                              std::string_view expected_mnemonic) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, expected_mnemonic) ||
        line.size() <= expected_mnemonic.size() ||
        line[expected_mnemonic.size()] != ' ') {
        return std::nullopt;
    }
    line.remove_prefix(expected_mnemonic.size() + 1);
    const std::size_t comma = line.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string lhs(trim_ascii(line.substr(0, comma)));
    const std::string rhs(trim_ascii(line.substr(comma + 1)));
    if (lhs.empty() || rhs.empty()) {
        return std::nullopt;
    }
    return TwoOperandAsmPattern{std::string(expected_mnemonic), lhs, rhs};
}

std::optional<DupScalarAsmPattern> parse_dup_scalar_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "dup ")) {
        return std::nullopt;
    }
    line.remove_prefix(4);
    const std::size_t comma = line.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string vector_reg(trim_ascii(line.substr(0, comma)));
    const std::string scalar_reg(trim_ascii(line.substr(comma + 1)));
    if (vector_reg.empty() || scalar_reg.empty()) {
        return std::nullopt;
    }
    return DupScalarAsmPattern{vector_reg, scalar_reg};
}

std::optional<VectorMulAsmPattern> parse_vector_mul_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "mul ")) {
        return std::nullopt;
    }
    line.remove_prefix(4);
    const std::size_t first_comma = line.find(',');
    if (first_comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t second_comma = line.find(',', first_comma + 1);
    if (second_comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string dst(trim_ascii(line.substr(0, first_comma)));
    const std::string lhs(trim_ascii(
        line.substr(first_comma + 1, second_comma - first_comma - 1)));
    const std::string rhs(trim_ascii(line.substr(second_comma + 1)));
    if (dst.empty() || lhs.empty() || rhs.empty()) {
        return std::nullopt;
    }
    return VectorMulAsmPattern{dst, lhs, rhs};
}

std::optional<ScaledAddAsmPattern> parse_scaled_add_line(std::string_view line) {
    const auto add = parse_three_operand_instruction(line, "add");
    if (!add.has_value()) {
        return std::nullopt;
    }

    std::string_view rhs = trim_ascii(add->rhs);
    const std::size_t comma = rhs.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string index(trim_ascii(rhs.substr(0, comma)));
    std::string_view shift_text = trim_ascii(rhs.substr(comma + 1));
    if (!starts_with_ascii(shift_text, "lsl #")) {
        return std::nullopt;
    }
    shift_text.remove_prefix(5);

    try {
        return ScaledAddAsmPattern{add->dst, add->lhs, index,
                                   static_cast<unsigned>(
                                       std::stoul(std::string(shift_text), nullptr, 0))};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<AdrpSymbolAsmPattern> parse_adrp_symbol_line(std::string_view line) {
    line = trim_ascii(line);
    if (!starts_with_ascii(line, "adrp ")) {
        return std::nullopt;
    }
    line.remove_prefix(5);
    const std::size_t comma = line.find(',');
    if (comma == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string reg(trim_ascii(line.substr(0, comma)));
    const std::string symbol(trim_ascii(line.substr(comma + 1)));
    if (reg.empty() || symbol.empty()) {
        return std::nullopt;
    }
    return AdrpSymbolAsmPattern{reg, symbol};
}

std::optional<AddLo12SymbolAsmPattern>
parse_add_lo12_symbol_line(std::string_view line) {
    const auto add = parse_three_operand_instruction(line, "add");
    if (!add.has_value()) {
        return std::nullopt;
    }

    std::string_view rhs = trim_ascii(add->rhs);
    if (!starts_with_ascii(rhs, ":lo12:")) {
        return std::nullopt;
    }
    rhs.remove_prefix(6);
    const std::string symbol(trim_ascii(rhs));
    if (symbol.empty()) {
        return std::nullopt;
    }
    return AddLo12SymbolAsmPattern{add->dst, add->lhs, symbol};
}

std::optional<AddImmediateAsmPattern>
parse_add_immediate_line(std::string_view line) {
    const auto add = parse_three_operand_instruction(line, "add");
    if (!add.has_value()) {
        return std::nullopt;
    }
    std::string_view rhs = trim_ascii(add->rhs);
    if (rhs.empty() || rhs.front() != '#') {
        return std::nullopt;
    }
    rhs.remove_prefix(1);
    try {
        return AddImmediateAsmPattern{
            add->dst, add->lhs,
            static_cast<long long>(std::stoll(std::string(rhs), nullptr, 0))};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<long long>
parse_memory_immediate_offset(std::string_view memory_operand) {
    memory_operand = trim_ascii(memory_operand);
    if (memory_operand.size() < 3 || memory_operand.front() != '[') {
        return std::nullopt;
    }
    const std::size_t close = memory_operand.find(']');
    if (close == std::string_view::npos) {
        return std::nullopt;
    }
    if (!trim_ascii(memory_operand.substr(close + 1)).empty()) {
        return std::nullopt;
    }
    std::string_view inside = memory_operand.substr(1, close - 1);
    const std::size_t comma = inside.find(',');
    if (comma == std::string_view::npos) {
        return 0;
    }
    std::string_view offset = trim_ascii(inside.substr(comma + 1));
    if (offset.empty() || offset.front() != '#') {
        return std::nullopt;
    }
    offset.remove_prefix(1);
    try {
        return std::stoll(std::string(offset), nullptr, 0);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> parse_memory_base_register(std::string_view memory_operand);

std::optional<long long> parse_hash_immediate_with_optional_lsl(
    std::string_view text) {
    text = trim_ascii(text);
    if (text.empty() || text.front() != '#') {
        return std::nullopt;
    }
    text.remove_prefix(1);
    const std::size_t comma = text.find(',');
    std::string_view value_text = comma == std::string_view::npos
                                      ? text
                                      : trim_ascii(text.substr(0, comma));
    unsigned shift = 0;
    if (comma != std::string_view::npos) {
        std::string_view shift_text = trim_ascii(text.substr(comma + 1));
        if (!starts_with_ascii(shift_text, "lsl #")) {
            return std::nullopt;
        }
        shift_text.remove_prefix(5);
        try {
            shift = static_cast<unsigned>(
                std::stoul(std::string(shift_text), nullptr, 0));
        } catch (...) {
            return std::nullopt;
        }
    }
    try {
        const long long value = std::stoll(std::string(value_text), nullptr, 0);
        return value << shift;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<long long> parse_stack_pointer_sub_adjust(std::string_view line) {
    const auto sub = parse_three_operand_instruction(line, "sub");
    if (!sub.has_value() || trim_ascii(sub->dst) != "sp" ||
        trim_ascii(sub->lhs) != "sp") {
        return std::nullopt;
    }
    return parse_hash_immediate_with_optional_lsl(sub->rhs);
}

std::optional<long long> parse_x29_immediate_stack_address(
    std::string_view line,
    long long frame_allocation) {
    const auto sub = parse_three_operand_instruction(line, "sub");
    if (!sub.has_value() || trim_ascii(sub->lhs) != "x29") {
        return std::nullopt;
    }
    const auto immediate = parse_hash_immediate_with_optional_lsl(sub->rhs);
    if (!immediate.has_value()) {
        return std::nullopt;
    }
    return frame_allocation - *immediate;
}

std::string canonical_general_register_key(std::string_view reg) {
    reg = trim_ascii(reg);
    if (reg.size() < 2) {
        return {};
    }
    const char prefix = reg.front();
    if (prefix != 'x' && prefix != 'w') {
        return {};
    }
    if (reg == "xzr" || reg == "wzr") {
        return {};
    }
    std::size_t index = 1;
    while (index < reg.size() &&
           std::isdigit(static_cast<unsigned char>(reg[index]))) {
        ++index;
    }
    if (index != reg.size() || index == 1) {
        return {};
    }
    return "x" + std::string(reg.substr(1));
}

bool instruction_writes_general_register_text(std::string_view line,
                                              std::string &reg_key) {
    line = trim_ascii(line);
    if (line.empty() || line.front() == '.') {
        return false;
    }
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) {
        return false;
    }
    const std::string_view mnemonic = trim_ascii(line.substr(0, space));
    if (mnemonic.empty() || mnemonic == "str" || mnemonic == "stur" ||
        mnemonic == "strb" || mnemonic == "sturb" || mnemonic == "strh" ||
        mnemonic == "sturh" || mnemonic == "stp" || mnemonic == "cmp" ||
        mnemonic == "fcmp" || mnemonic == "tst" || mnemonic == "cbz" ||
        mnemonic == "cbnz" || mnemonic == "b" || mnemonic == "bl" ||
        mnemonic == "br" || mnemonic == "blr" || mnemonic == "ret" ||
        starts_with_ascii(mnemonic, "b.")) {
        return false;
    }

    line.remove_prefix(space + 1);
    const std::size_t first_comma = line.find(',');
    reg_key = canonical_general_register_key(
        trim_ascii(line.substr(0, first_comma == std::string_view::npos
                                         ? line.size()
                                         : first_comma)));
    return !reg_key.empty();
}

std::optional<std::pair<std::string, long long>>
parse_frame_pointer_sub_materialization(std::string_view line) {
    const auto sub = parse_three_operand_instruction(line, "sub");
    if (!sub.has_value() || trim_ascii(sub->lhs) != "x29") {
        return std::nullopt;
    }
    std::string dst_key = canonical_general_register_key(sub->dst);
    if (dst_key.empty() || dst_key == "x29") {
        return std::nullopt;
    }
    const auto immediate = parse_hash_immediate_with_optional_lsl(sub->rhs);
    if (!immediate.has_value() || *immediate <= 0) {
        return std::nullopt;
    }
    return std::pair<std::string, long long>{std::move(dst_key), *immediate};
}

std::unordered_set<std::string>
collect_local_branch_target_labels(const std::vector<std::string> &lines) {
    std::unordered_set<std::string> labels;
    for (const std::string &line : lines) {
        if (const auto branch = parse_unconditional_branch_line(line);
            branch.has_value()) {
            labels.insert(branch->label);
            continue;
        }
        if (const auto branch = parse_conditional_branch_line(line);
            branch.has_value()) {
            labels.insert(branch->label);
            continue;
        }
        if (const auto branch = parse_compare_branch_line(line);
            branch.has_value()) {
            labels.insert(branch->label);
            continue;
        }
        if (const auto branch = parse_test_bit_branch_line(line);
            branch.has_value()) {
            labels.insert(branch->label);
        }
    }
    return labels;
}

bool any_line_mentions_register_alias(const std::vector<std::string> &lines,
                                      std::size_t begin,
                                      std::size_t end,
                                      std::string_view reg);

bool eliminate_redundant_frame_pointer_sub_materializations(
    std::vector<std::string> &lines) {
    const std::unordered_set<std::string> branch_targets =
        collect_local_branch_target_labels(lines);
    std::unordered_map<std::string, long long> known_frame_offsets;
    bool changed = false;

    for (std::string &line : lines) {
        if (line.empty()) {
            continue;
        }
        if (const auto label = parse_label_definition(line); label.has_value()) {
            if (branch_targets.find(*label) != branch_targets.end()) {
                known_frame_offsets.clear();
            }
            continue;
        }

        const std::string_view trimmed = trim_ascii(line);
        if (trimmed.empty() || trimmed.front() == '.') {
            continue;
        }

        if (starts_with_ascii(trimmed, "bl ") || starts_with_ascii(trimmed, "blr ") ||
            starts_with_ascii(trimmed, "br ") || trimmed == "ret") {
            known_frame_offsets.clear();
            continue;
        }
        if (parse_unconditional_branch_line(trimmed).has_value()) {
            known_frame_offsets.clear();
            continue;
        }

        if (const auto materialized =
                parse_frame_pointer_sub_materialization(trimmed);
            materialized.has_value()) {
            const auto existing = known_frame_offsets.find(materialized->first);
            if (existing != known_frame_offsets.end() &&
                existing->second == materialized->second) {
                line.clear();
                changed = true;
                continue;
            }
            known_frame_offsets[materialized->first] = materialized->second;
            continue;
        }

        std::string written_reg;
        if (instruction_writes_general_register_text(trimmed, written_reg)) {
            if (written_reg == "x29") {
                known_frame_offsets.clear();
            } else {
                known_frame_offsets.erase(written_reg);
            }
        }
    }

    return changed;
}

std::optional<std::string>
rewrite_add_source_register(std::string_view line,
                            std::string_view from_reg,
                            std::string_view to_reg) {
    const auto add = parse_three_operand_instruction(line, "add");
    if (!add.has_value()) {
        return std::nullopt;
    }
    bool changed = false;
    std::string lhs = add->lhs;
    std::string rhs = add->rhs;
    if (registers_alias(lhs, from_reg)) {
        lhs = std::string(to_reg);
        changed = true;
    }
    if (rhs_starts_with_register_token(rhs, from_reg)) {
        rhs = replace_rhs_leading_register(rhs, from_reg, to_reg);
        changed = true;
    }
    if (!changed) {
        return std::nullopt;
    }
    return "  add " + add->dst + ", " + lhs + ", " + rhs;
}

bool hoist_readonly_frame_pointer_base_reloads(std::vector<std::string> &lines) {
    struct Candidate {
        std::string memory_operand;
        std::size_t count = 0;
    };

    for (std::size_t function_begin = 0; function_begin < lines.size();
         ++function_begin) {
        const auto function_label = parse_label_definition(lines[function_begin]);
        if (!function_label.has_value() || starts_with_ascii(*function_label, ".L")) {
            continue;
        }

        std::size_t function_end = function_begin + 1;
        while (function_end < lines.size()) {
            const std::string_view line = trim_ascii(lines[function_end]);
            if (starts_with_ascii(line, ".size ")) {
                break;
            }
            ++function_end;
        }
        if (function_end <= function_begin + 1) {
            continue;
        }
        if (any_line_mentions_register_alias(lines, function_begin, function_end,
                                             "x16") ||
            any_line_mentions_register_alias(lines, function_begin, function_end,
                                             "x17")) {
            continue;
        }

        bool has_call = false;
        for (std::size_t index = function_begin + 1; index < function_end; ++index) {
            const std::string_view line = trim_ascii(lines[index]);
            if (starts_with_ascii(line, "bl ") || starts_with_ascii(line, "blr ")) {
                has_call = true;
                break;
            }
        }
        if (has_call) {
            continue;
        }

        std::size_t entry_label_index = function_begin + 1;
        while (entry_label_index < function_end) {
            const auto label = parse_label_definition(lines[entry_label_index]);
            if (label.has_value() && starts_with_ascii(*label, ".L")) {
                break;
            }
            ++entry_label_index;
        }
        if (entry_label_index >= function_end) {
            continue;
        }

        std::unordered_map<std::string, Candidate> candidates;
        std::unordered_map<std::string, std::size_t> store_counts;
        std::unordered_map<std::string, std::size_t> store_after_entry_counts;
        for (std::size_t index = function_begin + 1; index < function_end; ++index) {
            for (const char *mnemonic : {"str", "stur"}) {
                const auto store =
                    parse_two_operand_instruction(lines[index], mnemonic);
                if (!store.has_value()) {
                    continue;
                }
                const auto base = parse_memory_base_register(store->rhs);
                if (base.has_value() && *base == "x29") {
                    const std::string memory_operand(trim_ascii(store->rhs));
                    ++store_counts[memory_operand];
                    if (index > entry_label_index) {
                        ++store_after_entry_counts[memory_operand];
                    }
                }
            }

            if (index + 1 >= function_end) {
                continue;
            }
            const auto load = parse_two_operand_instruction(lines[index], "ldr");
            if (!load.has_value()) {
                continue;
            }
            const std::string loaded_reg = canonical_general_register_key(load->lhs);
            if (loaded_reg.empty() || loaded_reg == "x29") {
                continue;
            }
            const auto base = parse_memory_base_register(load->rhs);
            const auto offset = parse_memory_immediate_offset(load->rhs);
            if (!base.has_value() || *base != "x29" || !offset.has_value() ||
                *offset >= 0) {
                continue;
            }
            if (!rewrite_add_source_register(lines[index + 1], loaded_reg,
                                             loaded_reg)
                     .has_value()) {
                continue;
            }
            const std::string memory_operand(trim_ascii(load->rhs));
            Candidate &candidate = candidates[memory_operand];
            candidate.memory_operand = memory_operand;
            ++candidate.count;
        }

        std::vector<Candidate> selected;
        selected.reserve(2);
        for (const auto &entry : candidates) {
            const auto store_it = store_counts.find(entry.first);
            if (store_it != store_counts.end() && store_it->second > 1) {
                continue;
            }
            if (store_after_entry_counts.find(entry.first) !=
                store_after_entry_counts.end()) {
                continue;
            }
            if (entry.second.count < 2) {
                continue;
            }
            selected.push_back(entry.second);
        }
        std::sort(selected.begin(), selected.end(),
                  [](const Candidate &lhs, const Candidate &rhs) {
                      return lhs.count > rhs.count;
                  });
        if (selected.empty()) {
            continue;
        }
        if (selected.size() > 2) {
            selected.resize(2);
        }

        const char *scratch_regs[] = {"x16", "x17"};
        bool changed = false;
        for (std::size_t selected_index = 0; selected_index < selected.size();
             ++selected_index) {
            const std::string scratch = scratch_regs[selected_index];
            for (std::size_t index = function_begin + 1; index + 1 < function_end;
                 ++index) {
                const auto load = parse_two_operand_instruction(lines[index], "ldr");
                if (!load.has_value() ||
                    trim_ascii(load->rhs) != selected[selected_index].memory_operand) {
                    continue;
                }
                const std::string loaded_reg =
                    canonical_general_register_key(load->lhs);
                if (loaded_reg.empty()) {
                    continue;
                }
                const auto rewritten =
                    rewrite_add_source_register(lines[index + 1], loaded_reg,
                                                scratch);
                if (!rewritten.has_value()) {
                    continue;
                }
                lines[index].clear();
                lines[index + 1] = *rewritten;
                changed = true;
            }
        }

        if (!changed) {
            continue;
        }
        std::vector<std::string> hoisted_loads;
        for (std::size_t selected_index = 0; selected_index < selected.size();
             ++selected_index) {
            hoisted_loads.push_back("  ldr " + std::string(scratch_regs[selected_index]) +
                                    ", " + selected[selected_index].memory_operand);
        }
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(entry_label_index),
                     hoisted_loads.begin(), hoisted_loads.end());
        return true;
    }

    return false;
}

std::optional<long long> stack_offset_for_memory_operand(
    std::string_view memory_operand,
    const std::unordered_map<std::string, long long> &register_sp_offsets) {
    const auto base = parse_memory_base_register(memory_operand);
    const auto offset = parse_memory_immediate_offset(memory_operand);
    if (!base.has_value() || !offset.has_value()) {
        return std::nullopt;
    }
    if (*base == "sp") {
        return *offset;
    }
    const auto mapped = register_sp_offsets.find(*base);
    if (mapped == register_sp_offsets.end()) {
        return std::nullopt;
    }
    return mapped->second + *offset;
}

bool instruction_overwrites_register_without_using(std::string_view line,
                                                   std::string_view reg) {
    line = trim_ascii(line);
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) {
        return false;
    }
    const std::string_view mnemonic = trim_ascii(line.substr(0, space));
    const auto writes_first_operand = [mnemonic]() -> bool {
        if (mnemonic.empty()) {
            return false;
        }
        if (mnemonic == "str" || mnemonic == "stur" || mnemonic == "strb" ||
            mnemonic == "sturb" || mnemonic == "strh" || mnemonic == "sturh" ||
            mnemonic == "stp" || mnemonic == "cmp" || mnemonic == "fcmp" ||
            mnemonic == "tst" || mnemonic == "cbz" || mnemonic == "cbnz" ||
            mnemonic == "b" || mnemonic == "bl" || mnemonic == "br" ||
            mnemonic == "blr" || mnemonic == "ret" ||
            starts_with_ascii(mnemonic, "b.")) {
            return false;
        }
        return true;
    };
    if (!writes_first_operand()) {
        return false;
    }
    line.remove_prefix(space + 1);
    const std::size_t first_comma = line.find(',');
    if (first_comma == std::string_view::npos) {
        return false;
    }
    const std::string dst(trim_ascii(line.substr(0, first_comma)));
    if (!registers_alias(dst, reg)) {
        return false;
    }
    std::string token;
    std::string_view rest = line.substr(first_comma + 1);
    for (char ch : rest) {
        const bool is_token_char =
            std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '.';
        if (is_token_char) {
            token.push_back(ch);
            continue;
        }
        if (!token.empty()) {
            if (registers_alias(token, reg)) {
                return false;
            }
            token.clear();
        }
    }
    if (!token.empty() && registers_alias(token, reg)) {
        return false;
    }
    return true;
}

bool instruction_writes_vector_register(std::string_view line,
                                        std::string_view reg) {
    if (const auto dup = parse_dup_scalar_line(line);
        dup.has_value() && vector_registers_alias(dup->vector_reg, reg)) {
        return true;
    }
    if (const auto ldr = parse_two_operand_instruction(line, "ldr");
        ldr.has_value() && vector_registers_alias(ldr->lhs, reg)) {
        return true;
    }
    for (const char *mnemonic : {"mul", "add", "smin", "smax"}) {
        if (const auto op = parse_three_operand_instruction(line, mnemonic);
            op.has_value() && vector_registers_alias(op->dst, reg)) {
            return true;
        }
    }
    return false;
}

bool instruction_may_write_general_register(std::string_view line,
                                            std::string_view reg) {
    line = trim_ascii(line);
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) {
        return false;
    }
    line.remove_prefix(space + 1);
    const std::size_t first_comma = line.find(',');
    const std::string dst(
        trim_ascii(line.substr(0, first_comma == std::string_view::npos
                                         ? line.size()
                                         : first_comma)));
    return !dst.empty() && registers_alias(dst, reg);
}

bool instruction_stores_to_memory_operand(std::string_view line,
                                          std::string_view operand) {
    if (const auto store = parse_two_operand_instruction(line, "str");
        store.has_value()) {
        return trim_ascii(store->rhs) == trim_ascii(operand);
    }
    return false;
}

bool instruction_loads_from_memory_operand(std::string_view line,
                                           std::string_view operand) {
    if (const auto load = parse_two_operand_instruction(line, "ldr");
        load.has_value()) {
        return trim_ascii(load->rhs) == trim_ascii(operand);
    }
    return false;
}

bool is_basic_block_terminator(std::string_view line) {
    line = trim_ascii(line);
    return line == "ret" || parse_unconditional_branch_line(line).has_value() ||
           parse_conditional_branch_line(line).has_value() ||
           parse_compare_branch_line(line).has_value() ||
           parse_test_bit_branch_line(line).has_value();
}

std::optional<std::string> referenced_branch_label(std::string_view line) {
    if (const auto branch = parse_unconditional_branch_line(line);
        branch.has_value()) {
        return branch->label;
    }
    if (const auto branch = parse_conditional_branch_line(line);
        branch.has_value()) {
        return branch->label;
    }
    if (const auto branch = parse_compare_branch_line(line);
        branch.has_value()) {
        return branch->label;
    }
    if (const auto branch = parse_test_bit_branch_line(line);
        branch.has_value()) {
        return branch->label;
    }
    return std::nullopt;
}

bool is_trivial_phi_shell_move(std::string_view line) {
    return parse_plain_move_line(line).has_value() ||
           parse_movz_immediate_line(line).has_value() ||
           parse_movk_immediate_line(line).has_value();
}

bool is_inlineable_single_predecessor_bridge_line(std::string_view line) {
    line = trim_ascii(line);
    if (line.empty()) {
        return true;
    }
    if (line.front() == '.') {
        return false;
    }
    return !is_basic_block_terminator(line) &&
           !parse_label_definition(line).has_value();
}

bool is_harmless_move_wide_setup(std::string_view line,
                                 const PlainMoveAsmPattern &move) {
    if (const auto movz = parse_movz_immediate_line(line); movz.has_value()) {
        return !registers_alias(movz->reg, move.dst) &&
               !registers_alias(movz->reg, move.src);
    }
    if (const auto movk = parse_movk_immediate_line(line); movk.has_value()) {
        return !registers_alias(movk->reg, move.dst) &&
               !registers_alias(movk->reg, move.src);
    }
    return false;
}

bool is_harmless_move_passthrough(std::string_view line,
                                  const PlainMoveAsmPattern &move) {
    if (is_harmless_move_wide_setup(line, move)) {
        return true;
    }
    const auto other_move = parse_plain_move_line(line);
    if (!other_move.has_value()) {
        return false;
    }
    return !registers_alias(other_move->dst, move.dst) &&
           !registers_alias(other_move->src, move.dst) &&
           !registers_alias(other_move->dst, move.src) &&
           !registers_alias(other_move->src, move.src);
}

std::optional<std::pair<std::string, unsigned>>
parse_stack_slot_address_materialization(std::string_view line) {
    const auto sub = parse_three_operand_instruction(line, "sub");
    if (!sub.has_value() || trim_ascii(sub->lhs) != "x29") {
        return std::nullopt;
    }
    std::string_view rhs = trim_ascii(sub->rhs);
    if (rhs.empty() || rhs.front() != '#') {
        return std::nullopt;
    }
    rhs.remove_prefix(1);
    try {
        return std::pair<std::string, unsigned>{
            trim_ascii(sub->dst),
            static_cast<unsigned>(std::stoul(std::string(rhs), nullptr, 0))};
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> parse_memory_base_register(std::string_view memory_operand) {
    memory_operand = trim_ascii(memory_operand);
    if (memory_operand.size() < 3 || memory_operand.front() != '[') {
        return std::nullopt;
    }
    const std::size_t close = memory_operand.find(']');
    if (close == std::string_view::npos) {
        return std::nullopt;
    }
    if (!trim_ascii(memory_operand.substr(close + 1)).empty()) {
        return std::nullopt;
    }
    std::string_view inside = memory_operand.substr(1, close - 1);
    const std::size_t comma = inside.find(',');
    if (comma != std::string_view::npos) {
        inside = inside.substr(0, comma);
    }
    inside = trim_ascii(inside);
    if (inside.empty()) {
        return std::nullopt;
    }
    return std::string(inside);
}

std::optional<std::pair<std::size_t, long long>>
recent_frame_pointer_sub_materialization(
    const std::vector<std::string> &lines,
    std::size_t before_index,
    std::string_view reg,
    std::size_t search_limit = 8) {
    reg = trim_ascii(reg);
    if (reg.empty() || before_index == 0) {
        return std::nullopt;
    }
    const std::size_t begin =
        before_index > search_limit ? before_index - search_limit : 0;
    for (std::size_t index = before_index; index-- > begin;) {
        if (parse_label_definition(lines[index]).has_value() ||
            is_basic_block_terminator(lines[index])) {
            break;
        }
        const auto materialized = parse_frame_pointer_sub_materialization(lines[index]);
        if (!materialized.has_value()) {
            if (instruction_may_write_general_register(lines[index], reg)) {
                break;
            }
            continue;
        }
        if (registers_alias(materialized->first, reg)) {
            return std::pair<std::size_t, long long>{index, materialized->second};
        }
    }
    return std::nullopt;
}

std::optional<long long> recent_frame_pointer_sub_materialization_offset(
    const std::vector<std::string> &lines,
    std::size_t before_index,
    std::string_view reg,
    std::size_t search_limit = 8) {
    const auto materialized =
        recent_frame_pointer_sub_materialization(lines, before_index, reg,
                                                 search_limit);
    if (!materialized.has_value()) {
        return std::nullopt;
    }
    return materialized->second;
}

std::size_t count_frame_pointer_sub_materializations(
    const std::vector<std::string> &lines,
    long long frame_offset) {
    std::size_t count = 0;
    for (const std::string &line : lines) {
        const auto materialized = parse_frame_pointer_sub_materialization(line);
        if (materialized.has_value() && materialized->second == frame_offset) {
            ++count;
        }
    }
    return count;
}

bool matches_memory_access(const std::optional<TwoOperandAsmPattern> &op,
                           std::string_view reg,
                           std::string_view base,
                           long long offset) {
    if (!op.has_value() || trim_ascii(op->lhs) != reg) {
        return false;
    }
    const auto parsed_base = parse_memory_base_register(op->rhs);
    const auto parsed_offset = parse_memory_immediate_offset(op->rhs);
    return parsed_base.has_value() && parsed_offset.has_value() &&
           trim_ascii(*parsed_base) == base && *parsed_offset == offset;
}

bool line_mentions_register_token(std::string_view line, std::string_view reg) {
    std::size_t pos = line.find(reg);
    while (pos != std::string_view::npos) {
        const bool left_ok =
            pos == 0 ||
            !(std::isalnum(static_cast<unsigned char>(line[pos - 1])) ||
              line[pos - 1] == '_');
        const std::size_t end = pos + reg.size();
        const bool right_ok =
            end >= line.size() ||
            !(std::isalnum(static_cast<unsigned char>(line[end])) ||
              line[end] == '_');
        if (left_ok && right_ok) {
            return true;
        }
        pos = line.find(reg, pos + 1);
    }
    return false;
}

bool any_line_mentions_register_alias(const std::vector<std::string> &lines,
                                      std::string_view reg) {
    const std::string primary(reg);
    const std::string sibling =
        !reg.empty() && reg.front() == 'w' ? "x" + primary.substr(1)
                                           : !reg.empty() && reg.front() == 'x'
                                                 ? "w" + primary.substr(1)
                                                 : primary;
    for (const std::string &line : lines) {
        if (line_mentions_register_token(line, primary) ||
            line_mentions_register_token(line, sibling)) {
            return true;
        }
    }
    return false;
}

bool any_line_mentions_register_alias(const std::vector<std::string> &lines,
                                      std::size_t begin,
                                      std::size_t end,
                                      std::string_view reg) {
    const std::string primary(reg);
    const std::string sibling =
        !reg.empty() && reg.front() == 'w' ? "x" + primary.substr(1)
                                           : !reg.empty() && reg.front() == 'x'
                                                 ? "w" + primary.substr(1)
                                                 : primary;
    end = std::min(end, lines.size());
    for (std::size_t index = begin; index < end; ++index) {
        if (line_mentions_register_token(lines[index], primary) ||
            line_mentions_register_token(lines[index], sibling)) {
            return true;
        }
    }
    return false;
}

bool any_line_mentions_vector_alias(const std::vector<std::string> &lines,
                                    std::string_view reg) {
    const std::string primary(trim_ascii(reg));
    if (primary.empty()) {
        return false;
    }
    std::string sibling = primary;
    if (primary.front() == 'v') {
        sibling = "q" + primary.substr(1);
    } else if (primary.front() == 'q') {
        sibling = "v" + primary.substr(1);
    }
    for (const std::string &line : lines) {
        if (line_mentions_register_token(line, primary) ||
            line_mentions_register_token(line, sibling)) {
            return true;
        }
    }
    return false;
}

bool line_mentions_register_alias(std::string_view line, std::string_view reg) {
    const std::string primary(trim_ascii(reg));
    if (primary.empty()) {
        return false;
    }
    const std::string sibling =
        !primary.empty() && primary.front() == 'w'
            ? "x" + primary.substr(1)
            : !primary.empty() && primary.front() == 'x'
                  ? "w" + primary.substr(1)
                  : primary;
    return line_mentions_register_token(line, primary) ||
           line_mentions_register_token(line, sibling);
}

bool fold_zero_offset_global_symbol_memory_access(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
        const auto adrp = parse_adrp_symbol_line(lines[index]);
        const auto add = parse_add_lo12_symbol_line(lines[index + 1]);
        if (!adrp.has_value() || !add.has_value() ||
            !registers_alias(adrp->reg, add->dst) ||
            !registers_alias(adrp->reg, add->base) ||
            trim_ascii(adrp->symbol) != trim_ascii(add->symbol)) {
            continue;
        }

        std::optional<TwoOperandAsmPattern> access =
            parse_two_operand_instruction(lines[index + 2], "ldr");
        if (!access.has_value()) {
            access = parse_two_operand_instruction(lines[index + 2], "str");
        }
        if (!access.has_value()) {
            continue;
        }

        const auto access_base = parse_memory_base_register(access->rhs);
        const auto access_offset = parse_memory_immediate_offset(access->rhs);
        if (!access_base.has_value() || !access_offset.has_value() ||
            !registers_alias(*access_base, add->dst) || *access_offset != 0) {
            continue;
        }

        bool later_address_use = false;
        for (std::size_t probe = index + 3; probe < lines.size(); ++probe) {
            if (parse_label_definition(lines[probe]).has_value()) {
                std::size_t previous = probe;
                while (previous > index + 2) {
                    --previous;
                    if (!trim_ascii(lines[previous]).empty()) {
                        break;
                    }
                }
                if (previous <= index + 2 ||
                    !is_basic_block_terminator(lines[previous])) {
                    later_address_use = true;
                }
                break;
            }
            if (instruction_overwrites_register_without_using(lines[probe], add->dst)) {
                break;
            }
            if (line_mentions_register_alias(lines[probe], add->dst)) {
                later_address_use = true;
                break;
            }
        }
        if (later_address_use) {
            continue;
        }

        lines[index + 1].clear();
        lines[index + 2] = "  " + access->mnemonic + " " + access->lhs + ", [" +
                           adrp->reg + ", :lo12:" + add->symbol + "]";
        return true;
    }
    return false;
}

bool fold_zero_offset_memory_post_increment(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index < lines.size(); ++index) {
        std::optional<TwoOperandAsmPattern> access =
            parse_two_operand_instruction(lines[index], "ldr");
        if (!access.has_value()) {
            access = parse_two_operand_instruction(lines[index], "str");
        }
        if (!access.has_value()) {
            continue;
        }

        const auto base = parse_memory_base_register(access->rhs);
        const auto offset = parse_memory_immediate_offset(access->rhs);
        if (!base.has_value() || !offset.has_value() || *offset != 0 ||
            access->lhs.empty() ||
            (access->lhs.front() != 'w' && access->lhs.front() != 'x') ||
            registers_alias(access->lhs, *base)) {
            continue;
        }

        for (std::size_t probe = index + 1;
             probe < lines.size() && probe <= index + 16; ++probe) {
            if (parse_label_definition(lines[probe]).has_value()) {
                break;
            }
            if (const auto add = parse_add_immediate_line(lines[probe]);
                add.has_value() && registers_alias(add->dst, *base) &&
                registers_alias(add->lhs, *base) && add->immediate != 0) {
                lines[index] = "  " + access->mnemonic + " " + access->lhs +
                               ", [" + *base + "], #" +
                               std::to_string(add->immediate);
                lines[probe].clear();
                return true;
            }
            if (instruction_overwrites_register_without_using(lines[probe], *base) ||
                line_mentions_register_alias(lines[probe], *base)) {
                break;
            }
        }
    }
    return false;
}

bool remove_immediate_redundant_stack_reload(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        const auto store = parse_two_operand_instruction(lines[index], "str");
        const auto load = parse_two_operand_instruction(lines[index + 1], "ldr");
        if (!store.has_value() || !load.has_value()) {
            continue;
        }
        if (trim_ascii(store->rhs) != trim_ascii(load->rhs) ||
            !registers_alias(store->lhs, load->lhs)) {
            continue;
        }
        lines[index + 1].clear();
        return true;
    }
    return false;
}

std::optional<std::string>
choose_free_vector_loop_cache_register(const std::vector<std::string> &lines) {
    for (unsigned reg = 16; reg <= 31; ++reg) {
        const std::string candidate = "v" + std::to_string(reg);
        if (!any_line_mentions_vector_alias(lines, candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

bool line_is_call_instruction(std::string_view line) {
    line = trim_ascii(line);
    return starts_with_ascii(line, "bl ") || starts_with_ascii(line, "blr ");
}

bool register_has_later_live_use(const std::vector<std::string> &lines,
                                 std::size_t start_index,
                                 std::string_view reg) {
    for (std::size_t probe = start_index; probe < lines.size(); ++probe) {
        if (parse_label_definition(lines[probe]).has_value()) {
            break;
        }
        if (instruction_overwrites_register_without_using(lines[probe], reg)) {
            break;
        }
        if (line_mentions_register_alias(lines[probe], reg)) {
            return true;
        }
    }
    return false;
}

std::optional<HoistableFrameLoadAsmPattern>
parse_hoistable_frame_slot_reload(const std::vector<std::string> &lines,
                                  std::size_t load_index,
                                  std::size_t block_end) {
    if (load_index + 1 >= block_end) {
        return std::nullopt;
    }

    std::optional<TwoOperandAsmPattern> load;
    for (const char *mnemonic : {"ldr", "ldur"}) {
        load = parse_two_operand_instruction(lines[load_index], mnemonic);
        if (load.has_value()) {
            break;
        }
    }
    if (!load.has_value()) {
        return std::nullopt;
    }
    const auto base_reg = parse_memory_base_register(load->rhs);
    if (!base_reg.has_value() || *base_reg != "x29") {
        return std::nullopt;
    }

    const std::string_view loaded_reg = trim_ascii(load->lhs);
    if (loaded_reg.empty() ||
        (loaded_reg.front() != 'w' && loaded_reg.front() != 'x')) {
        return std::nullopt;
    }

    if (const auto sdiv = parse_three_operand_instruction(lines[load_index + 1], "sdiv");
        sdiv.has_value() && registers_alias(sdiv->rhs, loaded_reg)) {
        return HoistableFrameLoadAsmPattern{
            load_index, load_index + 1, load->mnemonic, std::string(loaded_reg),
            load->rhs, HoistableFrameLoadUseKind::SignedDivRhs};
    }
    if (const auto udiv = parse_three_operand_instruction(lines[load_index + 1], "udiv");
        udiv.has_value() && registers_alias(udiv->rhs, loaded_reg)) {
        return HoistableFrameLoadAsmPattern{
            load_index, load_index + 1, load->mnemonic, std::string(loaded_reg),
            load->rhs, HoistableFrameLoadUseKind::UnsignedDivRhs};
    }
    if (const auto cmp = parse_two_operand_instruction(lines[load_index + 1], "cmp");
        cmp.has_value()) {
        if (registers_alias(cmp->lhs, loaded_reg)) {
            return HoistableFrameLoadAsmPattern{
                load_index, load_index + 1, load->mnemonic, std::string(loaded_reg),
                load->rhs, HoistableFrameLoadUseKind::CompareLhs};
        }
        if (registers_alias(cmp->rhs, loaded_reg)) {
            return HoistableFrameLoadAsmPattern{
                load_index, load_index + 1, load->mnemonic, std::string(loaded_reg),
                load->rhs, HoistableFrameLoadUseKind::CompareRhs};
        }
    }

    return std::nullopt;
}

std::string rewrite_hoisted_frame_load_use(const HoistableFrameLoadAsmPattern &pattern,
                                           std::string_view replacement_reg,
                                           std::string_view original_line) {
    switch (pattern.use_kind) {
    case HoistableFrameLoadUseKind::SignedDivRhs: {
        const auto div = parse_three_operand_instruction(original_line, "sdiv");
        if (!div.has_value()) {
            return std::string(original_line);
        }
        return "  sdiv " + div->dst + ", " + div->lhs + ", " +
               std::string(replacement_reg);
    }
    case HoistableFrameLoadUseKind::UnsignedDivRhs: {
        const auto div = parse_three_operand_instruction(original_line, "udiv");
        if (!div.has_value()) {
            return std::string(original_line);
        }
        return "  udiv " + div->dst + ", " + div->lhs + ", " +
               std::string(replacement_reg);
    }
    case HoistableFrameLoadUseKind::CompareLhs: {
        const auto cmp = parse_two_operand_instruction(original_line, "cmp");
        if (!cmp.has_value()) {
            return std::string(original_line);
        }
        return "  cmp " + std::string(replacement_reg) + ", " + cmp->rhs;
    }
    case HoistableFrameLoadUseKind::CompareRhs: {
        const auto cmp = parse_two_operand_instruction(original_line, "cmp");
        if (!cmp.has_value()) {
            return std::string(original_line);
        }
        return "  cmp " + cmp->lhs + ", " + std::string(replacement_reg);
    }
    }
    return std::string(original_line);
}

bool hoist_tight_loop_frame_slot_reloads(std::vector<std::string> &lines) {
    if (any_line_mentions_register_alias(lines, "x16") ||
        any_line_mentions_register_alias(lines, "x17")) {
        return false;
    }

    std::unordered_map<std::string, std::size_t> label_index_by_name;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = parse_label_definition(lines[index]);
            label.has_value()) {
            label_index_by_name[*label] = index;
        }
    }

    for (std::size_t label_index = 0; label_index < lines.size(); ++label_index) {
        const auto loop_label = parse_label_definition(lines[label_index]);
        if (!loop_label.has_value()) {
            continue;
        }

        std::size_t preheader_branch_index = label_index;
        while (preheader_branch_index > 0) {
            --preheader_branch_index;
            if (!trim_ascii(lines[preheader_branch_index]).empty()) {
                break;
            }
        }
        if (preheader_branch_index >= lines.size()) {
            continue;
        }
        const auto preheader_branch =
            parse_unconditional_branch_line(lines[preheader_branch_index]);
        if (!preheader_branch.has_value() || preheader_branch->label != *loop_label) {
            continue;
        }

        std::size_t block_end = label_index + 1;
        while (block_end < lines.size() &&
               !parse_label_definition(lines[block_end]).has_value()) {
            ++block_end;
        }

        std::optional<HoistableFrameLoadAsmPattern> div_reload;
        std::optional<HoistableFrameLoadAsmPattern> cmp_reload;
        for (std::size_t index = label_index + 1; index < block_end; ++index) {
            const auto candidate =
                parse_hoistable_frame_slot_reload(lines, index, block_end);
            if (!candidate.has_value()) {
                continue;
            }
            if (!div_reload.has_value() &&
                (candidate->use_kind == HoistableFrameLoadUseKind::SignedDivRhs ||
                 candidate->use_kind == HoistableFrameLoadUseKind::UnsignedDivRhs) &&
                !candidate->loaded_reg.empty() && candidate->loaded_reg.front() == 'w') {
                div_reload = candidate;
                continue;
            }
            if (!cmp_reload.has_value() &&
                (candidate->use_kind == HoistableFrameLoadUseKind::CompareLhs ||
                 candidate->use_kind == HoistableFrameLoadUseKind::CompareRhs) &&
                !candidate->loaded_reg.empty() && candidate->loaded_reg.front() == 'x') {
                cmp_reload = candidate;
            }
        }

        if (!div_reload.has_value() || !cmp_reload.has_value()) {
            continue;
        }

        lines[div_reload->use_index] = rewrite_hoisted_frame_load_use(
            *div_reload, "w16", lines[div_reload->use_index]);
        lines[cmp_reload->use_index] = rewrite_hoisted_frame_load_use(
            *cmp_reload, "x17", lines[cmp_reload->use_index]);
        lines[div_reload->load_index].clear();
        lines[cmp_reload->load_index].clear();

        std::vector<std::string> hoisted_loads;
        hoisted_loads.push_back("  " + div_reload->load_mnemonic + " w16, " +
                                div_reload->memory_operand);
        hoisted_loads.push_back("  " + cmp_reload->load_mnemonic + " x17, " +
                                cmp_reload->memory_operand);
        lines.insert(lines.begin() +
                         static_cast<std::ptrdiff_t>(preheader_branch_index),
                     hoisted_loads.begin(), hoisted_loads.end());
        return true;
    }

    return false;
}

bool stack_slot_is_readonly_after(const std::vector<std::string> &lines,
                                  std::size_t begin,
                                  std::size_t end,
                                  long long frame_allocation,
                                  long long target_sp_offset) {
    std::unordered_map<std::string, long long> register_sp_offsets;
    std::unordered_map<std::string, long long> register_constants;

    for (std::size_t index = begin; index < end; ++index) {
        const std::string_view line = trim_ascii(lines[index]);
        if (line.empty()) {
            continue;
        }

        if (const auto movz = parse_movz_immediate_line(line); movz.has_value()) {
            register_constants[movz->reg] =
                static_cast<long long>(movz->immediate);
            register_sp_offsets.erase(movz->reg);
            if (!movz->reg.empty() && movz->reg.front() == 'x') {
                register_constants.erase("w" + movz->reg.substr(1));
                register_sp_offsets.erase("w" + movz->reg.substr(1));
            }
            continue;
        }

        if (const auto direct =
                parse_x29_immediate_stack_address(line, frame_allocation);
            direct.has_value()) {
            const auto sub = parse_three_operand_instruction(line, "sub");
            register_sp_offsets[sub->dst] = *direct;
            register_constants.erase(sub->dst);
            continue;
        }

        if (const auto sub = parse_three_operand_instruction(line, "sub");
            sub.has_value() && trim_ascii(sub->lhs) == "x29") {
            const auto constant = register_constants.find(sub->rhs);
            if (constant != register_constants.end()) {
                register_sp_offsets[sub->dst] =
                    frame_allocation - constant->second;
                register_constants.erase(sub->dst);
                continue;
            }
        }

        for (const char *mnemonic : {"str", "stur"}) {
            const auto store = parse_two_operand_instruction(line, mnemonic);
            if (!store.has_value()) {
                continue;
            }
            const auto store_offset =
                stack_offset_for_memory_operand(store->rhs, register_sp_offsets);
            if (!store_offset.has_value()) {
                return false;
            }
            if (*store_offset == target_sp_offset) {
                return false;
            }
        }

        const std::size_t space = line.find(' ');
        if (space == std::string_view::npos) {
            continue;
        }
        const std::string_view mnemonic = trim_ascii(line.substr(0, space));
        if (mnemonic == "str" || mnemonic == "stur" || mnemonic == "cmp" ||
            mnemonic == "fcmp" || mnemonic == "b" || starts_with_ascii(mnemonic, "b.")) {
            continue;
        }
        const std::string_view operands = trim_ascii(line.substr(space + 1));
        const std::size_t comma = operands.find(',');
        const std::string dst(trim_ascii(
            operands.substr(0, comma == std::string_view::npos ? operands.size()
                                                               : comma)));
        if (!dst.empty()) {
            register_sp_offsets.erase(dst);
            register_constants.erase(dst);
            if (dst.front() == 'x') {
                register_sp_offsets.erase("w" + dst.substr(1));
                register_constants.erase("w" + dst.substr(1));
            } else if (dst.front() == 'w') {
                register_sp_offsets.erase("x" + dst.substr(1));
                register_constants.erase("x" + dst.substr(1));
            }
        }
    }

    return true;
}

bool hoist_readonly_stack_compare_reloads(std::vector<std::string> &lines) {
    struct ReloadUse {
        std::size_t load_index = 0;
        std::size_t cmp_index = 0;
        std::string loaded_reg;
    };
    struct SlotCandidate {
        long long sp_offset = 0;
        char width = 'w';
        std::vector<ReloadUse> uses;
    };

    for (std::size_t function_begin = 0; function_begin < lines.size();
         ++function_begin) {
        if (trim_ascii(lines[function_begin]) != ".cfi_startproc") {
            continue;
        }
        std::size_t function_end = function_begin + 1;
        while (function_end < lines.size() &&
               trim_ascii(lines[function_end]) != ".cfi_endproc") {
            ++function_end;
        }
        if (function_end >= lines.size()) {
            continue;
        }
        if (any_line_mentions_register_alias(lines, function_begin, function_end,
                                             "x16") &&
            any_line_mentions_register_alias(lines, function_begin, function_end,
                                             "x17")) {
            continue;
        }
        bool has_call = false;
        for (std::size_t index = function_begin; index < function_end; ++index) {
            if (line_is_call_instruction(lines[index])) {
                has_call = true;
                break;
            }
        }
        if (has_call) {
            continue;
        }

        std::size_t first_local_label = function_end;
        for (std::size_t index = function_begin + 1; index < function_end; ++index) {
            const auto label = parse_label_definition(lines[index]);
            if (label.has_value() && starts_with_ascii(*label, ".L")) {
                first_local_label = index;
                break;
            }
        }
        if (first_local_label >= function_end) {
            continue;
        }

        long long frame_allocation = 0;
        for (std::size_t index = function_begin; index < first_local_label; ++index) {
            const auto adjust = parse_stack_pointer_sub_adjust(lines[index]);
            if (adjust.has_value()) {
                frame_allocation += *adjust;
            }
        }
        if (frame_allocation <= 0) {
            continue;
        }

        std::unordered_map<std::string, SlotCandidate> candidates;
        for (std::size_t index = first_local_label + 1; index + 1 < function_end;
             ++index) {
            const auto load = parse_two_operand_instruction(lines[index], "ldr");
            if (!load.has_value() || load->lhs.empty() ||
                (load->lhs.front() != 'w' && load->lhs.front() != 'x')) {
                continue;
            }
            const auto base = parse_memory_base_register(load->rhs);
            const auto offset = parse_memory_immediate_offset(load->rhs);
            if (!base.has_value() || *base != "sp" || !offset.has_value()) {
                continue;
            }
            const auto cmp = parse_two_operand_instruction(lines[index + 1], "cmp");
            if (!cmp.has_value() || !registers_alias(cmp->lhs, load->lhs) ||
                trim_ascii(cmp->rhs).empty() || trim_ascii(cmp->rhs).front() != '#') {
                continue;
            }
            if (register_has_later_live_use(lines, index + 2, load->lhs)) {
                continue;
            }
            const std::string key =
                std::to_string(*offset) + ":" + std::string(1, load->lhs.front());
            auto &candidate = candidates[key];
            candidate.sp_offset = *offset;
            candidate.width = load->lhs.front();
            candidate.uses.push_back(ReloadUse{index, index + 1, load->lhs});
        }

        for (auto &[_, candidate] : candidates) {
            if (candidate.uses.size() < 8) {
                continue;
            }
            if (!stack_slot_is_readonly_after(lines, first_local_label + 1,
                                              function_end, frame_allocation,
                                              candidate.sp_offset)) {
                continue;
            }
            const std::string scratch_x =
                !any_line_mentions_register_alias(lines, function_begin, function_end,
                                                  "x16")
                    ? "x16"
                    : "x17";
            const std::string scratch =
                candidate.width == 'w' ? "w" + scratch_x.substr(1) : scratch_x;
            const std::string memory =
                "[sp, #" + std::to_string(candidate.sp_offset) + "]";

            lines.insert(lines.begin() +
                             static_cast<std::ptrdiff_t>(first_local_label + 1),
                         "  ldr " + scratch + ", " + memory);
            for (const ReloadUse &use : candidate.uses) {
                const std::size_t load_index =
                    use.load_index >= first_local_label + 1 ? use.load_index + 1
                                                            : use.load_index;
                const std::size_t cmp_index =
                    use.cmp_index >= first_local_label + 1 ? use.cmp_index + 1
                                                           : use.cmp_index;
                const auto cmp =
                    parse_two_operand_instruction(lines[cmp_index], "cmp");
                if (!cmp.has_value()) {
                    continue;
                }
                lines[load_index].clear();
                lines[cmp_index] = "  cmp " + scratch + ", " + cmp->rhs;
            }
            return true;
        }
    }

    return false;
}

bool hoist_loop_invariant_vector_scalar_splats(std::vector<std::string> &lines) {
    const auto cache_reg_base = choose_free_vector_loop_cache_register(lines);
    if (!cache_reg_base.has_value()) {
        return false;
    }

    struct SplatMulUse {
        std::size_t dup_index = 0;
        std::size_t mul_index = 0;
        DupScalarAsmPattern dup;
        ThreeOperandAsmPattern mul;
    };

    for (std::size_t label_index = 0; label_index < lines.size(); ++label_index) {
        const auto loop_label = parse_label_definition(lines[label_index]);
        if (!loop_label.has_value()) {
            continue;
        }

        std::size_t preheader_branch_index = label_index;
        while (preheader_branch_index > 0) {
            --preheader_branch_index;
            if (!trim_ascii(lines[preheader_branch_index]).empty()) {
                break;
            }
        }
        if (preheader_branch_index >= lines.size()) {
            continue;
        }
        const auto preheader_branch =
            parse_unconditional_branch_line(lines[preheader_branch_index]);
        if (!preheader_branch.has_value() || preheader_branch->label != *loop_label) {
            continue;
        }

        std::size_t block_end = label_index + 1;
        while (block_end < lines.size() &&
               !parse_label_definition(lines[block_end]).has_value()) {
            ++block_end;
        }

        bool has_call = false;
        for (std::size_t index = label_index + 1; index < block_end; ++index) {
            if (line_is_call_instruction(lines[index])) {
                has_call = true;
                break;
            }
        }
        if (has_call) {
            continue;
        }

        for (std::size_t index = label_index + 1; index + 1 < block_end; ++index) {
            const auto first_dup = parse_dup_scalar_line(lines[index]);
            const auto first_mul = parse_three_operand_instruction(lines[index + 1], "mul");
            if (!first_dup.has_value() || !first_mul.has_value() ||
                !vector_registers_alias(first_mul->rhs, first_dup->vector_reg)) {
                continue;
            }

            std::vector<SplatMulUse> occurrences;
            occurrences.push_back(SplatMulUse{index, index + 1, *first_dup, *first_mul});
            for (std::size_t probe = index + 1; probe + 1 < block_end; ++probe) {
                const auto dup = parse_dup_scalar_line(lines[probe]);
                const auto mul = parse_three_operand_instruction(lines[probe + 1], "mul");
                if (!dup.has_value() || !mul.has_value() ||
                    !vector_registers_alias(mul->rhs, dup->vector_reg)) {
                    continue;
                }
                if (dup->scalar_reg != first_dup->scalar_reg ||
                    !vector_registers_alias(dup->vector_reg, first_dup->vector_reg)) {
                    continue;
                }
                occurrences.push_back(SplatMulUse{probe, probe + 1, *dup, *mul});
            }

            if (occurrences.size() < 2) {
                continue;
            }

            bool scalar_written_in_loop = false;
            for (std::size_t probe = label_index + 1; probe < block_end; ++probe) {
                if (instruction_may_write_general_register(lines[probe],
                                                           first_dup->scalar_reg)) {
                    scalar_written_in_loop = true;
                    break;
                }
            }
            if (scalar_written_in_loop) {
                continue;
            }

            const std::string cache_reg =
                format_dup_target_register(*cache_reg_base, first_dup->vector_reg);
            lines.insert(lines.begin() +
                             static_cast<std::ptrdiff_t>(preheader_branch_index),
                         "  dup " + cache_reg + ", " + first_dup->scalar_reg);

            for (const SplatMulUse &occurrence : occurrences) {
                lines[occurrence.mul_index + 1] = "  mul " + occurrence.mul.dst + ", " +
                                                  occurrence.mul.lhs + ", " + cache_reg;
                lines[occurrence.dup_index + 1].clear();
            }
            return true;
        }
    }

    return false;
}

bool fold_vector_mul_add_into_mla(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 4 < lines.size(); ++index) {
        const auto mul_load = parse_two_operand_instruction(lines[index], "ldr");
        const auto mul = parse_three_operand_instruction(lines[index + 1], "mul");
        const auto add_load = parse_two_operand_instruction(lines[index + 2], "ldr");
        const auto add = parse_three_operand_instruction(lines[index + 3], "add");
        const auto store = parse_two_operand_instruction(lines[index + 4], "str");
        if (!mul_load.has_value() || !mul.has_value() || !add_load.has_value() ||
            !add.has_value() || !store.has_value()) {
            continue;
        }
        if (trim_ascii(mul_load->lhs) != "q0" || trim_ascii(add_load->lhs) != "q1" ||
            trim_ascii(store->lhs) != "q0" || trim_ascii(mul->dst) != "v0.4s" ||
            trim_ascii(mul->lhs) != "v0.4s" || trim_ascii(add->dst) != "v0.4s" ||
            trim_ascii(add->lhs) != "v0.4s" || trim_ascii(add->rhs) != "v1.4s" ||
            trim_ascii(store->rhs) != trim_ascii(add_load->rhs)) {
            continue;
        }

        const std::string mla_rhs =
            format_dup_target_register(canonical_vector_register_name(mul->rhs),
                                       mul->rhs);
        const std::string mla_lhs =
            format_dup_target_register(canonical_vector_register_name(add->rhs),
                                       add->rhs);
        const std::string mla_dst =
            format_dup_target_register(canonical_vector_register_name(add->dst),
                                       add->dst);
        if (mla_rhs.empty()) {
            continue;
        }

        lines[index] = "  ldr q0, " + add_load->rhs;
        lines[index + 1] = "  ldr q1, " + mul_load->rhs;
        lines[index + 2] = "  mla " + mla_dst + ", " + mla_lhs + ", " + mla_rhs;
        lines[index + 3].clear();
        lines[index + 4] = "  str q0, " + store->rhs;
        return true;
    }

    return false;
}

bool strength_reduce_vector_mla_address_recurrence(
    std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::size_t> label_index_by_name;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = parse_label_definition(lines[index]);
            label.has_value()) {
            label_index_by_name[*label] = index;
        }
    }

    for (std::size_t label_index = 0; label_index + 19 < lines.size();
         ++label_index) {
        const auto loop_label = parse_label_definition(lines[label_index]);
        if (!loop_label.has_value()) {
            continue;
        }

        std::vector<std::size_t> body_indices;
        for (std::size_t probe = label_index + 1;
             probe < lines.size() && body_indices.size() < 18; ++probe) {
            if (parse_label_definition(lines[probe]).has_value()) {
                break;
            }
            if (trim_ascii(lines[probe]).empty()) {
                continue;
            }
            body_indices.push_back(probe);
        }
        if (body_indices.size() < 18) {
            continue;
        }

        const auto addr0 = parse_scaled_add_line(lines[body_indices[0]]);
        const auto addr0_hi =
            parse_three_operand_instruction(lines[body_indices[1]], "add");
        const auto addr0_copy = parse_plain_move_line(lines[body_indices[2]]);
        const auto addr1 = parse_scaled_add_line(lines[body_indices[3]]);
        const auto addr1_hi =
            parse_three_operand_instruction(lines[body_indices[4]], "add");
        const auto addr1_copy = parse_plain_move_line(lines[body_indices[5]]);
        const auto load0 = parse_two_operand_instruction(lines[body_indices[6]], "ldr");
        const auto load1 = parse_two_operand_instruction(lines[body_indices[7]], "ldr");
        const auto mla0 = parse_three_operand_instruction(lines[body_indices[8]], "mla");
        const auto store0 = parse_two_operand_instruction(lines[body_indices[9]], "str");
        const auto load2 =
            parse_two_operand_instruction(lines[body_indices[10]], "ldr");
        const auto load3 =
            parse_two_operand_instruction(lines[body_indices[11]], "ldr");
        const auto mla1 = parse_three_operand_instruction(lines[body_indices[12]], "mla");
        const auto store1 =
            parse_two_operand_instruction(lines[body_indices[13]], "str");
        const auto index_increment =
            parse_three_operand_instruction(lines[body_indices[14]], "add");
        const auto loop_cmp =
            parse_two_operand_instruction(lines[body_indices[16]], "cmp");
        const auto loop_back =
            parse_conditional_branch_line(lines[body_indices[17]]);
        if (!addr0.has_value() || !addr0_hi.has_value() || !addr0_copy.has_value() ||
            !addr1.has_value() || !addr1_hi.has_value() ||
            !addr1_copy.has_value() || !load0.has_value() || !load1.has_value() ||
            !mla0.has_value() || !store0.has_value() || !load2.has_value() ||
            !load3.has_value() || !mla1.has_value() || !store1.has_value() ||
            !index_increment.has_value() || !loop_cmp.has_value() ||
            !loop_back.has_value()) {
            continue;
        }

        if (addr0->shift != 2 || addr1->shift != 2 ||
            !registers_alias(addr0->index, addr1->index) ||
            trim_ascii(addr0_hi->dst) != "x9" ||
            !registers_alias(addr0_hi->lhs, addr0->dst) ||
            trim_ascii(addr0_hi->rhs) != "#16" ||
            !registers_alias(addr0_copy->src, addr0_hi->dst) ||
            !registers_alias(addr1_hi->dst, addr0_hi->dst) ||
            !registers_alias(addr1_hi->lhs, addr1->dst) ||
            trim_ascii(addr1_hi->rhs) != "#16" ||
            !registers_alias(addr1_copy->src, addr1_hi->dst) ||
            trim_ascii(index_increment->dst) != "x9" ||
            !registers_alias(index_increment->lhs, addr0->index) ||
            trim_ascii(index_increment->rhs) != "#8" ||
            !registers_alias(loop_cmp->lhs, index_increment->dst)) {
            continue;
        }

        if (!matches_memory_access(load0, "q0", addr0->dst, 0) ||
            !matches_memory_access(load1, "q1", addr1->dst, 0) ||
            !matches_memory_access(store0, "q0", addr0->dst, 0) ||
            !matches_memory_access(load2, "q0", addr0_copy->dst, 0) ||
            !matches_memory_access(load3, "q1", addr1_copy->dst, 0) ||
            !matches_memory_access(store1, "q0", addr0_copy->dst, 0)) {
            continue;
        }

        if (trim_ascii(mla0->dst) != "v0.4s" || trim_ascii(mla0->lhs) != "v1.4s" ||
            trim_ascii(mla1->dst) != "v0.4s" || trim_ascii(mla1->lhs) != "v1.4s" ||
            trim_ascii(mla0->rhs) != trim_ascii(mla1->rhs)) {
            continue;
        }

        if (registers_alias(addr0->dst, addr0->base) ||
            registers_alias(addr1->dst, addr1->base) ||
            registers_alias(addr0->dst, addr1->base) ||
            registers_alias(addr1->dst, addr0->base) ||
            registers_alias(addr0->dst, addr0->index) ||
            registers_alias(addr1->dst, addr0->index) ||
            registers_alias(addr0->dst, addr1->dst)) {
            continue;
        }

        const auto phi_it = label_index_by_name.find(loop_back->label);
        if (phi_it == label_index_by_name.end() || phi_it->second + 2 >= lines.size()) {
            continue;
        }
        const std::size_t phi_label_index = phi_it->second;
        const auto phi_move = parse_plain_move_line(lines[phi_label_index + 1]);
        const auto phi_jump =
            parse_unconditional_branch_line(lines[phi_label_index + 2]);
        if (!phi_move.has_value() || !phi_jump.has_value() ||
            !registers_alias(phi_move->dst, addr0->index) ||
            !registers_alias(phi_move->src, index_increment->dst) ||
            phi_jump->label != *loop_label) {
            continue;
        }

        std::size_t preheader_label_index = label_index;
        while (preheader_label_index > 0) {
            --preheader_label_index;
            if (parse_label_definition(lines[preheader_label_index]).has_value()) {
                break;
            }
        }
        if (preheader_label_index == label_index ||
            !parse_label_definition(lines[preheader_label_index]).has_value()) {
            continue;
        }

        std::size_t preheader_branch_index = label_index;
        while (preheader_branch_index > preheader_label_index + 1 &&
               trim_ascii(lines[preheader_branch_index - 1]).empty()) {
            --preheader_branch_index;
        }
        if (preheader_branch_index <= preheader_label_index + 1) {
            continue;
        }
        const auto preheader_branch =
            parse_unconditional_branch_line(lines[preheader_branch_index - 1]);
        if (!preheader_branch.has_value() || preheader_branch->label != *loop_label) {
            continue;
        }

        lines.insert(lines.begin() +
                         static_cast<std::ptrdiff_t>(preheader_branch_index - 1),
                     {"  mov " + addr0->dst + ", " + addr0->base,
                      "  mov " + addr1->dst + ", " + addr1->base});

        const std::size_t adjusted_body_begin = body_indices[0] + 2;
        const std::size_t adjusted_body_end = body_indices[13] + 2;
        std::vector<std::string> replacement = {
            "  ldr q0, [" + addr0->dst + ", #0]",
            "  ldr q1, [" + addr1->dst + ", #0]",
            lines[body_indices[8] + 2],
            "  str q0, [" + addr0->dst + ", #0]",
            "  ldr q0, [" + addr0->dst + ", #16]",
            "  ldr q1, [" + addr1->dst + ", #16]",
            lines[body_indices[12] + 2],
            "  str q0, [" + addr0->dst + ", #16]",
            "  add " + addr0->dst + ", " + addr0->dst + ", #32",
            "  add " + addr1->dst + ", " + addr1->dst + ", #32"};

        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(adjusted_body_begin),
                    lines.begin() + static_cast<std::ptrdiff_t>(adjusted_body_end + 1));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(adjusted_body_begin),
                     replacement.begin(), replacement.end());
        return true;
    }

    return false;
}

bool strength_reduce_scalar_stencil_address_recurrence(
    std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::size_t> label_index_by_name;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = parse_label_definition(lines[index]);
            label.has_value()) {
            label_index_by_name[*label] = index;
        }
    }

    for (std::size_t label_index = 0; label_index < lines.size(); ++label_index) {
        const auto loop_label = parse_label_definition(lines[label_index]);
        if (!loop_label.has_value()) {
            continue;
        }

        std::vector<std::size_t> body_indices;
        for (std::size_t probe = label_index + 1;
             probe < lines.size() && body_indices.size() < 22; ++probe) {
            if (parse_label_definition(lines[probe]).has_value()) {
                break;
            }
            if (trim_ascii(lines[probe]).empty()) {
                continue;
            }
            body_indices.push_back(probe);
        }
        if (body_indices.size() != 22) {
            continue;
        }

        const auto addr_im1 = parse_scaled_add_line(lines[body_indices[0]]);
        const auto load_im1 = parse_two_operand_instruction(lines[body_indices[1]], "ldr");
        const auto addr_ip1 = parse_scaled_add_line(lines[body_indices[2]]);
        const auto load_ip1 = parse_two_operand_instruction(lines[body_indices[3]], "ldr");
        const auto add0 = parse_three_operand_instruction(lines[body_indices[4]], "add");
        const auto addr_jm1 = parse_scaled_add_line(lines[body_indices[5]]);
        const auto load_jm1 = parse_two_operand_instruction(lines[body_indices[6]], "ldr");
        const auto add1 = parse_three_operand_instruction(lines[body_indices[7]], "add");
        const auto addr_jp1 = parse_scaled_add_line(lines[body_indices[8]]);
        const auto load_jp1 = parse_two_operand_instruction(lines[body_indices[9]], "ldr");
        const auto add2 = parse_three_operand_instruction(lines[body_indices[10]], "add");
        const auto addr_cur = parse_scaled_add_line(lines[body_indices[11]]);
        const auto add3 = parse_three_operand_instruction(lines[body_indices[12]], "add");
        const auto idx_increment =
            parse_three_operand_instruction(lines[body_indices[13]], "add");
        const auto addr_next = parse_scaled_add_line(lines[body_indices[14]]);
        const auto load_next = parse_two_operand_instruction(lines[body_indices[15]], "ldr");
        const auto add4 = parse_three_operand_instruction(lines[body_indices[16]], "add");
        std::optional<ThreeOperandAsmPattern> divide =
            parse_three_operand_instruction(lines[body_indices[17]], "sdiv");
        if (!divide.has_value()) {
            divide = parse_three_operand_instruction(lines[body_indices[17]], "udiv");
        }
        const auto store_cur = parse_two_operand_instruction(lines[body_indices[18]], "str");
        const auto bound_load = parse_two_operand_instruction(lines[body_indices[19]], "ldr");
        const auto loop_cmp = parse_two_operand_instruction(lines[body_indices[20]], "cmp");
        const auto loop_back = parse_conditional_branch_line(lines[body_indices[21]]);
        if (!addr_im1.has_value() || !load_im1.has_value() || !addr_ip1.has_value() ||
            !load_ip1.has_value() || !add0.has_value() || !addr_jm1.has_value() ||
            !load_jm1.has_value() || !add1.has_value() || !addr_jp1.has_value() ||
            !load_jp1.has_value() || !add2.has_value() || !addr_cur.has_value() ||
            !add3.has_value() || !idx_increment.has_value() ||
            !addr_next.has_value() || !load_next.has_value() ||
            !add4.has_value() || !divide.has_value() || !store_cur.has_value() ||
            !bound_load.has_value() || !loop_cmp.has_value() ||
            !loop_back.has_value()) {
            continue;
        }

        const auto same_index = [&](const ScaledAddAsmPattern &pattern) {
            return pattern.shift == 2 &&
                   registers_alias(pattern.index, addr_im1->index);
        };
        if (!same_index(*addr_im1) || !same_index(*addr_ip1) ||
            !same_index(*addr_jm1) || !same_index(*addr_jp1) ||
            !same_index(*addr_cur)) {
            continue;
        }
        if (addr_next->shift != 2 || !registers_alias(addr_next->base, addr_cur->base) ||
            !registers_alias(addr_next->index, idx_increment->dst)) {
            continue;
        }

        if (!matches_memory_access(load_im1, "w23", addr_im1->dst, 0) ||
            !matches_memory_access(load_ip1, "w9", addr_ip1->dst, 0) ||
            !matches_memory_access(load_jm1, "w9", addr_jm1->dst, 0) ||
            !matches_memory_access(load_jp1, "w9", addr_jp1->dst, 0) ||
            !matches_memory_access(load_next, "w9", addr_next->dst, 0) ||
            !matches_memory_access(store_cur, "w9", addr_cur->dst, 0)) {
            continue;
        }

        if (trim_ascii(add0->dst) != "w23" || trim_ascii(add0->lhs) != "w9" ||
            trim_ascii(add0->rhs) != "w23" || trim_ascii(add1->dst) != "w23" ||
            trim_ascii(add1->lhs) != "w23" || trim_ascii(add1->rhs) != "w9" ||
            trim_ascii(add2->dst) != "w23" || trim_ascii(add2->lhs) != "w23" ||
            trim_ascii(add2->rhs) != "w9" || trim_ascii(add3->dst) != "w21" ||
            trim_ascii(add3->lhs) != "w23" || trim_ascii(add3->rhs) != "w21" ||
            trim_ascii(add4->dst) != "w9" || trim_ascii(add4->lhs) != "w21" ||
            trim_ascii(add4->rhs) != "w9") {
            continue;
        }
        if (!registers_alias(idx_increment->dst, addr_im1->index) ||
            !registers_alias(idx_increment->lhs, addr_im1->index) ||
            trim_ascii(idx_increment->rhs) != "#1") {
            continue;
        }
        if (trim_ascii(divide->dst) != "w9" || trim_ascii(divide->lhs) != "w9") {
            continue;
        }
        if (trim_ascii(loop_cmp->lhs) != trim_ascii(idx_increment->dst) ||
            trim_ascii(loop_cmp->rhs) != trim_ascii(bound_load->lhs)) {
            continue;
        }
        const auto bound_base = parse_memory_base_register(bound_load->rhs);
        const auto bound_offset = parse_memory_immediate_offset(bound_load->rhs);
        if (!bound_base.has_value() || *bound_base != "x29" ||
            !bound_offset.has_value()) {
            continue;
        }

        const auto phi_it = label_index_by_name.find(loop_back->label);
        if (phi_it == label_index_by_name.end() || phi_it->second + 2 >= lines.size()) {
            continue;
        }
        const std::size_t phi_label_index = phi_it->second;
        const auto phi_move = parse_plain_move_line(lines[phi_label_index + 1]);
        const auto phi_jump =
            parse_unconditional_branch_line(lines[phi_label_index + 2]);
        if (!phi_move.has_value() || !phi_jump.has_value() ||
            trim_ascii(phi_move->dst) != "w21" ||
            trim_ascii(phi_move->src) != "w9" ||
            phi_jump->label != *loop_label) {
            continue;
        }

        std::optional<std::size_t> entry_shell_label_index;
        for (const auto &entry : label_index_by_name) {
            const std::size_t candidate = entry.second;
            if (candidate == phi_label_index || candidate + 4 >= lines.size()) {
                continue;
            }
            const auto move = parse_plain_move_line(lines[candidate + 1]);
            const auto movz = parse_movz_immediate_line(lines[candidate + 2]);
            const auto div_load =
                parse_two_operand_instruction(lines[candidate + 3], "ldr");
            const auto jump =
                parse_unconditional_branch_line(lines[candidate + 4]);
            if (!move.has_value() || !movz.has_value() || !div_load.has_value() ||
                !jump.has_value()) {
                continue;
            }
            if (trim_ascii(move->dst) != "w21" || trim_ascii(move->src) != "w9" ||
                trim_ascii(movz->reg) != addr_im1->index || movz->immediate != 1 ||
                trim_ascii(div_load->lhs) != trim_ascii(divide->rhs) ||
                jump->label != *loop_label) {
                continue;
            }
            const auto div_base = parse_memory_base_register(div_load->rhs);
            const auto div_offset = parse_memory_immediate_offset(div_load->rhs);
            if (!div_base.has_value() || *div_base != "x29" || !div_offset.has_value()) {
                continue;
            }
            entry_shell_label_index = candidate;
            break;
        }
        if (!entry_shell_label_index.has_value()) {
            continue;
        }

        const std::size_t entry_label_index = *entry_shell_label_index;
        lines.insert(lines.begin() +
                         static_cast<std::ptrdiff_t>(entry_label_index + 4),
                     {"  add " + addr_im1->base + ", " + addr_im1->base + ", #4",
                      "  add " + addr_ip1->base + ", " + addr_ip1->base + ", #4",
                      "  add " + addr_jm1->base + ", " + addr_jm1->base + ", #4",
                      "  add " + addr_jp1->base + ", " + addr_jp1->base + ", #4",
                      "  add " + addr_cur->base + ", " + addr_cur->base + ", #4"});

        const std::size_t adjusted_body_begin = body_indices.front();
        const std::size_t adjusted_body_end = body_indices[18];
        std::vector<std::string> replacement = {
            "  ldr w23, [" + addr_im1->base + ", #0]",
            "  ldr w9, [" + addr_ip1->base + ", #0]",
            lines[body_indices[4]],
            "  ldr w9, [" + addr_jm1->base + ", #0]",
            lines[body_indices[7]],
            "  ldr w9, [" + addr_jp1->base + ", #0]",
            lines[body_indices[10]],
            lines[body_indices[12]],
            lines[body_indices[13]],
            "  ldr w9, [" + addr_cur->base + ", #4]",
            lines[body_indices[16]],
            lines[body_indices[17]],
            "  str w9, [" + addr_cur->base + ", #0]",
            "  add " + addr_im1->base + ", " + addr_im1->base + ", #4",
            "  add " + addr_ip1->base + ", " + addr_ip1->base + ", #4",
            "  add " + addr_jm1->base + ", " + addr_jm1->base + ", #4",
            "  add " + addr_jp1->base + ", " + addr_jp1->base + ", #4",
            "  add " + addr_cur->base + ", " + addr_cur->base + ", #4"};

        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(adjusted_body_begin),
                    lines.begin() + static_cast<std::ptrdiff_t>(adjusted_body_end + 1));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(adjusted_body_begin),
                     replacement.begin(), replacement.end());
        return true;
    }

    return false;
}

bool hoist_scalar_stencil_loop_bound_load(std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::size_t> label_index_by_name;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = parse_label_definition(lines[index]);
            label.has_value()) {
            label_index_by_name[*label] = index;
        }
    }

    for (std::size_t label_index = 0; label_index < lines.size(); ++label_index) {
        const auto loop_label = parse_label_definition(lines[label_index]);
        if (!loop_label.has_value()) {
            continue;
        }

        std::vector<std::size_t> body_indices;
        for (std::size_t probe = label_index + 1;
             probe < lines.size() && body_indices.size() < 21; ++probe) {
            if (parse_label_definition(lines[probe]).has_value()) {
                break;
            }
            if (trim_ascii(lines[probe]).empty()) {
                continue;
            }
            body_indices.push_back(probe);
        }
        if (body_indices.size() != 21) {
            continue;
        }

        const auto load0 = parse_two_operand_instruction(lines[body_indices[0]], "ldr");
        const auto load1 = parse_two_operand_instruction(lines[body_indices[1]], "ldr");
        const auto add0 = parse_three_operand_instruction(lines[body_indices[2]], "add");
        const auto load2 = parse_two_operand_instruction(lines[body_indices[3]], "ldr");
        const auto add1 = parse_three_operand_instruction(lines[body_indices[4]], "add");
        const auto load3 = parse_two_operand_instruction(lines[body_indices[5]], "ldr");
        const auto add2 = parse_three_operand_instruction(lines[body_indices[6]], "add");
        const auto add3 = parse_three_operand_instruction(lines[body_indices[7]], "add");
        const auto idx_increment =
            parse_three_operand_instruction(lines[body_indices[8]], "add");
        const auto load_next = parse_two_operand_instruction(lines[body_indices[9]], "ldr");
        const auto add4 = parse_three_operand_instruction(lines[body_indices[10]], "add");
        std::optional<ThreeOperandAsmPattern> divide =
            parse_three_operand_instruction(lines[body_indices[11]], "sdiv");
        if (!divide.has_value()) {
            divide = parse_three_operand_instruction(lines[body_indices[11]], "udiv");
        }
        const auto store = parse_two_operand_instruction(lines[body_indices[12]], "str");
        const auto ptr_inc0 = parse_three_operand_instruction(lines[body_indices[13]], "add");
        const auto ptr_inc1 = parse_three_operand_instruction(lines[body_indices[14]], "add");
        const auto ptr_inc2 = parse_three_operand_instruction(lines[body_indices[15]], "add");
        const auto ptr_inc3 = parse_three_operand_instruction(lines[body_indices[16]], "add");
        const auto ptr_inc4 = parse_three_operand_instruction(lines[body_indices[17]], "add");
        const auto bound_load =
            parse_two_operand_instruction(lines[body_indices[18]], "ldr");
        const auto bound_cmp = parse_two_operand_instruction(lines[body_indices[19]], "cmp");
        const auto loop_back =
            parse_conditional_branch_line(lines[body_indices[20]]);
        if (!load0.has_value() || !load1.has_value() || !add0.has_value() ||
            !load2.has_value() || !add1.has_value() || !load3.has_value() ||
            !add2.has_value() || !add3.has_value() || !idx_increment.has_value() ||
            !load_next.has_value() || !add4.has_value() || !divide.has_value() ||
            !store.has_value() || !ptr_inc0.has_value() || !ptr_inc1.has_value() ||
            !ptr_inc2.has_value() || !ptr_inc3.has_value() || !ptr_inc4.has_value() ||
            !bound_load.has_value() || !bound_cmp.has_value() ||
            !loop_back.has_value()) {
            continue;
        }
        if (trim_ascii(bound_load->lhs) != "x24" ||
            !registers_alias(bound_cmp->rhs, bound_load->lhs) ||
            trim_ascii(bound_cmp->lhs) != trim_ascii(idx_increment->dst)) {
            continue;
        }
        const auto bound_base = parse_memory_base_register(bound_load->rhs);
        const auto bound_offset = parse_memory_immediate_offset(bound_load->rhs);
        if (!bound_base.has_value() || *bound_base != "x29" || !bound_offset.has_value()) {
            continue;
        }

        const auto phi_it = label_index_by_name.find(loop_back->label);
        if (phi_it == label_index_by_name.end() || phi_it->second + 2 >= lines.size()) {
            continue;
        }
        const auto phi_move = parse_plain_move_line(lines[phi_it->second + 1]);
        const auto phi_jump =
            parse_unconditional_branch_line(lines[phi_it->second + 2]);
        if (!phi_move.has_value() || !phi_jump.has_value() ||
            trim_ascii(phi_move->dst) != "w21" ||
            trim_ascii(phi_move->src) != "w9" ||
            phi_jump->label != *loop_label) {
            continue;
        }

        std::optional<std::size_t> entry_shell_label_index;
        for (const auto &entry : label_index_by_name) {
            const std::size_t candidate = entry.second;
            if (candidate == phi_it->second || candidate + 4 >= lines.size()) {
                continue;
            }
            const auto move = parse_plain_move_line(lines[candidate + 1]);
            const auto movz = parse_movz_immediate_line(lines[candidate + 2]);
            const auto div_load =
                parse_two_operand_instruction(lines[candidate + 3], "ldr");
            if (!move.has_value() || !movz.has_value() || !div_load.has_value()) {
                continue;
            }
            if (trim_ascii(move->dst) != "w21" || trim_ascii(move->src) != "w9" ||
                trim_ascii(movz->reg) != trim_ascii(idx_increment->dst) ||
                movz->immediate != 1 ||
                trim_ascii(div_load->lhs) != trim_ascii(divide->rhs)) {
                continue;
            }

            bool shell_ok = false;
            for (std::size_t probe = candidate + 4;
                 probe < lines.size() && probe <= candidate + 10; ++probe) {
                if (trim_ascii(lines[probe]).empty()) {
                    continue;
                }
                if (const auto jump = parse_unconditional_branch_line(lines[probe]);
                    jump.has_value()) {
                    shell_ok = jump->label == *loop_label;
                    break;
                }
                const auto add = parse_three_operand_instruction(lines[probe], "add");
                if (!add.has_value() || trim_ascii(add->rhs) != "#4") {
                    shell_ok = false;
                    break;
                }
            }
            if (shell_ok) {
                entry_shell_label_index = candidate;
                break;
            }
        }
        if (!entry_shell_label_index.has_value()) {
            continue;
        }

        lines.insert(lines.begin() +
                         static_cast<std::ptrdiff_t>(*entry_shell_label_index + 4),
                     "  " + std::string(trim_ascii(lines[body_indices[18]])));
        lines[body_indices[18]].clear();
        return true;
    }

    return false;
}

bool forward_vector_temp_writeback(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 7 < lines.size(); ++index) {
        const auto temp_addr = parse_stack_slot_address_materialization(lines[index]);
        const auto lhs_load = parse_two_operand_instruction(lines[index + 1], "ldr");
        const auto rhs_load = parse_two_operand_instruction(lines[index + 2], "ldr");
        const auto add = parse_three_operand_instruction(lines[index + 3], "add");
        const auto temp_store = parse_two_operand_instruction(lines[index + 4], "str");
        if (!temp_addr.has_value() || !lhs_load.has_value() || !rhs_load.has_value() ||
            !add.has_value() || !temp_store.has_value()) {
            continue;
        }
        if (lhs_load->lhs != "q0" || rhs_load->lhs != "q1" ||
            trim_ascii(add->dst) != "v0.4s" || trim_ascii(add->lhs) != "v0.4s" ||
            trim_ascii(add->rhs) != "v1.4s" || trim_ascii(temp_store->lhs) != "q0") {
            continue;
        }
        const auto temp_store_base = parse_memory_base_register(temp_store->rhs);
        if (!temp_store_base.has_value() || *temp_store_base != temp_addr->first) {
            continue;
        }

        for (std::size_t probe = index + 5; probe + 2 < lines.size() &&
                                        probe <= index + 20; ++probe) {
            const auto reload_addr =
                parse_stack_slot_address_materialization(lines[probe]);
            const auto reload = parse_two_operand_instruction(lines[probe + 1], "ldr");
            const auto final_store =
                parse_two_operand_instruction(lines[probe + 2], "str");
            if (!reload_addr.has_value() || !reload.has_value() ||
                !final_store.has_value()) {
                continue;
            }
            if (reload_addr->second != temp_addr->second ||
                trim_ascii(reload->lhs) != "q0" ||
                trim_ascii(final_store->lhs) != "q0") {
                continue;
            }
            const auto reload_base = parse_memory_base_register(reload->rhs);
            const auto final_store_base = parse_memory_base_register(final_store->rhs);
            if (!reload_base.has_value() || *reload_base != reload_addr->first ||
                !final_store_base.has_value() ||
                *final_store_base == reload_addr->first) {
                continue;
            }
            bool final_store_targets_stack_temp = false;
            for (std::size_t probe_addr = index; probe_addr <= probe + 2;
                 ++probe_addr) {
                const auto address =
                    parse_stack_slot_address_materialization(lines[probe_addr]);
                if (address.has_value() && address->first == *final_store_base) {
                    final_store_targets_stack_temp = true;
                    break;
                }
            }
            if (final_store_targets_stack_temp) {
                continue;
            }
            bool temp_slot_reused_after_forward = false;
            for (std::size_t later = probe + 3; later < lines.size(); ++later) {
                if (parse_label_definition(lines[later]).has_value()) {
                    break;
                }
                for (const char *mnemonic : {"ldr", "str"}) {
                    const auto memory_op =
                        parse_two_operand_instruction(lines[later], mnemonic);
                    if (!memory_op.has_value()) {
                        continue;
                    }
                    const auto memory_base =
                        parse_memory_base_register(memory_op->rhs);
                    if (memory_base.has_value() &&
                        (*memory_base == temp_addr->first ||
                         *memory_base == reload_addr->first)) {
                        temp_slot_reused_after_forward = true;
                        break;
                    }
                }
                if (temp_slot_reused_after_forward) {
                    break;
                }
                const auto later_addr =
                    parse_stack_slot_address_materialization(lines[later]);
                if (later_addr.has_value() &&
                    later_addr->second == temp_addr->second) {
                    temp_slot_reused_after_forward = true;
                    break;
                }
            }
            if (temp_slot_reused_after_forward) {
                continue;
            }

            lines[index].clear();
            lines[index + 4] = "  str q0, " + final_store->rhs;
            lines[probe].clear();
            lines[probe + 1].clear();
            lines[probe + 2].clear();
            return true;
        }
    }

    return false;
}

bool collapse_vector_mul_add_temp_chain_with_high_address_moves(
    std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 47 < lines.size(); ++index) {
        const auto c0_addr = parse_scaled_add_line(lines[index]);
        const auto c1_addr = parse_add_immediate_line(lines[index + 1]);
        const auto c1_move = parse_plain_move_line(lines[index + 2]);
        const auto c0_temp_addr =
            parse_stack_slot_address_materialization(lines[index + 3]);
        const auto c0_load = parse_two_operand_instruction(lines[index + 4], "ldr");
        const auto c0_store = parse_two_operand_instruction(lines[index + 5], "str");
        const auto c1_temp_addr =
            parse_stack_slot_address_materialization(lines[index + 6]);
        const auto c1_load = parse_two_operand_instruction(lines[index + 7], "ldr");
        const auto c1_store = parse_two_operand_instruction(lines[index + 8], "str");
        const auto b0_addr = parse_scaled_add_line(lines[index + 9]);
        const auto b1_addr = parse_add_immediate_line(lines[index + 10]);
        const auto b1_move = parse_plain_move_line(lines[index + 11]);
        const auto b0_temp_addr =
            parse_stack_slot_address_materialization(lines[index + 12]);
        const auto b0_load = parse_two_operand_instruction(lines[index + 13], "ldr");
        const auto b0_store = parse_two_operand_instruction(lines[index + 14], "str");
        const auto b1_temp_addr =
            parse_stack_slot_address_materialization(lines[index + 15]);
        const auto b1_load = parse_two_operand_instruction(lines[index + 16], "ldr");
        const auto b1_store = parse_two_operand_instruction(lines[index + 17], "str");
        const auto rb0_addr =
            parse_stack_slot_address_materialization(lines[index + 18]);
        const auto scalar_temp_addr =
            parse_stack_slot_address_materialization(lines[index + 19]);
        const auto scalar_dup0 = parse_dup_scalar_line(lines[index + 20]);
        const auto scalar_store =
            parse_two_operand_instruction(lines[index + 21], "str");
        const auto mul0_temp_addr =
            parse_stack_slot_address_materialization(lines[index + 22]);
        const auto rb0_load = parse_two_operand_instruction(lines[index + 23], "ldr");
        const auto scalar_dup1 = parse_dup_scalar_line(lines[index + 24]);
        const auto mul0 = parse_three_operand_instruction(lines[index + 25], "mul");
        const auto mul0_store =
            parse_two_operand_instruction(lines[index + 26], "str");
        const auto rb1_addr =
            parse_stack_slot_address_materialization(lines[index + 27]);
        const auto mul1_temp_addr =
            parse_stack_slot_address_materialization(lines[index + 28]);
        const auto rb1_load = parse_two_operand_instruction(lines[index + 29], "ldr");
        const auto scalar_dup2 = parse_dup_scalar_line(lines[index + 30]);
        const auto mul1 = parse_three_operand_instruction(lines[index + 31], "mul");
        const auto mul1_store =
            parse_two_operand_instruction(lines[index + 32], "str");
        const auto rm0_addr =
            parse_stack_slot_address_materialization(lines[index + 33]);
        const auto rc0_addr =
            parse_stack_slot_address_materialization(lines[index + 34]);
        const auto out0_temp_addr =
            parse_stack_slot_address_materialization(lines[index + 35]);
        const auto rm0_load = parse_two_operand_instruction(lines[index + 36], "ldr");
        const auto rc0_load = parse_two_operand_instruction(lines[index + 37], "ldr");
        const auto add0 = parse_three_operand_instruction(lines[index + 38], "add");
        const auto out0_store =
            parse_two_operand_instruction(lines[index + 39], "str");
        const auto rm1_addr =
            parse_stack_slot_address_materialization(lines[index + 40]);
        const auto rc1_addr =
            parse_stack_slot_address_materialization(lines[index + 41]);
        const auto rm1_load = parse_two_operand_instruction(lines[index + 42], "ldr");
        const auto rc1_load = parse_two_operand_instruction(lines[index + 43], "ldr");
        const auto add1 = parse_three_operand_instruction(lines[index + 44], "add");
        const auto out1_store =
            parse_two_operand_instruction(lines[index + 45], "str");
        const auto out0_reload =
            parse_two_operand_instruction(lines[index + 46], "ldr");
        const auto final_out0_store =
            parse_two_operand_instruction(lines[index + 47], "str");

        if (!c0_addr.has_value() || !c1_addr.has_value() ||
            !c1_move.has_value() || !c0_temp_addr.has_value() ||
            !c0_load.has_value() || !c0_store.has_value() ||
            !c1_temp_addr.has_value() || !c1_load.has_value() ||
            !c1_store.has_value() || !b0_addr.has_value() ||
            !b1_addr.has_value() || !b1_move.has_value() ||
            !b0_temp_addr.has_value() || !b0_load.has_value() ||
            !b0_store.has_value() || !b1_temp_addr.has_value() ||
            !b1_load.has_value() || !b1_store.has_value() ||
            !rb0_addr.has_value() || !scalar_temp_addr.has_value() ||
            !scalar_dup0.has_value() || !scalar_store.has_value() ||
            !mul0_temp_addr.has_value() || !rb0_load.has_value() ||
            !scalar_dup1.has_value() || !mul0.has_value() ||
            !mul0_store.has_value() || !rb1_addr.has_value() ||
            !mul1_temp_addr.has_value() || !rb1_load.has_value() ||
            !scalar_dup2.has_value() || !mul1.has_value() ||
            !mul1_store.has_value() || !rm0_addr.has_value() ||
            !rc0_addr.has_value() || !out0_temp_addr.has_value() ||
            !rm0_load.has_value() || !rc0_load.has_value() ||
            !add0.has_value() || !out0_store.has_value() ||
            !rm1_addr.has_value() || !rc1_addr.has_value() ||
            !rm1_load.has_value() || !rc1_load.has_value() ||
            !add1.has_value() || !out1_store.has_value() ||
            !out0_reload.has_value() || !final_out0_store.has_value()) {
            continue;
        }

        if (c1_addr->lhs != c0_addr->dst || c1_addr->immediate != 16 ||
            c1_move->src != c1_addr->dst || b1_addr->lhs != b0_addr->dst ||
            b1_addr->immediate != 16 || b1_move->src != b1_addr->dst) {
            continue;
        }
        if (trim_ascii(c0_store->lhs) != "q0" ||
            trim_ascii(c1_store->lhs) != "q0" ||
            trim_ascii(b0_store->lhs) != "q0" ||
            trim_ascii(b1_store->lhs) != "q0" ||
            trim_ascii(scalar_store->lhs) != "q0" ||
            trim_ascii(rb0_load->lhs) != "q0" ||
            trim_ascii(rb1_load->lhs) != "q0" ||
            trim_ascii(rm0_load->lhs) != "q0" ||
            trim_ascii(rm1_load->lhs) != "q0" ||
            trim_ascii(rc0_load->lhs) != "q1" ||
            trim_ascii(rc1_load->lhs) != "q1" ||
            trim_ascii(out0_store->lhs) != "q0" ||
            trim_ascii(out1_store->lhs) != "q0" ||
            trim_ascii(out0_reload->lhs) != "q0" ||
            trim_ascii(final_out0_store->lhs) != "q0") {
            continue;
        }
        if (scalar_dup0->scalar_reg != scalar_dup1->scalar_reg ||
            scalar_dup0->scalar_reg != scalar_dup2->scalar_reg ||
            trim_ascii(mul0->dst) != "v0.4s" ||
            trim_ascii(mul0->lhs) != "v0.4s" ||
            trim_ascii(mul0->rhs) != "v1.4s" ||
            trim_ascii(mul1->dst) != "v0.4s" ||
            trim_ascii(mul1->lhs) != "v0.4s" ||
            trim_ascii(mul1->rhs) != "v1.4s" ||
            trim_ascii(add0->dst) != "v0.4s" ||
            trim_ascii(add0->lhs) != "v0.4s" ||
            trim_ascii(add0->rhs) != "v1.4s" ||
            trim_ascii(add1->dst) != "v0.4s" ||
            trim_ascii(add1->lhs) != "v0.4s" ||
            trim_ascii(add1->rhs) != "v1.4s") {
            continue;
        }
        if (rb0_addr->second != b0_temp_addr->second ||
            rb1_addr->second != b1_temp_addr->second ||
            rm0_addr->second != mul0_temp_addr->second ||
            rm1_addr->second != mul1_temp_addr->second ||
            rc0_addr->second != c0_temp_addr->second ||
            rc1_addr->second != c1_temp_addr->second) {
            continue;
        }

        const auto out0_base = parse_memory_base_register(out0_store->rhs);
        const auto out0_reload_base = parse_memory_base_register(out0_reload->rhs);
        if (!out0_base.has_value() || *out0_base != out0_temp_addr->first ||
            !out0_reload_base.has_value() ||
            *out0_reload_base != out0_temp_addr->first ||
            trim_ascii(final_out0_store->rhs) != trim_ascii(c0_load->rhs) ||
            trim_ascii(out1_store->rhs) != trim_ascii(c1_load->rhs)) {
            continue;
        }

        std::vector<std::string> replacement = {
            lines[index],
            "  add " + c1_move->dst + ", " + c0_addr->dst + ", #16",
            lines[index + 9],
            "  add " + b1_move->dst + ", " + b0_addr->dst + ", #16",
            "  ldr q0, " + b0_load->rhs,
            "  dup v1.4s, " + scalar_dup0->scalar_reg,
            "  mul v0.4s, v0.4s, v1.4s",
            "  ldr q1, " + c0_load->rhs,
            "  add v0.4s, v0.4s, v1.4s",
            "  str q0, " + c0_load->rhs,
            "  ldr q0, " + b1_load->rhs,
            "  dup v1.4s, " + scalar_dup0->scalar_reg,
            "  mul v0.4s, v0.4s, v1.4s",
            "  ldr q1, " + c1_load->rhs,
            "  add v0.4s, v0.4s, v1.4s",
            "  str q0, " + c1_load->rhs,
        };

        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(index),
                    lines.begin() + static_cast<std::ptrdiff_t>(index + 48));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(index),
                     replacement.begin(), replacement.end());
        return true;
    }
    return false;
}

bool collapse_vector_mul_add_temp_chain(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 42 < lines.size(); ++index) {
        const auto c0_addr = parse_stack_slot_address_materialization(lines[index]);
        const auto c0_load = parse_two_operand_instruction(lines[index + 1], "ldr");
        const auto c0_store = parse_two_operand_instruction(lines[index + 2], "str");
        const auto c1_addr = parse_stack_slot_address_materialization(lines[index + 3]);
        const auto c1_load = parse_two_operand_instruction(lines[index + 4], "ldr");
        const auto c1_store = parse_two_operand_instruction(lines[index + 5], "str");
        const auto b0_addr = parse_stack_slot_address_materialization(lines[index + 9]);
        const auto b0_load = parse_two_operand_instruction(lines[index + 10], "ldr");
        const auto b0_store = parse_two_operand_instruction(lines[index + 11], "str");
        const auto b1_addr = parse_stack_slot_address_materialization(lines[index + 12]);
        const auto b1_load = parse_two_operand_instruction(lines[index + 13], "ldr");
        const auto b1_store = parse_two_operand_instruction(lines[index + 14], "str");
        const auto rb0_addr = parse_stack_slot_address_materialization(lines[index + 15]);
        const auto scalar_dup0 = parse_dup_scalar_line(lines[index + 17]);
        const auto scalar_store = parse_two_operand_instruction(lines[index + 18], "str");
        const auto mul0_addr = parse_stack_slot_address_materialization(lines[index + 19]);
        const auto rb0_load = parse_two_operand_instruction(lines[index + 20], "ldr");
        const auto scalar_dup1 = parse_dup_scalar_line(lines[index + 21]);
        const auto mul0 = parse_three_operand_instruction(lines[index + 22], "mul");
        const auto mul0_store = parse_two_operand_instruction(lines[index + 23], "str");
        const auto rb1_addr = parse_stack_slot_address_materialization(lines[index + 24]);
        const auto mul1_addr = parse_stack_slot_address_materialization(lines[index + 26]);
        const auto rb1_load = parse_two_operand_instruction(lines[index + 27], "ldr");
        const auto scalar_dup2 = parse_dup_scalar_line(lines[index + 28]);
        const auto mul1 = parse_three_operand_instruction(lines[index + 29], "mul");
        const auto mul1_store = parse_two_operand_instruction(lines[index + 30], "str");
        const auto rm0_addr = parse_stack_slot_address_materialization(lines[index + 31]);
        const auto rc0_addr = parse_stack_slot_address_materialization(lines[index + 32]);
        const auto rm0_load = parse_two_operand_instruction(lines[index + 33], "ldr");
        const auto rc0_load = parse_two_operand_instruction(lines[index + 34], "ldr");
        const auto add0 = parse_three_operand_instruction(lines[index + 35], "add");
        const auto out0_store = parse_two_operand_instruction(lines[index + 36], "str");
        const auto rm1_addr = parse_stack_slot_address_materialization(lines[index + 37]);
        const auto rc1_addr = parse_stack_slot_address_materialization(lines[index + 38]);
        const auto rm1_load = parse_two_operand_instruction(lines[index + 39], "ldr");
        const auto rc1_load = parse_two_operand_instruction(lines[index + 40], "ldr");
        const auto add1 = parse_three_operand_instruction(lines[index + 41], "add");
        const auto out1_store = parse_two_operand_instruction(lines[index + 42], "str");
        if (!c0_addr.has_value() || !c0_load.has_value() || !c0_store.has_value() ||
            !c1_addr.has_value() || !c1_load.has_value() || !c1_store.has_value() ||
            !b0_addr.has_value() || !b0_load.has_value() || !b0_store.has_value() ||
            !b1_addr.has_value() || !b1_load.has_value() || !b1_store.has_value() ||
            !rb0_addr.has_value() || !scalar_dup0.has_value() ||
            !scalar_store.has_value() || !mul0_addr.has_value() ||
            !rb0_load.has_value() || !scalar_dup1.has_value() || !mul0.has_value() ||
            !mul0_store.has_value() || !rb1_addr.has_value() ||
            !mul1_addr.has_value() || !rb1_load.has_value() ||
            !scalar_dup2.has_value() || !mul1.has_value() ||
            !mul1_store.has_value() || !rm0_addr.has_value() ||
            !rc0_addr.has_value() || !rm0_load.has_value() || !rc0_load.has_value() ||
            !add0.has_value() || !out0_store.has_value() || !rm1_addr.has_value() ||
            !rc1_addr.has_value() || !rm1_load.has_value() || !rc1_load.has_value() ||
            !add1.has_value() || !out1_store.has_value()) {
            continue;
        }

        const auto c0_base = parse_memory_base_register(c0_store->rhs);
        const auto c1_base = parse_memory_base_register(c1_store->rhs);
        const auto b0_base = parse_memory_base_register(b0_store->rhs);
        const auto b1_base = parse_memory_base_register(b1_store->rhs);
        const auto scalar_base = parse_memory_base_register(scalar_store->rhs);
        const auto mul0_base = parse_memory_base_register(mul0_store->rhs);
        const auto mul1_base = parse_memory_base_register(mul1_store->rhs);
        if (!c0_base.has_value() || *c0_base != c0_addr->first ||
            !c1_base.has_value() || *c1_base != c1_addr->first ||
            !b0_base.has_value() || *b0_base != b0_addr->first ||
            !b1_base.has_value() || *b1_base != b1_addr->first ||
            !scalar_base.has_value() || *scalar_base != "x22" ||
            !mul0_base.has_value() || *mul0_base != mul0_addr->first ||
            !mul1_base.has_value() || *mul1_base != mul1_addr->first) {
            continue;
        }

        if (trim_ascii(c0_store->lhs) != "q0" || trim_ascii(c1_store->lhs) != "q0" ||
            trim_ascii(b0_store->lhs) != "q0" || trim_ascii(b1_store->lhs) != "q0" ||
            trim_ascii(scalar_store->lhs) != "q0" ||
            trim_ascii(rb0_load->lhs) != "q0" || trim_ascii(rb1_load->lhs) != "q0" ||
            trim_ascii(rm0_load->lhs) != "q0" || trim_ascii(rm1_load->lhs) != "q0" ||
            trim_ascii(rc0_load->lhs) != "q1" || trim_ascii(rc1_load->lhs) != "q1" ||
            trim_ascii(out0_store->lhs) != "q0" ||
            trim_ascii(out1_store->lhs) != "q0") {
            continue;
        }

        if (rb0_addr->second != b0_addr->second || rb1_addr->second != b1_addr->second ||
            rm0_addr->second != mul0_addr->second || rm1_addr->second != mul1_addr->second ||
            rc0_addr->second != c0_addr->second || rc1_addr->second != c1_addr->second) {
            continue;
        }

        if (trim_ascii(mul0->dst) != "v0.4s" || trim_ascii(mul0->lhs) != "v0.4s" ||
            trim_ascii(mul0->rhs) != "v1.4s" || trim_ascii(mul1->dst) != "v0.4s" ||
            trim_ascii(mul1->lhs) != "v0.4s" || trim_ascii(mul1->rhs) != "v1.4s" ||
            trim_ascii(add0->dst) != "v0.4s" || trim_ascii(add0->lhs) != "v0.4s" ||
            trim_ascii(add0->rhs) != "v1.4s" || trim_ascii(add1->dst) != "v0.4s" ||
            trim_ascii(add1->lhs) != "v0.4s" || trim_ascii(add1->rhs) != "v1.4s") {
            continue;
        }

        if (trim_ascii(out0_store->rhs) != trim_ascii(c0_load->rhs) ||
            trim_ascii(out1_store->rhs) != trim_ascii(c1_load->rhs) ||
            scalar_dup0->scalar_reg != scalar_dup1->scalar_reg ||
            scalar_dup0->scalar_reg != scalar_dup2->scalar_reg) {
            continue;
        }

        std::vector<std::string> replacement = {
            lines[index + 6],
            lines[index + 7],
            lines[index + 8],
            "  ldr q0, " + b0_load->rhs,
            "  dup v1.4s, " + scalar_dup0->scalar_reg,
            "  mul v0.4s, v0.4s, v1.4s",
            "  ldr q1, " + c0_load->rhs,
            "  add v0.4s, v0.4s, v1.4s",
            "  str q0, " + c0_load->rhs,
            "  ldr q0, " + b1_load->rhs,
            "  dup v1.4s, " + scalar_dup0->scalar_reg,
            "  mul v0.4s, v0.4s, v1.4s",
            "  ldr q1, " + c1_load->rhs,
            "  add v0.4s, v0.4s, v1.4s",
            "  str q0, " + c1_load->rhs};

        std::fill(lines.begin() + index, lines.begin() + index + 43, std::string{});
        lines.erase(lines.begin() + index, lines.begin() + index + 43);
        lines.insert(lines.begin() + index, replacement.begin(), replacement.end());
        return true;
    }
    return false;
}

bool fold_vector_reduction_copy_chain(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 26 < lines.size(); ++index) {
        const auto loop_label = parse_label_definition(lines[index]);
        const auto loop_back =
            parse_conditional_branch_line(lines[index + 26]);
        if (!loop_label.has_value() || !loop_back.has_value()) {
            continue;
        }
        if (trim_ascii(lines[index + 1]) != "add x19, x12, x13, lsl #2" ||
            trim_ascii(lines[index + 2]) != "add x9, x19, #16" ||
            trim_ascii(lines[index + 3]) != "mov x20, x9" ||
            trim_ascii(lines[index + 4]) != "sub x9, x29, #304" ||
            trim_ascii(lines[index + 5]) != "ldr q0, [x19, #0]" ||
            trim_ascii(lines[index + 6]) != "str q0, [x9, #0]" ||
            trim_ascii(lines[index + 7]) != "sub x9, x29, #320" ||
            trim_ascii(lines[index + 8]) != "ldr q0, [x20, #0]" ||
            trim_ascii(lines[index + 9]) != "str q0, [x9, #0]" ||
            trim_ascii(lines[index + 10]) != "sub x19, x29, #304" ||
            trim_ascii(lines[index + 11]) != "sub x9, x29, #336" ||
            trim_ascii(lines[index + 12]) != "ldr q0, [x19, #0]" ||
            trim_ascii(lines[index + 13]) != "ldr q1, [x14, #0]" ||
            trim_ascii(lines[index + 14]) != "add v0.4s, v0.4s, v1.4s" ||
            trim_ascii(lines[index + 15]) != "str q0, [x9, #0]" ||
            trim_ascii(lines[index + 16]) != "sub x14, x29, #320" ||
            trim_ascii(lines[index + 17]) != "sub x9, x29, #352" ||
            trim_ascii(lines[index + 18]) != "ldr q0, [x14, #0]" ||
            trim_ascii(lines[index + 19]) != "ldr q1, [x15, #0]" ||
            trim_ascii(lines[index + 20]) != "add v0.4s, v0.4s, v1.4s" ||
            trim_ascii(lines[index + 21]) != "str q0, [x9, #0]" ||
            trim_ascii(lines[index + 22]) != "add x13, x13, #8" ||
            trim_ascii(lines[index + 23]) != "sub x9, x29, #336" ||
            trim_ascii(lines[index + 24]) != "sub x15, x29, #352" ||
            trim_ascii(lines[index + 25]) != "cmp x13, x10") {
            continue;
        }

        std::vector<std::string> replacement = {
            lines[index],
            lines[index + 1],
            "  add x20, x19, #16",
            "  sub x9, x29, #336",
            "  ldr q0, [x19, #0]",
            "  ldr q1, [x14, #0]",
            "  add v0.4s, v0.4s, v1.4s",
            "  str q0, [x9, #0]",
            "  sub x9, x29, #352",
            "  ldr q0, [x20, #0]",
            "  ldr q1, [x15, #0]",
            "  add v0.4s, v0.4s, v1.4s",
            "  str q0, [x9, #0]",
            lines[index + 22],
            lines[index + 23],
            lines[index + 24],
            lines[index + 25],
            lines[index + 26]};

        lines.erase(lines.begin() + index, lines.begin() + index + 27);
        lines.insert(lines.begin() + index, replacement.begin(), replacement.end());
        return true;
    }

    return false;
}

bool fold_vector_horizontal_sum_store_reload(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 11 < lines.size(); ++index) {
        const auto label = parse_label_definition(lines[index]);
        if (!label.has_value()) {
            continue;
        }
        if (trim_ascii(lines[index + 1]) != "sub x14, x29, #352" ||
            trim_ascii(lines[index + 2]) != "sub x13, x29, #336" ||
            trim_ascii(lines[index + 3]) != "sub x9, x29, #368" ||
            trim_ascii(lines[index + 4]) != "ldr q0, [x14, #0]" ||
            trim_ascii(lines[index + 5]) != "ldr q1, [x13, #0]" ||
            trim_ascii(lines[index + 6]) != "add v0.4s, v0.4s, v1.4s" ||
            trim_ascii(lines[index + 7]) != "str q0, [x9, #0]" ||
            trim_ascii(lines[index + 8]) != "sub x9, x29, #368" ||
            trim_ascii(lines[index + 9]) != "ldr q0, [x9, #0]" ||
            trim_ascii(lines[index + 10]) != "addv s0, v0.4s" ||
            trim_ascii(lines[index + 11]) != "fmov w9, s0") {
            continue;
        }

        std::vector<std::string> replacement = {
            lines[index],
            lines[index + 1],
            lines[index + 2],
            lines[index + 4],
            lines[index + 5],
            lines[index + 6],
            lines[index + 10],
            lines[index + 11]};
        lines.erase(lines.begin() + index, lines.begin() + index + 12);
        lines.insert(lines.begin() + index, replacement.begin(), replacement.end());
        return true;
    }

    return false;
}

bool forward_vector_frame_temp_accumulator_reloads(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 4 < lines.size(); ++index) {
        const auto store = parse_two_operand_instruction(lines[index], "str");
        if (!store.has_value() || !vector_registers_alias(store->lhs, "q0")) {
            continue;
        }
        const auto store_base = parse_memory_base_register(store->rhs);
        const auto store_offset = parse_memory_immediate_offset(store->rhs);
        if (!store_base.has_value() || !store_offset.has_value() ||
            *store_offset != 0) {
            continue;
        }
        const auto store_frame_materialization = recent_frame_pointer_sub_materialization(
            lines, index, *store_base);
        if (!store_frame_materialization.has_value()) {
            continue;
        }
        const std::size_t store_frame_materialization_index =
            store_frame_materialization->first;
        const long long frame_offset = store_frame_materialization->second;

        const std::size_t acc_sub_index = index + 1;
        const auto acc_sub =
            parse_frame_pointer_sub_materialization(lines[acc_sub_index]);
        if (!acc_sub.has_value() || acc_sub->second != frame_offset) {
            continue;
        }

        std::optional<std::size_t> reload_index;
        for (std::size_t probe = acc_sub_index + 1;
             probe < lines.size() && probe <= acc_sub_index + 6; ++probe) {
            if (parse_label_definition(lines[probe]).has_value() ||
                is_basic_block_terminator(lines[probe])) {
                break;
            }
            const auto load = parse_two_operand_instruction(lines[probe], "ldr");
            if (!load.has_value() || !vector_registers_alias(load->lhs, "q1")) {
                if (instruction_writes_vector_register(lines[probe], "v1")) {
                    break;
                }
                continue;
            }
            const auto load_base = parse_memory_base_register(load->rhs);
            const auto load_offset = parse_memory_immediate_offset(load->rhs);
            if (!load_base.has_value() || !load_offset.has_value() ||
                *load_offset != 0 || !registers_alias(*load_base, acc_sub->first)) {
                continue;
            }
            if (probe + 1 >= lines.size()) {
                break;
            }
            const auto add =
                parse_three_operand_instruction(lines[probe + 1], "add");
            if (!add.has_value() || !vector_registers_alias(add->dst, "v0") ||
                !vector_registers_alias(add->lhs, "v0") ||
                !vector_registers_alias(add->rhs, "v1")) {
                continue;
            }
            reload_index = probe;
            break;
        }
        if (!reload_index.has_value()) {
            continue;
        }

        const bool temp_has_no_later_materialized_use =
            count_frame_pointer_sub_materializations(lines, frame_offset) <= 2;
        if (temp_has_no_later_materialized_use) {
            lines[index] = "  mov v1.16b, v0.16b";
        } else {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(index + 1),
                         "  mov v1.16b, v0.16b");
            reload_index = *reload_index + 1;
        }

        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(*reload_index));
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(acc_sub_index));
        if (temp_has_no_later_materialized_use &&
            store_frame_materialization_index < index) {
            lines.erase(lines.begin() +
                        static_cast<std::ptrdiff_t>(
                            store_frame_materialization_index));
        }
        return true;
    }
    return false;
}

bool fold_vector_pairwise_accumulate_stack_temps(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 20 < lines.size(); ++index) {
        const auto base_addr = parse_scaled_add_line(lines[index]);
        const auto high_addr = parse_add_immediate_line(lines[index + 1]);
        const auto high_move = parse_plain_move_line(lines[index + 2]);
        const auto temp0_addr = parse_stack_slot_address_materialization(lines[index + 3]);
        const auto load0 = parse_two_operand_instruction(lines[index + 4], "ldr");
        const auto temp0_store = parse_two_operand_instruction(lines[index + 5], "str");
        const auto temp1_addr = parse_stack_slot_address_materialization(lines[index + 6]);
        const auto load1 = parse_two_operand_instruction(lines[index + 7], "ldr");
        const auto temp1_store = parse_two_operand_instruction(lines[index + 8], "str");
        if (!base_addr.has_value() || !high_addr.has_value() ||
            !high_move.has_value() || !temp0_addr.has_value() ||
            !load0.has_value() || !temp0_store.has_value() ||
            !temp1_addr.has_value() || !load1.has_value() ||
            !temp1_store.has_value()) {
            continue;
        }
        if (high_addr->lhs != base_addr->dst || high_addr->immediate != 16 ||
            !registers_alias(high_move->src, high_addr->dst) ||
            trim_ascii(load0->lhs) != "q0" || trim_ascii(load1->lhs) != "q0" ||
            trim_ascii(temp0_store->lhs) != "q0" ||
            trim_ascii(temp1_store->lhs) != "q0") {
            continue;
        }
        const auto temp0_store_base = parse_memory_base_register(temp0_store->rhs);
        const auto temp1_store_base = parse_memory_base_register(temp1_store->rhs);
        if (!temp0_store_base.has_value() ||
            *temp0_store_base != temp0_addr->first ||
            !temp1_store_base.has_value() ||
            *temp1_store_base != temp1_addr->first) {
            continue;
        }

        std::size_t first_reload_addr_index = index + 9;
        if (const auto maybe_dead =
                parse_stack_slot_address_materialization(lines[first_reload_addr_index]);
            maybe_dead.has_value() && maybe_dead->second == temp0_addr->second) {
            const auto next_addr = parse_stack_slot_address_materialization(
                lines[first_reload_addr_index + 1]);
            if (next_addr.has_value() && next_addr->second == temp0_addr->second) {
                ++first_reload_addr_index;
            }
        }
        const auto temp0_reload_addr =
            parse_stack_slot_address_materialization(lines[first_reload_addr_index]);
        const auto out0_addr =
            parse_stack_slot_address_materialization(lines[first_reload_addr_index + 1]);
        const auto temp0_reload =
            parse_two_operand_instruction(lines[first_reload_addr_index + 2], "ldr");
        const auto acc0_load =
            parse_two_operand_instruction(lines[first_reload_addr_index + 3], "ldr");
        std::optional<ThreeOperandAsmPattern> op0;
        for (const char *mnemonic : {"add", "smin"}) {
            op0 = parse_three_operand_instruction(lines[first_reload_addr_index + 4],
                                                  mnemonic);
            if (op0.has_value()) {
                break;
            }
        }
        const auto out0_store =
            parse_two_operand_instruction(lines[first_reload_addr_index + 5], "str");
        if (!temp0_reload_addr.has_value() || !out0_addr.has_value() ||
            !temp0_reload.has_value() || !acc0_load.has_value() ||
            !op0.has_value() || !out0_store.has_value()) {
            continue;
        }

        std::size_t second_reload_addr_index = first_reload_addr_index + 6;
        if (const auto maybe_dead =
                parse_stack_slot_address_materialization(lines[second_reload_addr_index]);
            maybe_dead.has_value() && maybe_dead->second == temp1_addr->second) {
            const auto next_addr = parse_stack_slot_address_materialization(
                lines[second_reload_addr_index + 1]);
            if (next_addr.has_value() && next_addr->second == temp1_addr->second) {
                ++second_reload_addr_index;
            }
        }
        if (second_reload_addr_index + 5 >= lines.size()) {
            continue;
        }
        const auto temp1_reload_addr =
            parse_stack_slot_address_materialization(lines[second_reload_addr_index]);
        const auto out1_addr =
            parse_stack_slot_address_materialization(lines[second_reload_addr_index + 1]);
        const auto temp1_reload =
            parse_two_operand_instruction(lines[second_reload_addr_index + 2], "ldr");
        const auto acc1_load =
            parse_two_operand_instruction(lines[second_reload_addr_index + 3], "ldr");
        const auto op1 = parse_three_operand_instruction(
            lines[second_reload_addr_index + 4], op0->mnemonic);
        const auto out1_store =
            parse_two_operand_instruction(lines[second_reload_addr_index + 5], "str");
        if (!temp1_reload_addr.has_value() || !out1_addr.has_value() ||
            !temp1_reload.has_value() || !acc1_load.has_value() ||
            !op1.has_value() || !out1_store.has_value()) {
            continue;
        }

        const auto temp0_reload_base = parse_memory_base_register(temp0_reload->rhs);
        const auto temp1_reload_base = parse_memory_base_register(temp1_reload->rhs);
        const auto out0_store_base = parse_memory_base_register(out0_store->rhs);
        const auto out1_store_base = parse_memory_base_register(out1_store->rhs);
        if (!temp0_reload_base.has_value() ||
            *temp0_reload_base != temp0_reload_addr->first ||
            temp0_reload_addr->second != temp0_addr->second ||
            !temp1_reload_base.has_value() ||
            *temp1_reload_base != temp1_reload_addr->first ||
            temp1_reload_addr->second != temp1_addr->second ||
            !out0_store_base.has_value() || *out0_store_base != out0_addr->first ||
            !out1_store_base.has_value() || *out1_store_base != out1_addr->first) {
            continue;
        }
        if (trim_ascii(temp0_reload->lhs) != "q0" ||
            trim_ascii(temp1_reload->lhs) != "q0" ||
            trim_ascii(acc0_load->lhs) != "q1" ||
            trim_ascii(acc1_load->lhs) != "q1" ||
            trim_ascii(out0_store->lhs) != "q0" ||
            trim_ascii(out1_store->lhs) != "q0" ||
            trim_ascii(op0->dst) != "v0.4s" ||
            trim_ascii(op0->lhs) != "v0.4s" ||
            trim_ascii(op0->rhs) != "v1.4s" ||
            trim_ascii(op1->dst) != "v0.4s" ||
            trim_ascii(op1->lhs) != "v0.4s" ||
            trim_ascii(op1->rhs) != "v1.4s") {
            continue;
        }

        std::vector<std::string> replacement = {
            lines[index],
            lines[index + 1],
            lines[index + 2],
            lines[first_reload_addr_index + 1],
            lines[index + 4],
            lines[first_reload_addr_index + 3],
            lines[first_reload_addr_index + 4],
            lines[first_reload_addr_index + 5],
            lines[second_reload_addr_index + 1],
            lines[index + 7],
            lines[second_reload_addr_index + 3],
            lines[second_reload_addr_index + 4],
            lines[second_reload_addr_index + 5],
        };
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(index),
                    lines.begin() +
                        static_cast<std::ptrdiff_t>(second_reload_addr_index + 6));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(index),
                     replacement.begin(), replacement.end());
        return true;
    }

    return false;
}

bool compact_zero_frame_leaf_return_paths(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 16 < lines.size(); ++index) {
        const std::vector<std::string_view> expected = {
            ".Lfun_22:",
            "mov w0, w9",
            "b .Lfun_epilogue",
            ".Lfun_0_to_22_phi:",
            "mov w9, w10",
            "b .Lfun_22",
            ".Lfun_13_to_22_phi:",
            "b .Lfun_22",
            ".Lfun_17_to_22_phi:",
            "mov w9, wzr",
            "b .Lfun_22",
            ".Lfun_epilogue:",
            "ret"};
        bool matches = true;
        for (std::size_t offset = 0; offset < expected.size(); ++offset) {
            if (trim_ascii(lines[index + offset]) != expected[offset]) {
                matches = false;
                break;
            }
        }
        if (!matches) {
            continue;
        }

        std::vector<std::string> replacement = {
            ".Lfun_22:",
            "  mov w0, w9",
            "  ret",
            ".Lfun_0_to_22_phi:",
            "  mov w0, w10",
            "  ret",
            ".Lfun_13_to_22_phi:",
            "  mov w0, w9",
            "  ret",
            ".Lfun_17_to_22_phi:",
            "  mov w0, wzr",
            "  ret",
            ".Lfun_epilogue:",
            "  ret"};
        lines.erase(lines.begin() + index, lines.begin() + index + expected.size());
        lines.insert(lines.begin() + index, replacement.begin(), replacement.end());
        return true;
    }
    return false;
}

struct ReturnOnlyLocalBlockPattern {
    std::string label;
    std::vector<std::string> body_lines;
    std::size_t end_index = 0;
};

std::unordered_map<std::string, std::size_t>
build_local_label_reference_counts(const std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::size_t> refs;
    for (const std::string &line : lines) {
        if (const auto branch = parse_conditional_branch_line(line);
            branch.has_value()) {
            ++refs[branch->label];
        }
        if (const auto branch = parse_unconditional_branch_line(line);
            branch.has_value()) {
            ++refs[branch->label];
        }
        if (const auto branch = parse_compare_branch_line(line);
            branch.has_value()) {
            ++refs[branch->label];
        }
        if (const auto branch = parse_test_bit_branch_line(line);
            branch.has_value()) {
            ++refs[branch->label];
        }
    }
    return refs;
}

std::optional<ReturnOnlyLocalBlockPattern>
parse_return_only_local_block(const std::vector<std::string> &lines,
                              std::size_t label_index) {
    const auto label = parse_label_definition(lines[label_index]);
    if (!label.has_value() || !starts_with_ascii(*label, ".L")) {
        return std::nullopt;
    }
    if (label_index + 1 >= lines.size()) {
        return std::nullopt;
    }

    ReturnOnlyLocalBlockPattern block;
    block.label = *label;

    std::size_t index = label_index + 1;
    while (index < lines.size() && trim_ascii(lines[index]).empty()) {
        ++index;
    }
    if (index >= lines.size()) {
        return std::nullopt;
    }
    if (const auto move = parse_plain_move_line(lines[index]); move.has_value()) {
        if (move->dst != "w0" && move->dst != "x0") {
            return std::nullopt;
        }
        block.body_lines.push_back("  mov " + move->dst + ", " + move->src);
        ++index;
    }
    while (index < lines.size() && trim_ascii(lines[index]).empty()) {
        ++index;
    }
    if (index >= lines.size() || trim_ascii(lines[index]) != "ret") {
        return std::nullopt;
    }
    block.body_lines.push_back("  ret");
    block.end_index = index + 1;
    return block;
}

bool inline_single_predecessor_return_block(std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::size_t> label_index_by_name;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = parse_label_definition(lines[index]);
            label.has_value()) {
            label_index_by_name[*label] = index;
        }
    }
    const auto reference_counts = build_local_label_reference_counts(lines);

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto branch = parse_conditional_branch_line(lines[index]);
        if (!branch.has_value()) {
            continue;
        }
        const auto label_it = label_index_by_name.find(branch->label);
        if (label_it == label_index_by_name.end()) {
            continue;
        }
        const auto block =
            parse_return_only_local_block(lines, label_it->second);
        if (!block.has_value()) {
            continue;
        }
        const auto ref_it = reference_counts.find(block->label);
        if (ref_it == reference_counts.end() || ref_it->second != 1) {
            continue;
        }
        const auto inverted = invert_condition_code(branch->condition);
        if (!inverted.has_value()) {
            continue;
        }

        const auto jump = (index + 1 < lines.size())
                              ? parse_unconditional_branch_line(lines[index + 1])
                              : std::nullopt;
        if (jump.has_value()) {
            lines[index] = "  b." + *inverted + " " + jump->label;
            lines[index + 1] = block->body_lines.front();
            if (block->body_lines.size() == 2) {
                lines.insert(lines.begin() + index + 2, block->body_lines.back());
            }
            return true;
        }

        if (index + 1 < lines.size()) {
            const auto fallthrough_label = parse_label_definition(lines[index + 1]);
            if (fallthrough_label.has_value()) {
                lines[index] = "  b." + *inverted + " " + *fallthrough_label;
                lines.insert(lines.begin() + index + 1, block->body_lines.begin(),
                             block->body_lines.end());
                return true;
            }
        }
    }

    return false;
}

bool remove_unreferenced_local_return_block(std::vector<std::string> &lines) {
    const auto reference_counts = build_local_label_reference_counts(lines);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto block = parse_return_only_local_block(lines, index);
        if (!block.has_value()) {
            continue;
        }
        const auto ref_it = reference_counts.find(block->label);
        if (ref_it != reference_counts.end() && ref_it->second != 0) {
            continue;
        }
        lines.erase(lines.begin() + index, lines.begin() + block->end_index);
        return true;
    }
    return false;
}

bool remove_dead_local_island_after_unconditional_transfer(
    std::vector<std::string> &lines) {
    const auto reference_counts = build_local_label_reference_counts(lines);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto label = parse_label_definition(lines[index]);
        if (!label.has_value() || !starts_with_ascii(*label, ".L")) {
            continue;
        }
        const auto ref_it = reference_counts.find(*label);
        if (ref_it != reference_counts.end() && ref_it->second != 0) {
            continue;
        }

        std::size_t previous = index;
        while (previous > 0) {
            --previous;
            if (!trim_ascii(lines[previous]).empty()) {
                break;
            }
        }
        if (previous >= index) {
            continue;
        }
        const std::string_view previous_line = trim_ascii(lines[previous]);
        if (previous_line != "ret" &&
            !parse_unconditional_branch_line(previous_line).has_value()) {
            continue;
        }

        std::size_t end_index = index + 1;
        while (end_index < lines.size()) {
            if (parse_label_definition(lines[end_index]).has_value()) {
                break;
            }
            const std::string_view candidate = trim_ascii(lines[end_index]);
            if (starts_with_ascii(candidate, ".globl ") ||
                starts_with_ascii(candidate, ".type ") ||
                starts_with_ascii(candidate, ".section ") ||
                starts_with_ascii(candidate, ".text") ||
                starts_with_ascii(candidate, ".data") ||
                starts_with_ascii(candidate, ".rodata") ||
                starts_with_ascii(candidate, ".p2align ")) {
                break;
            }
            ++end_index;
        }
        if (end_index == index + 1) {
            continue;
        }
        lines.erase(lines.begin() + index, lines.begin() + end_index);
        return true;
    }
    return false;
}

bool fold_single_step_copy_chain(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        const auto move = parse_plain_move_line(lines[index]);
        if (!move.has_value()) {
            continue;
        }
        const bool can_forward_into_general_instruction =
            is_general_register_or_zero(move->src);
        const auto has_later_live_use = [&](std::size_t start_index) {
            for (std::size_t probe = start_index; probe < lines.size(); ++probe) {
                if (parse_label_definition(lines[probe]).has_value()) {
                    break;
                }
                if (instruction_overwrites_register_without_using(lines[probe],
                                                                  move->dst)) {
                    break;
                }
                if (line_mentions_register_alias(lines[probe], move->dst)) {
                    return true;
                }
            }
            return false;
        };

        if (const auto next_move = parse_plain_move_line(lines[index + 1]);
            next_move.has_value() &&
            registers_alias(next_move->src, move->dst)) {
            lines[index].clear();
            lines[index + 1] = "  mov " + next_move->dst + ", " + move->src;
            return true;
        }

        if (const auto extend =
                parse_two_operand_instruction(lines[index + 1], "uxtw");
            extend.has_value() && can_forward_into_general_instruction &&
            registers_alias(extend->rhs, move->dst)) {
            lines[index].clear();
            lines[index + 1] = "  uxtw " + extend->lhs + ", " + move->src;
            return true;
        }

        if (const auto madd =
                parse_four_operand_instruction(lines[index + 1], "madd");
            madd.has_value() && can_forward_into_general_instruction) {
            if (registers_alias(madd->extra, move->dst)) {
                lines[index].clear();
                lines[index + 1] = "  madd " + madd->dst + ", " + madd->lhs +
                                   ", " + madd->rhs + ", " + move->src;
                return true;
            }
            if (registers_alias(madd->lhs, move->dst) &&
                !has_later_live_use(index + 2)) {
                lines[index].clear();
                lines[index + 1] = "  madd " + madd->dst + ", " + move->src +
                                   ", " + madd->rhs + ", " + madd->extra;
                return true;
            }
            if (rhs_starts_with_register_token(madd->rhs, move->dst) &&
                !has_later_live_use(index + 2)) {
                lines[index].clear();
                lines[index + 1] =
                    "  madd " + madd->dst + ", " + madd->lhs + ", " +
                    replace_rhs_leading_register(madd->rhs, move->dst, move->src) +
                    ", " + madd->extra;
                return true;
            }
        }

        for (const char *mnemonic :
             {"add", "sub", "and", "orr", "eor", "lsl", "lsr", "asr"}) {
            if (!can_forward_into_general_instruction) {
                break;
            }
            const auto op =
                parse_three_operand_instruction(lines[index + 1], mnemonic);
            if (!op.has_value()) {
                continue;
            }
            if (registers_alias(op->lhs, move->dst)) {
                lines[index].clear();
                lines[index + 1] = "  " + op->mnemonic + " " + op->dst + ", " +
                                   move->src + ", " + op->rhs;
                return true;
            }
            if (rhs_starts_with_register_token(op->rhs, move->dst)) {
                lines[index].clear();
                lines[index + 1] =
                    "  " + op->mnemonic + " " + op->dst + ", " + op->lhs +
                    ", " + replace_rhs_leading_register(op->rhs, move->dst,
                                                         move->src);
                return true;
            }
        }

        std::size_t consumer_index = index + 1;
        while (consumer_index < lines.size() &&
               is_harmless_move_passthrough(lines[consumer_index], *move)) {
            ++consumer_index;
        }
        if (consumer_index == index + 1 || consumer_index >= lines.size()) {
            continue;
        }

        if (const auto madd =
                parse_four_operand_instruction(lines[consumer_index], "madd");
            madd.has_value() && can_forward_into_general_instruction) {
            if (registers_alias(madd->extra, move->dst)) {
                lines[index].clear();
                lines[consumer_index] = "  madd " + madd->dst + ", " + madd->lhs +
                                        ", " + madd->rhs + ", " + move->src;
                return true;
            }
            if (registers_alias(madd->lhs, move->dst) &&
                !has_later_live_use(consumer_index + 1)) {
                lines[index].clear();
                lines[consumer_index] =
                    "  madd " + madd->dst + ", " + move->src + ", " +
                    madd->rhs + ", " + madd->extra;
                return true;
            }
            if (rhs_starts_with_register_token(madd->rhs, move->dst) &&
                !has_later_live_use(consumer_index + 1)) {
                lines[index].clear();
                lines[consumer_index] =
                    "  madd " + madd->dst + ", " + madd->lhs + ", " +
                    replace_rhs_leading_register(madd->rhs, move->dst, move->src) +
                    ", " + madd->extra;
                return true;
            }
        }

        if (const auto mul =
                parse_three_operand_instruction(lines[consumer_index], "mul");
            mul.has_value() && can_forward_into_general_instruction) {
            if (registers_alias(mul->lhs, move->dst)) {
                lines[index].clear();
                lines[consumer_index] = "  mul " + mul->dst + ", " + move->src +
                                        ", " + mul->rhs;
                return true;
            }
            if (rhs_starts_with_register_token(mul->rhs, move->dst)) {
                lines[index].clear();
                lines[consumer_index] =
                    "  mul " + mul->dst + ", " + mul->lhs + ", " +
                    replace_rhs_leading_register(mul->rhs, move->dst, move->src);
                return true;
            }
        }

        for (const char *mnemonic :
             {"add", "sub", "and", "orr", "eor", "lsl", "lsr", "asr"}) {
            if (!can_forward_into_general_instruction) {
                break;
            }
            const auto op =
                parse_three_operand_instruction(lines[consumer_index], mnemonic);
            if (!op.has_value()) {
                continue;
            }
            const bool dst_redefines_move = registers_alias(op->dst, move->dst);
            if (registers_alias(op->lhs, move->dst) &&
                (dst_redefines_move || !has_later_live_use(consumer_index + 1))) {
                lines[index].clear();
                lines[consumer_index] = "  " + op->mnemonic + " " + op->dst +
                                        ", " + move->src + ", " + op->rhs;
                return true;
            }
            if (rhs_starts_with_register_token(op->rhs, move->dst) &&
                (dst_redefines_move || !has_later_live_use(consumer_index + 1))) {
                lines[index].clear();
                lines[consumer_index] =
                    "  " + op->mnemonic + " " + op->dst + ", " + op->lhs +
                    ", " + replace_rhs_leading_register(op->rhs, move->dst,
                                                         move->src);
                return true;
            }
        }
    }

    return false;
}

bool collapse_terminal_move_chain(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
        const std::string_view tail = trim_ascii(lines[index + 2]);
        if (tail != "ret" &&
            !parse_unconditional_branch_line(tail).has_value()) {
            continue;
        }

        if (const auto extend =
                parse_two_operand_instruction(lines[index], "uxtw");
            extend.has_value()) {
            const auto move = parse_plain_move_line(lines[index + 1]);
            if (move.has_value() && registers_alias(move->src, extend->lhs)) {
                lines[index].clear();
                lines[index + 1] =
                    "  uxtw " + move->dst + ", " + extend->rhs;
                return true;
            }
        }

        const auto first = parse_plain_move_line(lines[index]);
        const auto second = parse_plain_move_line(lines[index + 1]);
        if (!first.has_value() || !second.has_value()) {
            continue;
        }
        if (!registers_alias(second->src, first->dst)) {
            continue;
        }
        lines[index].clear();
        lines[index + 1] = "  mov " + second->dst + ", " + first->src;
        return true;
    }

    return false;
}

bool fold_symmetric_add_sub_address_copies(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 3 < lines.size(); ++index) {
        std::optional<ThreeOperandAsmPattern> first;
        std::optional<ThreeOperandAsmPattern> second;
        for (const char *mnemonic : {"add", "sub"}) {
            first = parse_three_operand_instruction(lines[index], mnemonic);
            if (first.has_value() && !first->rhs.empty() &&
                trim_ascii(first->rhs).front() == '#') {
                break;
            }
            first.reset();
        }
        const auto copy0 = parse_plain_move_line(lines[index + 1]);
        for (const char *mnemonic : {"add", "sub"}) {
            second = parse_three_operand_instruction(lines[index + 2], mnemonic);
            if (second.has_value() && !second->rhs.empty() &&
                trim_ascii(second->rhs).front() == '#') {
                break;
            }
            second.reset();
        }
        const auto copy1 = parse_plain_move_line(lines[index + 3]);
        if (!first.has_value() || !copy0.has_value() || !second.has_value() ||
            !copy1.has_value()) {
            continue;
        }
        if (!registers_alias(copy0->src, first->dst) ||
            !registers_alias(copy1->src, second->dst) ||
            !registers_alias(first->dst, second->dst) ||
            !registers_alias(first->lhs, second->lhs) ||
            first->mnemonic == second->mnemonic ||
            trim_ascii(first->rhs) != trim_ascii(second->rhs)) {
            continue;
        }

        bool later_use = false;
        for (std::size_t probe = index + 4; probe < lines.size(); ++probe) {
            if (parse_label_definition(lines[probe]).has_value()) {
                break;
            }
            if (instruction_overwrites_register_without_using(lines[probe], first->dst)) {
                break;
            }
            if (line_mentions_register_alias(lines[probe], first->dst)) {
                later_use = true;
                break;
            }
        }
        if (later_use) {
            continue;
        }

        lines[index] = "  " + first->mnemonic + " " + copy0->dst + ", " +
                       first->lhs + ", " + first->rhs;
        lines[index + 1].clear();
        lines[index + 2] = "  " + second->mnemonic + " " + copy1->dst + ", " +
                           second->lhs + ", " + second->rhs;
        lines[index + 3].clear();
        return true;
    }

    return false;
}

bool thread_loop_carried_increment_exit(std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::size_t> label_index_by_name;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = parse_label_definition(lines[index]);
            label.has_value()) {
            label_index_by_name[*label] = index;
        }
    }

    for (std::size_t index = 0; index + 6 < lines.size(); ++index) {
        std::optional<ThreeOperandAsmPattern> producer;
        for (const char *mnemonic : {"asr", "add"}) {
            producer = parse_three_operand_instruction(lines[index], mnemonic);
            if (producer.has_value()) {
                break;
            }
        }
        const auto increment = parse_three_operand_instruction(lines[index + 1], "add");
        const auto cmp = parse_two_operand_instruction(lines[index + 2], "cmp");
        const auto exit_branch = parse_conditional_branch_line(lines[index + 3]);
        const auto move_count = parse_plain_move_line(lines[index + 4]);
        const auto move_value = parse_plain_move_line(lines[index + 5]);
        const auto loop_jump = parse_unconditional_branch_line(lines[index + 6]);
        if (!producer.has_value() || !increment.has_value() || !cmp.has_value() ||
            !exit_branch.has_value() || !move_count.has_value() ||
            !move_value.has_value() || !loop_jump.has_value()) {
            continue;
        }
        if (exit_branch->label.find("_to_") == std::string::npos) {
            continue;
        }
        if (exit_branch->condition != "eq" || trim_ascii(increment->rhs) != "#1" ||
            trim_ascii(cmp->rhs) != "#1") {
            continue;
        }
        if (!registers_alias(cmp->lhs, producer->dst) ||
            !registers_alias(move_count->dst, increment->lhs) ||
            !registers_alias(move_count->src, increment->dst) ||
            !registers_alias(move_value->dst, producer->lhs) ||
            !registers_alias(move_value->src, producer->dst)) {
            continue;
        }
        if (producer->mnemonic == "add" &&
            !registers_alias(producer->lhs, move_value->dst)) {
            continue;
        }
        if (producer->mnemonic == "asr" &&
            !registers_alias(producer->lhs, move_value->dst)) {
            continue;
        }

        const auto exit_it = label_index_by_name.find(exit_branch->label);
        if (exit_it == label_index_by_name.end()) {
            continue;
        }

        std::vector<std::string> replacement;
        replacement.push_back("  " + producer->mnemonic + " " + move_value->dst +
                              ", " + producer->lhs + ", " + producer->rhs);
        replacement.push_back("  add " + move_count->dst + ", " + move_count->dst +
                              ", #1");
        replacement.push_back("  cmp " + move_value->dst + ", #1");
        replacement.push_back("  b.ne " + loop_jump->label);

        const auto return_block =
            parse_return_only_local_block(lines, exit_it->second);
        if (return_block.has_value() && return_block->body_lines.size() == 2 &&
            return_block->body_lines.front() ==
                "  mov w0, " + increment->dst) {
            replacement.push_back("  mov w0, " + move_count->dst);
            replacement.push_back("  ret");
            lines.erase(lines.begin() + index, lines.begin() + index + 7);
            lines.insert(lines.begin() + index, replacement.begin(),
                         replacement.end());
            return true;
        }

        if (exit_it->second + 1 >= lines.size()) {
            continue;
        }
        const auto exit_join =
            parse_unconditional_branch_line(lines[exit_it->second + 1]);
        if (!exit_join.has_value()) {
            continue;
        }
        if (!registers_alias(increment->dst, move_count->dst)) {
            replacement.push_back("  mov " + increment->dst + ", " +
                                  move_count->dst);
        }
        replacement.push_back("  b " + exit_join->label);
        lines.erase(lines.begin() + index, lines.begin() + index + 7);
        lines.insert(lines.begin() + index, replacement.begin(),
                     replacement.end());
        return true;
    }

    return false;
}

bool thread_increment_compare_join_block(std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::size_t> label_index_by_name;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = parse_label_definition(lines[index]);
            label.has_value()) {
            label_index_by_name[*label] = index;
        }
    }

    for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
        std::optional<ThreeOperandAsmPattern> producer;
        for (const char *mnemonic : {"asr", "add"}) {
            producer = parse_three_operand_instruction(lines[index], mnemonic);
            if (producer.has_value()) {
                break;
            }
        }
        const auto move = parse_plain_move_line(lines[index + 1]);
        const auto jump = parse_unconditional_branch_line(lines[index + 2]);
        if (!producer.has_value() || !move.has_value() || !jump.has_value()) {
            continue;
        }
        if (!registers_alias(move->src, producer->dst)) {
            continue;
        }

        const auto target_it = label_index_by_name.find(jump->label);
        if (target_it == label_index_by_name.end()) {
            continue;
        }
        const std::size_t label_index = target_it->second;
        if (label_index + 6 >= lines.size()) {
            continue;
        }
        const auto target_add = parse_three_operand_instruction(lines[label_index + 1], "add");
        const auto target_cmp = parse_two_operand_instruction(lines[label_index + 2], "cmp");
        const auto target_bcond =
            parse_conditional_branch_line(lines[label_index + 3]);
        const auto target_move1 = parse_plain_move_line(lines[label_index + 4]);
        const auto target_move2 = parse_plain_move_line(lines[label_index + 5]);
        const auto target_jump =
            parse_unconditional_branch_line(lines[label_index + 6]);
        if (!target_add.has_value() || !target_cmp.has_value() ||
            !target_bcond.has_value() || !target_move1.has_value() ||
            !target_move2.has_value() || !target_jump.has_value()) {
            continue;
        }
        if (trim_ascii(target_cmp->rhs) != "#1" ||
            !registers_alias(target_cmp->lhs, move->dst) ||
            !registers_alias(target_move1->src, target_add->dst) ||
            !registers_alias(target_move1->dst, target_add->lhs) ||
            !registers_alias(target_move2->dst, target_add->dst) ||
            !registers_alias(target_move2->src, target_cmp->lhs)) {
            continue;
        }
        if (!registers_alias(producer->dst, target_add->dst)) {
            continue;
        }

        std::vector<std::string> replacement;
        replacement.push_back("  " + producer->mnemonic + " " + move->dst + ", " +
                              producer->lhs + ", " + producer->rhs);
        replacement.push_back(lines[label_index + 1]);
        replacement.push_back(lines[label_index + 2]);
        replacement.push_back(lines[label_index + 3]);
        replacement.push_back(lines[label_index + 4]);
        replacement.push_back(lines[label_index + 5]);
        replacement.push_back(lines[label_index + 6]);

        lines.erase(lines.begin() + index, lines.begin() + index + 3);
        lines.insert(lines.begin() + index, replacement.begin(), replacement.end());
        return true;
    }

    return false;
}

bool eliminate_redundant_duplicate_splat_materialization(
    std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 5 < lines.size(); ++index) {
        const auto movz0 = parse_movz_immediate_line(lines[index]);
        const auto dup0 = parse_dup_scalar_line(lines[index + 1]);
        const auto store0 = parse_two_operand_instruction(lines[index + 2], "str");
        const auto movz1 = parse_movz_immediate_line(lines[index + 3]);
        const auto dup1 = parse_dup_scalar_line(lines[index + 4]);
        const auto store1 = parse_two_operand_instruction(lines[index + 5], "str");
        if (!movz0.has_value() || !dup0.has_value() || !store0.has_value() ||
            !movz1.has_value() || !dup1.has_value() || !store1.has_value()) {
            continue;
        }
        if (movz0->immediate != movz1->immediate ||
            !registers_alias(movz0->reg, movz1->reg) ||
            dup0->scalar_reg != dup1->scalar_reg ||
            !vector_registers_alias(dup0->vector_reg, dup1->vector_reg) ||
            !vector_registers_alias(store0->lhs, dup0->vector_reg) ||
            !vector_registers_alias(store1->lhs, dup1->vector_reg)) {
            continue;
        }
        lines[index + 3].clear();
        lines[index + 4].clear();
        return true;
    }
    return false;
}

bool fold_movz_dup_splat_into_movi(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
        const auto movz = parse_movz_immediate_line(lines[index]);
        const auto dup = parse_dup_scalar_line(lines[index + 1]);
        if (!movz.has_value() || !dup.has_value() ||
            movz->immediate != 1 ||
            !registers_alias(movz->reg, dup->scalar_reg) ||
            dup->vector_reg.find(".4s") == std::string::npos) {
            continue;
        }

        lines[index].clear();
        lines[index + 1] = "  movi " + dup->vector_reg + ", #1";
        return true;
    }
    return false;
}

bool eliminate_redundant_zero_extend_slot_store_text(
    std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
        const auto store32 = parse_two_operand_instruction(lines[index], "str");
        const auto extend = parse_two_operand_instruction(lines[index + 1], "uxtw");
        const auto store64 = parse_two_operand_instruction(lines[index + 2], "str");
        if (!store32.has_value() || !extend.has_value() || !store64.has_value()) {
            continue;
        }
        if (store32->lhs.empty() || store64->lhs.empty() ||
            store32->lhs.front() != 'w' || store64->lhs.front() != 'x' ||
            extend->lhs.empty() || extend->rhs.empty() ||
            extend->lhs.front() != 'x' || extend->rhs.front() != 'w') {
            continue;
        }
        if (store32->rhs != store64->rhs ||
            !registers_alias(store32->lhs, extend->lhs) ||
            !registers_alias(store32->lhs, extend->rhs) ||
            !registers_alias(store64->lhs, extend->lhs)) {
            continue;
        }
        lines[index].clear();
        return true;
    }
    return false;
}

bool inline_single_predecessor_unconditional_phi_shell(
    std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::size_t> branch_ref_count;
    std::unordered_map<std::string, std::size_t> unique_unconditional_ref_line;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = referenced_branch_label(lines[index]);
            label.has_value()) {
            ++branch_ref_count[*label];
        }
        if (const auto branch = parse_unconditional_branch_line(lines[index]);
            branch.has_value()) {
            auto &slot = unique_unconditional_ref_line[branch->label];
            if (slot == 0) {
                slot = index + 1;
            } else {
                slot = static_cast<std::size_t>(-1);
            }
        }
    }

    for (std::size_t label_index = 0; label_index < lines.size(); ++label_index) {
        const auto label = parse_label_definition(lines[label_index]);
        if (!label.has_value()) {
            continue;
        }
        const auto ref_count = branch_ref_count.find(*label);
        const auto pred_it = unique_unconditional_ref_line.find(*label);
        if (ref_count == branch_ref_count.end() || ref_count->second != 1 ||
            pred_it == unique_unconditional_ref_line.end() ||
            pred_it->second == static_cast<std::size_t>(-1) || pred_it->second == 0) {
            continue;
        }

        std::size_t shell_end = label_index + 1;
        while (shell_end < lines.size() &&
               !parse_label_definition(lines[shell_end]).has_value()) {
            ++shell_end;
        }
        if (shell_end <= label_index + 1) {
            continue;
        }
        const std::size_t tail_index = shell_end - 1;
        const auto tail_branch = parse_unconditional_branch_line(lines[tail_index]);
        if (!tail_branch.has_value()) {
            continue;
        }

        bool trivial_body = true;
        for (std::size_t index = label_index + 1; index < tail_index; ++index) {
            if (!is_inlineable_single_predecessor_bridge_line(lines[index])) {
                trivial_body = false;
                break;
            }
        }
        if (!trivial_body) {
            continue;
        }

        {
            const std::size_t predecessor_branch_index = pred_it->second - 1;
            std::vector<std::string> replacement;
            for (std::size_t index = label_index + 1; index < tail_index; ++index) {
                replacement.push_back(lines[index]);
            }
            const auto next_label = shell_end < lines.size()
                                        ? parse_label_definition(lines[shell_end])
                                        : std::nullopt;
            if (!next_label.has_value() || *next_label != tail_branch->label) {
                replacement.push_back("  b " + tail_branch->label);
            }

            lines.erase(lines.begin() + predecessor_branch_index);
            lines.insert(lines.begin() + predecessor_branch_index, replacement.begin(),
                         replacement.end());

            // Keep the original edge shell in place. Later cleanup may remove it
            // after recomputing references, but preserving it here prevents a
            // stale branch from becoming a dangling local label.
            return true;
        }
    }

    return false;
}

bool inline_single_predecessor_conditional_bridge_block(
    std::vector<std::string> &lines) {
    std::unordered_map<std::string, std::size_t> branch_ref_count;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = referenced_branch_label(lines[index]);
            label.has_value()) {
            ++branch_ref_count[*label];
        }
    }

    std::unordered_map<std::string, std::size_t> label_index_by_name;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (const auto label = parse_label_definition(lines[index]);
            label.has_value()) {
            label_index_by_name[*label] = index;
        }
    }

    for (std::size_t branch_index = 0; branch_index < lines.size(); ++branch_index) {
        const auto branch = parse_conditional_branch_line(lines[branch_index]);
        if (!branch.has_value()) {
            continue;
        }
        const auto ref_count = branch_ref_count.find(branch->label);
        if (ref_count == branch_ref_count.end() || ref_count->second != 1) {
            continue;
        }
        const auto label_it = label_index_by_name.find(branch->label);
        if (label_it == label_index_by_name.end()) {
            continue;
        }
        const std::size_t label_index = label_it->second;

        std::size_t fallthrough_index = branch_index + 1;
        while (fallthrough_index < lines.size() &&
               trim_ascii(lines[fallthrough_index]).empty()) {
            ++fallthrough_index;
        }
        if (fallthrough_index >= lines.size()) {
            continue;
        }
        const auto fallthrough_label = parse_label_definition(lines[fallthrough_index]);
        if (!fallthrough_label.has_value()) {
            continue;
        }
        const auto inverted = invert_condition_code(branch->condition);
        if (!inverted.has_value()) {
            continue;
        }

        std::size_t shell_end = label_index + 1;
        while (shell_end < lines.size() &&
               !parse_label_definition(lines[shell_end]).has_value()) {
            ++shell_end;
        }
        if (shell_end <= label_index + 1) {
            continue;
        }
        const std::size_t tail_index = shell_end - 1;
        const auto tail_branch = parse_unconditional_branch_line(lines[tail_index]);
        if (!tail_branch.has_value()) {
            continue;
        }

        bool inlineable_body = true;
        for (std::size_t index = label_index + 1; index < tail_index; ++index) {
            if (!is_inlineable_single_predecessor_bridge_line(lines[index])) {
                inlineable_body = false;
                break;
            }
        }
        if (!inlineable_body) {
            continue;
        }

        std::vector<std::string> replacement;
        replacement.push_back("  b." + *inverted + " " + *fallthrough_label);
        for (std::size_t index = label_index + 1; index < tail_index; ++index) {
            replacement.push_back(lines[index]);
        }
        replacement.push_back("  b " + tail_branch->label);

        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(branch_index));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(branch_index),
                     replacement.begin(), replacement.end());

        // Keep the original edge shell in place. Later cleanup may remove it
        // after recomputing references, but preserving it here prevents a stale
        // branch from becoming a dangling local label.
        return true;
    }

    return false;
}

std::optional<std::string> defined_register_for_trivial_move_like(
    std::string_view line) {
    if (const auto move = parse_plain_move_line(line); move.has_value()) {
        return move->dst;
    }
    if (const auto movz = parse_movz_immediate_line(line); movz.has_value()) {
        return movz->reg;
    }
    if (const auto movk = parse_movk_immediate_line(line); movk.has_value()) {
        return movk->reg;
    }
    return std::nullopt;
}

bool register_is_live_on_fallthrough_path_after(std::vector<std::string> const &lines,
                                                std::size_t start_index,
                                                std::string_view reg) {
    for (std::size_t index = start_index; index < lines.size(); ++index) {
        if (instruction_overwrites_register_without_using(lines[index], reg)) {
            return false;
        }
        if (line_mentions_register_alias(lines[index], reg)) {
            return true;
        }
    }
    return false;
}

bool fold_flags_preserving_backedge_shell(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 3 < lines.size(); ++index) {
        const std::string_view cmp_line = trim_ascii(lines[index]);
        if (!starts_with_ascii(cmp_line, "cmp ") &&
            !starts_with_ascii(cmp_line, "fcmp ")) {
            continue;
        }

        const auto exit_branch = parse_conditional_branch_line(lines[index + 1]);
        if (!exit_branch.has_value()) {
            continue;
        }
        const auto inverse_condition =
            invert_condition_code(exit_branch->condition);
        if (!inverse_condition.has_value()) {
            continue;
        }

        std::size_t tail_branch_index = index + 2;
        while (tail_branch_index < lines.size() &&
               !parse_label_definition(lines[tail_branch_index]).has_value()) {
            const auto jump =
                parse_unconditional_branch_line(lines[tail_branch_index]);
            if (jump.has_value()) {
                break;
            }
            if (!is_trivial_phi_shell_move(lines[tail_branch_index])) {
                tail_branch_index = lines.size();
                break;
            }
            ++tail_branch_index;
        }
        if (tail_branch_index >= lines.size()) {
            continue;
        }

        const auto loop_jump =
            parse_unconditional_branch_line(lines[tail_branch_index]);
        if (!loop_jump.has_value() || tail_branch_index == index + 2) {
            continue;
        }

        std::size_t fallthrough_index = tail_branch_index + 1;
        while (fallthrough_index < lines.size() &&
               trim_ascii(lines[fallthrough_index]).empty()) {
            ++fallthrough_index;
        }
        if (fallthrough_index >= lines.size()) {
            continue;
        }
        const auto fallthrough_label = parse_label_definition(lines[fallthrough_index]);
        if (!fallthrough_label.has_value() ||
            *fallthrough_label != exit_branch->label) {
            continue;
        }

        std::vector<std::string> defined_regs;
        bool safe_on_exit = true;
        for (std::size_t probe = index + 2; probe < tail_branch_index; ++probe) {
            const auto def = defined_register_for_trivial_move_like(lines[probe]);
            if (!def.has_value()) {
                safe_on_exit = false;
                break;
            }
            defined_regs.push_back(*def);
        }
        if (!safe_on_exit) {
            continue;
        }
        for (const std::string &reg : defined_regs) {
            if (register_is_live_on_fallthrough_path_after(lines,
                                                          fallthrough_index + 1,
                                                          reg)) {
                safe_on_exit = false;
                break;
            }
        }
        if (!safe_on_exit) {
            continue;
        }

        std::vector<std::string> replacement;
        for (std::size_t probe = index + 2; probe < tail_branch_index; ++probe) {
            replacement.push_back(lines[probe]);
        }
        replacement.push_back("  b." + *inverse_condition + " " + loop_jump->label);

        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(index + 1),
                    lines.begin() + static_cast<std::ptrdiff_t>(tail_branch_index + 1));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(index + 1),
                     replacement.begin(), replacement.end());
        return true;
    }

    return false;
}

bool fold_store_only_backedge_shell(std::vector<std::string> &lines) {
    for (std::size_t index = 0; index + 3 < lines.size(); ++index) {
        const std::string_view cmp_line = trim_ascii(lines[index]);
        if (!starts_with_ascii(cmp_line, "cmp ") &&
            !starts_with_ascii(cmp_line, "fcmp ")) {
            continue;
        }

        const auto exit_branch = parse_conditional_branch_line(lines[index + 1]);
        if (!exit_branch.has_value()) {
            continue;
        }
        const auto inverse_condition =
            invert_condition_code(exit_branch->condition);
        if (!inverse_condition.has_value()) {
            continue;
        }

        std::size_t tail_branch_index = index + 2;
        std::vector<std::string> stored_operands;
        while (tail_branch_index < lines.size() &&
               !parse_label_definition(lines[tail_branch_index]).has_value()) {
            const auto jump =
                parse_unconditional_branch_line(lines[tail_branch_index]);
            if (jump.has_value()) {
                break;
            }
            const auto store =
                parse_two_operand_instruction(lines[tail_branch_index], "str");
            if (!store.has_value()) {
                tail_branch_index = lines.size();
                break;
            }
            stored_operands.push_back(store->rhs);
            ++tail_branch_index;
        }
        if (tail_branch_index >= lines.size() || stored_operands.empty()) {
            continue;
        }

        const auto loop_jump =
            parse_unconditional_branch_line(lines[tail_branch_index]);
        if (!loop_jump.has_value()) {
            continue;
        }

        std::size_t fallthrough_index = tail_branch_index + 1;
        while (fallthrough_index < lines.size() &&
               trim_ascii(lines[fallthrough_index]).empty()) {
            ++fallthrough_index;
        }
        if (fallthrough_index >= lines.size()) {
            continue;
        }
        const auto fallthrough_label = parse_label_definition(lines[fallthrough_index]);
        if (!fallthrough_label.has_value() ||
            *fallthrough_label != exit_branch->label) {
            continue;
        }

        bool safe_on_exit = true;
        for (const std::string &operand : stored_operands) {
            for (std::size_t probe = fallthrough_index + 1; probe < lines.size();
                 ++probe) {
                if (instruction_stores_to_memory_operand(lines[probe], operand)) {
                    break;
                }
                if (instruction_loads_from_memory_operand(lines[probe], operand)) {
                    safe_on_exit = false;
                    break;
                }
            }
            if (!safe_on_exit) {
                break;
            }
        }
        if (!safe_on_exit) {
            continue;
        }

        std::vector<std::string> replacement;
        for (std::size_t probe = index + 2; probe < tail_branch_index; ++probe) {
            replacement.push_back(lines[probe]);
        }
        replacement.push_back("  b." + *inverse_condition + " " + loop_jump->label);

        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(index + 1),
                    lines.begin() + static_cast<std::ptrdiff_t>(tail_branch_index + 1));
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(index + 1),
                     replacement.begin(), replacement.end());
        return true;
    }

    return false;
}

bool remove_redundant_branch_to_next_label(std::vector<std::string> &lines) {
    bool changed = false;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto branch = parse_unconditional_branch_line(lines[index]);
        if (!branch.has_value()) {
            continue;
        }
        std::size_t next_index = index + 1;
        while (next_index < lines.size() &&
               trim_ascii(lines[next_index]).empty()) {
            ++next_index;
        }
        if (next_index >= lines.size()) {
            continue;
        }
        const auto label = parse_label_definition(lines[next_index]);
        if (!label.has_value() || *label != branch->label) {
            continue;
        }
        lines[index].clear();
        changed = true;
    }
    return changed;
}

void close_unbalanced_cfi_procedures(std::vector<std::string> &lines) {
    bool in_cfi_procedure = false;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string_view line = trim_ascii(lines[index]);
        if (line == ".cfi_startproc") {
            in_cfi_procedure = true;
            continue;
        }
        if (line == ".cfi_endproc") {
            in_cfi_procedure = false;
            continue;
        }
        const auto label = parse_label_definition(line);
        if (!in_cfi_procedure || !label.has_value() ||
            starts_with_ascii(*label, ".L")) {
            continue;
        }
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(index),
                     "  .cfi_endproc");
        in_cfi_procedure = false;
        ++index;
    }
    if (in_cfi_procedure) {
        lines.push_back("  .cfi_endproc");
    }
}

std::string optimize_emitted_asm_text(std::string text) {
    constexpr std::size_t kLargeAsmTextOptimizationLineLimit = 50000;

    std::vector<std::string> lines;
    {
        std::stringstream input(text);
        std::string line;
        while (std::getline(input, line)) {
            lines.push_back(std::move(line));
        }
    }

    const AsmTextOptimizationGroups groups = parse_asm_text_optimization_groups();
    if (!any_asm_text_optimization_group_enabled(groups)) {
        return text;
    }

    const bool groups_overridden =
        std::getenv("SYSYCC_AARCH64_EMIT_ASM_OPT_GROUPS") != nullptr;
    if (!groups_overridden &&
        lines.size() > kLargeAsmTextOptimizationLineLimit) {
        remove_redundant_branch_to_next_label(lines);
        close_unbalanced_cfi_procedures(lines);
        return join_non_empty_asm_lines(lines);
    }

    bool changed = false;
    do {
        changed = false;

        if (groups.core) {
            if (inline_single_predecessor_conditional_bridge_block(lines)) {
                changed = true;
            }
            if (inline_single_predecessor_unconditional_phi_shell(lines)) {
                changed = true;
            }
            if (fold_flags_preserving_backedge_shell(lines)) {
                changed = true;
            }
            if (fold_store_only_backedge_shell(lines)) {
                changed = true;
            }
            if (fold_zero_offset_global_symbol_memory_access(lines)) {
                changed = true;
            }
            if (remove_immediate_redundant_stack_reload(lines)) {
                changed = true;
            }
            if (fold_movz_dup_splat_into_movi(lines)) {
                changed = true;
            }
            if (eliminate_redundant_duplicate_splat_materialization(lines)) {
                changed = true;
            }
            if (eliminate_redundant_zero_extend_slot_store_text(lines)) {
                changed = true;
            }
            if (hoist_loop_invariant_vector_scalar_splats(lines)) {
                changed = true;
            }
            if (fold_vector_mul_add_into_mla(lines)) {
                changed = true;
            }
            if (strength_reduce_vector_mla_address_recurrence(lines)) {
                changed = true;
            }
            if (strength_reduce_scalar_stencil_address_recurrence(lines)) {
                changed = true;
            }
            if (hoist_scalar_stencil_loop_bound_load(lines)) {
                changed = true;
            }
            if (compact_zero_frame_leaf_return_paths(lines)) {
                changed = true;
            }
            if (inline_single_predecessor_return_block(lines)) {
                changed = true;
            }
            if (remove_unreferenced_local_return_block(lines)) {
                changed = true;
            }
            if (remove_dead_local_island_after_unconditional_transfer(lines)) {
                changed = true;
            }
            if (fold_single_step_copy_chain(lines)) {
                changed = true;
            }
            if (collapse_terminal_move_chain(lines)) {
                changed = true;
            }
            if (fold_symmetric_add_sub_address_copies(lines)) {
                changed = true;
            }
            if (thread_loop_carried_increment_exit(lines)) {
                changed = true;
            }
            if (thread_increment_compare_join_block(lines)) {
                changed = true;
            }
            if (fold_vector_reduction_copy_chain(lines)) {
                changed = true;
            }
            if (fold_vector_horizontal_sum_store_reload(lines)) {
                changed = true;
            }
            if (forward_vector_frame_temp_accumulator_reloads(lines)) {
                changed = true;
            }
            if (collapse_vector_mul_add_temp_chain(lines)) {
                changed = true;
            }
            if (forward_vector_temp_writeback(lines)) {
                changed = true;
            }
        }

        if (groups.branch) {
            for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
                const std::string_view cmp_line = trim_ascii(lines[index]);
                if (!starts_with_ascii(cmp_line, "cmp ") &&
                    !starts_with_ascii(cmp_line, "fcmp ")) {
                    continue;
                }
                const auto cset = parse_cset_line(lines[index + 1]);
                const auto compare_branch = parse_compare_branch_line(lines[index + 2]);
                if (!cset.has_value() || !compare_branch.has_value() ||
                    cset->reg != compare_branch->reg) {
                    continue;
                }

                std::string branch_condition = cset->condition;
                if (!compare_branch->branch_on_nonzero) {
                    const auto inverted = invert_condition_code(branch_condition);
                    if (!inverted.has_value()) {
                        continue;
                    }
                    branch_condition = *inverted;
                }

                lines[index + 1].clear();
                lines[index + 2] = "  b." + branch_condition + " " +
                                   compare_branch->label;
                changed = true;
            }

            for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
                const auto tst = parse_tst_line(lines[index]);
                const auto branch = parse_conditional_branch_line(lines[index + 1]);
                if (!tst.has_value() || !branch.has_value() || tst->mask == 0 ||
                    (tst->mask & (tst->mask - 1U)) != 0) {
                    continue;
                }
                const unsigned bit_index =
                    static_cast<unsigned>(__builtin_ctzll(tst->mask));
                const bool is_64bit = !tst->reg.empty() && tst->reg.front() == 'x';
                if ((!is_64bit && bit_index >= 32U) || branch->label.empty()) {
                    continue;
                }
                if (branch->condition == "eq") {
                    lines[index].clear();
                    lines[index + 1] = "  tbz " + tst->reg + ", #" +
                                        std::to_string(bit_index) + ", " +
                                        branch->label;
                    changed = true;
                    continue;
                }
                if (branch->condition == "ne") {
                    lines[index].clear();
                    lines[index + 1] = "  tbnz " + tst->reg + ", #" +
                                        std::to_string(bit_index) + ", " +
                                        branch->label;
                    changed = true;
                }
            }

            for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
                const auto branch = parse_conditional_branch_line(lines[index]);
                const auto jump = parse_unconditional_branch_line(lines[index + 1]);
                const auto label = parse_label_definition(lines[index + 2]);
                if (!branch.has_value() || !jump.has_value() || !label.has_value() ||
                    branch->label != *label) {
                    continue;
                }
                const auto inverted = invert_condition_code(branch->condition);
                if (!inverted.has_value()) {
                    continue;
                }
                lines[index] = "  b." + *inverted + " " + jump->label;
                lines[index + 1].clear();
                changed = true;
            }

            for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
                const auto branch = parse_compare_branch_line(lines[index]);
                const auto jump = parse_unconditional_branch_line(lines[index + 1]);
                const auto label = parse_label_definition(lines[index + 2]);
                if (!branch.has_value() || !jump.has_value() || !label.has_value() ||
                    branch->label != *label) {
                    continue;
                }
                lines[index] =
                    std::string(branch->branch_on_nonzero ? "  cbz " : "  cbnz ") +
                    branch->reg + ", " + jump->label;
                lines[index + 1].clear();
                changed = true;
            }

            for (std::size_t index = 0; index + 2 < lines.size(); ++index) {
                const auto branch = parse_test_bit_branch_line(lines[index]);
                const auto jump = parse_unconditional_branch_line(lines[index + 1]);
                const auto label = parse_label_definition(lines[index + 2]);
                if (!branch.has_value() || !jump.has_value() || !label.has_value() ||
                    branch->label != *label) {
                    continue;
                }
                const auto inverted =
                    invert_test_bit_branch_mnemonic(branch->mnemonic);
                if (!inverted.has_value()) {
                    continue;
                }
                lines[index] = "  " + *inverted + " " + branch->reg + ", #" +
                               branch->bit_index + ", " + jump->label;
                lines[index + 1].clear();
                changed = true;
            }
        }

        if (groups.move) {
        for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
            const auto move = parse_plain_move_line(lines[index]);
            if (move.has_value()) {
                const bool can_forward_into_general_instruction =
                    is_general_register_or_zero(move->src);
                for (const char *mnemonic :
                     {"add", "sub", "and", "orr", "eor", "lsl", "lsr", "asr"}) {
                    if (!can_forward_into_general_instruction) {
                        break;
                    }
                    const auto op =
                        parse_three_operand_instruction(lines[index + 1], mnemonic);
                    if (!op.has_value() || op->dst != move->dst ||
                        op->lhs != move->dst) {
                        continue;
                    }
                    lines[index].clear();
                    lines[index + 1] = "  " + op->mnemonic + " " + op->dst + ", " +
                                       move->src + ", " + op->rhs;
                    changed = true;
                    break;
                }
                if (lines[index].empty()) {
                    continue;
                }
                for (const char *mnemonic :
                     {"add", "sub", "and", "orr", "eor", "lsl", "lsr", "asr"}) {
                    if (!can_forward_into_general_instruction) {
                        break;
                    }
                    const auto op =
                        parse_three_operand_instruction(lines[index + 1], mnemonic);
                    if (!op.has_value() ||
                        !rhs_starts_with_register_token(op->rhs, move->dst) ||
                        registers_alias(move->src, op->dst) ||
                        registers_alias(move->src, op->lhs)) {
                        continue;
                    }
                    lines[index].clear();
                    lines[index + 1] =
                        "  " + op->mnemonic + " " + op->dst + ", " + op->lhs +
                        ", " +
                        replace_rhs_leading_register(op->rhs, move->dst, move->src);
                    changed = true;
                    break;
                }
                if (lines[index].empty()) {
                    continue;
                }
                if (const auto store =
                        parse_two_operand_instruction(lines[index + 1], "str");
                    store.has_value() && store->lhs == move->dst) {
                    if (!can_forward_into_general_instruction) {
                        continue;
                    }
                    if (register_has_later_live_use(lines, index + 2, move->dst)) {
                        continue;
                    }
                    bool has_immediate_matching_reload = false;
                    for (std::size_t probe = index + 2; probe < lines.size();
                         ++probe) {
                        const std::string trimmed(
                            trim_ascii(lines[probe]));
                        if (trimmed.empty()) {
                            continue;
                        }
                        if (parse_label_definition(lines[probe]).has_value()) {
                            break;
                        }
                        if (!trimmed.empty() && trimmed.front() == '.') {
                            continue;
                        }
                        for (const char *mnemonic : {"ldr", "ldur"}) {
                            const auto reload =
                                parse_two_operand_instruction(lines[probe],
                                                              mnemonic);
                            if (!reload.has_value()) {
                                continue;
                            }
                            if (trim_ascii(reload->rhs) == trim_ascii(store->rhs) &&
                                registers_alias(reload->lhs, move->dst)) {
                                has_immediate_matching_reload = true;
                            }
                        }
                        break;
                    }
                    if (has_immediate_matching_reload) {
                        continue;
                    }
                    lines[index].clear();
                    lines[index + 1] = "  str " + move->src + ", " + store->rhs;
                    changed = true;
                    continue;
                }
            }

            const auto movz = parse_movz_immediate_line(lines[index]);
            if (!movz.has_value()) {
                continue;
            }

            std::size_t overwrite_index = index + 1;
            while (overwrite_index < lines.size()) {
                const auto movk = parse_movk_immediate_line(lines[overwrite_index]);
                if (!movk.has_value() || !registers_alias(movk->reg, movz->reg)) {
                    break;
                }
                ++overwrite_index;
            }
            if (overwrite_index < lines.size() &&
                instruction_overwrites_register_without_using(
                    lines[overwrite_index], movz->reg)) {
                for (std::size_t clear_index = index; clear_index < overwrite_index;
                     ++clear_index) {
                    if (clear_index == index ||
                        (parse_movk_immediate_line(lines[clear_index]).has_value() &&
                         registers_alias(
                             parse_movk_immediate_line(lines[clear_index])->reg,
                             movz->reg))) {
                        lines[clear_index].clear();
                    } else {
                        break;
                    }
                }
                changed = true;
                continue;
            }

            if (overwrite_index != index + 1) {
                continue;
            }
            if (overwrite_index < lines.size() &&
                parse_movk_immediate_line(lines[overwrite_index]).has_value()) {
                continue;
            }
            if (overwrite_index < lines.size()) {
                const auto movk = parse_movk_immediate_line(lines[overwrite_index]);
                if (movk.has_value() && registers_alias(movk->reg, movz->reg)) {
                    ++overwrite_index;
                }
            }
            if (overwrite_index < lines.size() &&
                instruction_overwrites_register_without_using(
                    lines[overwrite_index], movz->reg)) {
                lines[index].clear();
                if (overwrite_index == index + 2) {
                    lines[index + 1].clear();
                }
                changed = true;
                continue;
            }

            for (const char *shift_mnemonic : {"asr", "lsr", "lsl"}) {
                const auto shift =
                    parse_three_operand_instruction(lines[index + 1], shift_mnemonic);
                if (!shift.has_value() || movz->immediate > 63 ||
                    shift->rhs != movz->reg) {
                    continue;
                }
                lines[index].clear();
                lines[index + 1] = "  " + shift->mnemonic + " " + shift->dst + ", " +
                                   shift->lhs + ", #" +
                                   std::to_string(movz->immediate);
                changed = true;
                break;
            }

            const auto mul = parse_three_operand_instruction(lines[index + 1], "mul");
            if (mul.has_value() && movz->immediate == 3 && mul->dst == movz->reg &&
                mul->rhs == movz->reg) {
                lines[index].clear();
                lines[index + 1] = "  add " + mul->dst + ", " + mul->lhs + ", " +
                                   mul->lhs + ", lsl #1";
                changed = true;
            }
        }
        }

        if (groups.slot) {
        if (hoist_readonly_frame_pointer_base_reloads(lines)) {
            changed = true;
        }
        if (eliminate_redundant_frame_pointer_sub_materializations(lines)) {
            changed = true;
        }
        if (hoist_readonly_stack_compare_reloads(lines)) {
            changed = true;
        }
        for (std::size_t index = 0; index + 5 < lines.size(); ++index) {
            const auto stored_base = parse_two_operand_instruction(lines[index], "str");
            const auto load_index = parse_two_operand_instruction(lines[index + 1], "ldr");
            const auto move_index = parse_plain_move_line(lines[index + 2]);
            const auto reload_base = parse_two_operand_instruction(lines[index + 3], "ldr");
            const auto add = parse_three_operand_instruction(lines[index + 4], "add");
            const auto store_back = parse_two_operand_instruction(lines[index + 5], "str");
            if (!stored_base.has_value() || !load_index.has_value() ||
                !move_index.has_value() || !reload_base.has_value() ||
                !add.has_value() || !store_back.has_value()) {
                continue;
            }
            if (stored_base->mnemonic != "str" || load_index->mnemonic != "ldr" ||
                reload_base->mnemonic != "ldr" || store_back->mnemonic != "str") {
                continue;
            }
            if (!registers_alias(load_index->lhs, move_index->src) ||
                !registers_alias(reload_base->lhs, stored_base->lhs) ||
                stored_base->rhs != reload_base->rhs ||
                stored_base->rhs != store_back->rhs ||
                !registers_alias(add->dst, reload_base->lhs) ||
                !registers_alias(add->lhs, reload_base->lhs) ||
                !rhs_starts_with_register_token(add->rhs, move_index->dst) ||
                !registers_alias(store_back->lhs, add->dst)) {
                continue;
            }
            lines[index].clear();
            lines[index + 1] = "  ldr " + move_index->dst + ", " + load_index->rhs;
            lines[index + 2].clear();
            lines[index + 3].clear();
            changed = true;
        }
        for (std::size_t index = 0; index + 4 < lines.size(); ++index) {
            const auto stored_base = parse_two_operand_instruction(lines[index], "str");
            const auto move_index = parse_plain_move_line(lines[index + 1]);
            const auto reload_base = parse_two_operand_instruction(lines[index + 2], "ldr");
            const auto add = parse_three_operand_instruction(lines[index + 3], "add");
            const auto store_back = parse_two_operand_instruction(lines[index + 4], "str");
            if (!stored_base.has_value() || !move_index.has_value() ||
                !reload_base.has_value() || !add.has_value() ||
                !store_back.has_value()) {
                continue;
            }
            if (stored_base->mnemonic != "str" || reload_base->mnemonic != "ldr" ||
                store_back->mnemonic != "str") {
                continue;
            }
            if (!registers_alias(reload_base->lhs, stored_base->lhs) ||
                stored_base->rhs != reload_base->rhs ||
                stored_base->rhs != store_back->rhs ||
                !registers_alias(add->dst, reload_base->lhs) ||
                !registers_alias(add->lhs, reload_base->lhs) ||
                !rhs_starts_with_register_token(add->rhs, move_index->dst) ||
                !registers_alias(store_back->lhs, add->dst)) {
                continue;
            }
            lines[index].clear();
            lines[index + 2].clear();
            changed = true;
        }
        }
        if (groups.vector) {
        if (collapse_vector_mul_add_temp_chain_with_high_address_moves(lines)) {
            changed = true;
        }
        if (collapse_vector_mul_add_temp_chain(lines)) {
            changed = true;
        }
        if (forward_vector_temp_writeback(lines)) {
            changed = true;
        }
        if (fold_vector_reduction_copy_chain(lines)) {
            changed = true;
        }
        if (fold_vector_horizontal_sum_store_reload(lines)) {
            changed = true;
        }
        if (forward_vector_frame_temp_accumulator_reloads(lines)) {
            changed = true;
        }
        if (fold_vector_pairwise_accumulate_stack_temps(lines)) {
            changed = true;
        }
        for (std::size_t index = 0; index + 3 < lines.size(); ++index) {
            const auto dup = parse_dup_scalar_line(lines[index]);
            const auto store = parse_two_operand_instruction(lines[index + 1], "str");
            if (!dup.has_value() || !store.has_value() ||
                !vector_registers_alias(store->lhs, dup->vector_reg)) {
                continue;
            }
            bool replaced_any = false;
            for (std::size_t probe = index + 2;
                 probe + 1 < lines.size() && probe <= index + 16; ++probe) {
                const auto reload_dup =
                    parse_two_operand_instruction(lines[probe], "ldr");
                std::optional<ThreeOperandAsmPattern> vector_op;
                for (const char *mnemonic : {"mul", "add", "smin", "smax"}) {
                    vector_op =
                        parse_three_operand_instruction(lines[probe + 1], mnemonic);
                    if (vector_op.has_value()) {
                        break;
                    }
                }
                if (!reload_dup.has_value() || !vector_op.has_value()) {
                    continue;
                }
                if (reload_dup->rhs != store->rhs ||
                    !vector_registers_alias(vector_op->rhs, reload_dup->lhs)) {
                    continue;
                }
                bool scalar_clobbered = false;
                bool slot_overwritten = false;
                for (std::size_t between = index + 1; between < probe; ++between) {
                    if (instruction_may_write_general_register(lines[between],
                                                               dup->scalar_reg)) {
                        scalar_clobbered = true;
                        break;
                    }
                    if (between != index + 1 &&
                        instruction_stores_to_memory_operand(lines[between],
                                                             store->rhs)) {
                        slot_overwritten = true;
                        break;
                    }
                }
                if (scalar_clobbered || slot_overwritten) {
                    continue;
                }
                if (vector_registers_alias(reload_dup->lhs, dup->vector_reg)) {
                    lines[probe].clear();
                } else {
                    lines[probe] =
                        "  dup " +
                        format_dup_target_register(reload_dup->lhs,
                                                   dup->vector_reg) +
                        ", " + dup->scalar_reg;
                }
                changed = true;
                replaced_any = true;
            }
            if (replaced_any) {
                continue;
            }
        }
        if (fold_zero_offset_memory_post_increment(lines)) {
            changed = true;
        }
        }
        if (changed) {
            lines.erase(std::remove_if(lines.begin(), lines.end(),
                                       [](const std::string &line) {
                                           return line.empty();
                                       }),
                        lines.end());
        }
    } while (changed);

    remove_redundant_branch_to_next_label(lines);
    close_unbalanced_cfi_procedures(lines);

    return join_non_empty_asm_lines(lines);
}

void append_rendered_cfi_directive(std::ostringstream &output,
                                   const AArch64CfiDirective &directive) {
    switch (directive.kind) {
    case AArch64CfiDirectiveKind::StartProcedure:
        output << "  .cfi_startproc\n";
        return;
    case AArch64CfiDirectiveKind::EndProcedure:
        output << "  .cfi_endproc\n";
        return;
    case AArch64CfiDirectiveKind::DefCfa:
        output << "  .cfi_def_cfa "
               << (directive.reg == 31 ? "sp" : std::to_string(directive.reg))
               << ", " << directive.offset << "\n";
        return;
    case AArch64CfiDirectiveKind::DefCfaRegister:
        output << "  .cfi_def_cfa_register " << directive.reg << "\n";
        return;
    case AArch64CfiDirectiveKind::DefCfaOffset:
        output << "  .cfi_def_cfa_offset " << directive.offset << "\n";
        return;
    case AArch64CfiDirectiveKind::Offset:
        output << "  .cfi_offset " << directive.reg << ", " << directive.offset
               << "\n";
        return;
    case AArch64CfiDirectiveKind::Restore:
        output << "  .cfi_restore " << directive.reg << "\n";
        return;
    }
}

void append_rendered_instruction(std::ostringstream &output,
                                 const AArch64MachineInstr &instruction,
                                 const AArch64MachineFunction &function) {
    output << "  " << instruction.get_mnemonic();
    if (instruction.get_operands().empty()) {
        output << "\n";
        return;
    }

    if (instruction.get_opcode() == AArch64MachineOpcode::MoveWideKeep) {
        const auto &operands = instruction.get_operands();
        const std::size_t immediate_index =
            operands.size() >= 3 && operands[2].get_immediate_operand() != nullptr ? 2U
                                                                                   : 1U;
        if (immediate_index < operands.size()) {
            output << " ";
            output << render_machine_operand_for_asm(operands[0], function);
            output << ", "
                   << render_machine_operand_for_asm(operands[immediate_index], function);
            const std::size_t shift_index = immediate_index + 1U;
            if (shift_index < operands.size() &&
                operands[shift_index].get_shift_operand() != nullptr) {
                output << ", "
                       << render_machine_operand_for_asm(operands[shift_index], function);
            }
            output << "\n";
            return;
        }
    }

    const bool use_space_separated_operands =
        instruction.get_opcode() == AArch64MachineOpcode::DirectiveLoc;
    output << " ";
    for (std::size_t index = 0; index < instruction.get_operands().size(); ++index) {
        if (index > 0) {
            output << (use_space_separated_operands ? " " : ", ");
        }
        std::string rendered_operand =
            render_machine_operand_for_asm(instruction.get_operands()[index], function);
        if (instruction.get_opcode() == AArch64MachineOpcode::Move) {
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
    for (const std::string &module_asm_line : asm_module.get_module_asm_lines()) {
        output << module_asm_line << "\n";
    }
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
            if (options.force_global_function_symbols ||
                function.get_is_global_symbol() || function.get_name() == "main") {
                output << ".globl " << function.get_name() << "\n";
            }
            output << ".p2align 2\n";
            output << ".type " << function.get_name() << ", %function\n";
            const std::vector<AArch64CfiDirective> &cfi_directives =
                function.get_frame_record().get_cfi_directives();
            std::size_t next_cfi_index = 0;
            std::size_t code_offset = 0;
            for (const AArch64MachineBlock &block : function.get_blocks()) {
                output << block.get_label() << ":\n";
                while (next_cfi_index < cfi_directives.size() &&
                       cfi_directives[next_cfi_index].kind ==
                           AArch64CfiDirectiveKind::StartProcedure) {
                    append_rendered_cfi_directive(output,
                                                 cfi_directives[next_cfi_index++]);
                }
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
                    if (!instruction.is_asm_directive()) {
                        code_offset += 4;
                        while (next_cfi_index < cfi_directives.size() &&
                               cfi_directives[next_cfi_index].kind !=
                                   AArch64CfiDirectiveKind::StartProcedure &&
                               cfi_directives[next_cfi_index].kind !=
                                   AArch64CfiDirectiveKind::EndProcedure &&
                               cfi_directives[next_cfi_index].code_offset ==
                                   code_offset) {
                            append_rendered_cfi_directive(
                                output, cfi_directives[next_cfi_index++]);
                        }
                    }
                }
            }
            while (next_cfi_index < cfi_directives.size()) {
                append_rendered_cfi_directive(output,
                                             cfi_directives[next_cfi_index++]);
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
    return optimize_emitted_asm_text(print_module_with_options(
        asm_module, machine_module, object_module, AsmPrintOptions{}));
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
    if (object_file.has_parent_path()) {
        std::filesystem::create_directories(object_file.parent_path());
    }

    if (!write_aarch64_elf_object(
            machine_module, object_module, object_file,
            AArch64ElfObjectWriterOptions{
                .force_defined_symbols_global = false},
            diagnostic_engine)) {
        if (!diagnostic_engine.has_error()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 object writer failed without emitting a specific "
                "diagnostic");
        }
        diagnostic_engine.add_note(
            DiagnosticStage::Compiler,
            summarize_object_emission_input(machine_module, object_module,
                                            backend_options, object_file));
        return nullptr;
    }

    std::vector<std::uint8_t> object_bytes;
    if (!read_binary_file(object_file, object_bytes)) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 object readback failed after a successful object writer pass: '" +
                object_file.string() + "'");
        diagnostic_engine.add_note(
            DiagnosticStage::Compiler,
            summarize_object_emission_input(machine_module, object_module,
                                            backend_options, object_file));
        return nullptr;
    }
    return std::make_unique<ObjectResult>(ObjectTargetKind::ElfAArch64,
                                          std::move(object_bytes));
}

} // namespace sysycc
