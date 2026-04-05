#include "backend/asm_gen/aarch64/aarch64_asm_backend.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

constexpr const char *kDefaultTargetTriple = "aarch64-unknown-linux-gnu";
constexpr unsigned kCallerSavedAllocatablePhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::X9),
    static_cast<unsigned>(AArch64PhysicalReg::X10),
    static_cast<unsigned>(AArch64PhysicalReg::X11),
    static_cast<unsigned>(AArch64PhysicalReg::X12),
    static_cast<unsigned>(AArch64PhysicalReg::X13),
    static_cast<unsigned>(AArch64PhysicalReg::X14),
    static_cast<unsigned>(AArch64PhysicalReg::X15),
};
constexpr unsigned kCalleeSavedAllocatablePhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::X19),
    static_cast<unsigned>(AArch64PhysicalReg::X20),
    static_cast<unsigned>(AArch64PhysicalReg::X21),
    static_cast<unsigned>(AArch64PhysicalReg::X22),
    static_cast<unsigned>(AArch64PhysicalReg::X23),
};
constexpr unsigned kSpillScratchPhysicalRegs[] = {
    static_cast<unsigned>(AArch64PhysicalReg::X24),
    static_cast<unsigned>(AArch64PhysicalReg::X25),
    static_cast<unsigned>(AArch64PhysicalReg::X26),
    static_cast<unsigned>(AArch64PhysicalReg::X27),
};
constexpr unsigned kSpillAddressPhysicalReg =
    static_cast<unsigned>(AArch64PhysicalReg::X28);

bool is_callee_saved_allocatable_physical_reg(unsigned reg) {
    return std::find(std::begin(kCalleeSavedAllocatablePhysicalRegs),
                     std::end(kCalleeSavedAllocatablePhysicalRegs),
                     reg) != std::end(kCalleeSavedAllocatablePhysicalRegs);
}

AArch64CallClobberMask make_default_call_clobber_mask() {
    return AArch64CallClobberMask({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                   13, 14, 15, 16, 17});
}

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

std::string sanitize_label_fragment(const std::string &text) {
    std::string sanitized;
    sanitized.reserve(text.size());
    for (char ch : text) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            sanitized.push_back(ch);
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        return "unnamed";
    }
    return sanitized;
}

bool is_integer_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Integer;
}

bool is_pointer_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Pointer;
}

bool is_void_type(const CoreIrType *type) {
    return type != nullptr && type->get_kind() == CoreIrTypeKind::Void;
}

const CoreIrIntegerType *as_integer_type(const CoreIrType *type) {
    if (!is_integer_type(type)) {
        return nullptr;
    }
    return static_cast<const CoreIrIntegerType *>(type);
}

bool is_narrow_integer_type(const CoreIrType *type) {
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return false;
    }
    const std::size_t bit_width = integer_type->get_bit_width();
    return bit_width == 1 || bit_width == 8 || bit_width == 16;
}

bool is_supported_scalar_storage_type(const CoreIrType *type) {
    if (is_pointer_type(type)) {
        return true;
    }
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return false;
    }
    const std::size_t bit_width = integer_type->get_bit_width();
    return bit_width == 1 || bit_width == 8 || bit_width == 16 ||
           bit_width == 32 || bit_width == 64;
}

bool is_supported_object_type(const CoreIrType *type) {
    if (type == nullptr) {
        return false;
    }
    if (is_supported_scalar_storage_type(type)) {
        return true;
    }
    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
        array_type != nullptr) {
        return is_supported_object_type(array_type->get_element_type());
    }
    if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
        struct_type != nullptr) {
        for (const CoreIrType *element_type : struct_type->get_element_types()) {
            if (!is_supported_object_type(element_type)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool is_supported_value_type(const CoreIrType *type) {
    return is_void_type(type) || is_supported_scalar_storage_type(type);
}

bool is_byte_string_global(const CoreIrGlobal &global) {
    const auto *array_type =
        dynamic_cast<const CoreIrArrayType *>(global.get_type());
    const auto *byte_string =
        dynamic_cast<const CoreIrConstantByteString *>(global.get_initializer());
    if (array_type == nullptr || byte_string == nullptr) {
        return false;
    }
    const auto *element_type = as_integer_type(array_type->get_element_type());
    return element_type != nullptr && element_type->get_bit_width() == 8 &&
           byte_string->get_bytes().size() == array_type->get_element_count();
}

std::size_t get_storage_size(const CoreIrType *type) {
    if (is_pointer_type(type)) {
        return 8;
    }
    const auto *integer_type = as_integer_type(type);
    if (integer_type == nullptr) {
        return 0;
    }
    if (integer_type->get_bit_width() == 1 ||
        integer_type->get_bit_width() == 8) {
        return 1;
    }
    if (integer_type->get_bit_width() == 16) {
        return 2;
    }
    if (integer_type->get_bit_width() <= 32) {
        return 4;
    }
    return 8;
}

std::size_t get_storage_alignment(const CoreIrType *type) {
    const std::size_t size = get_storage_size(type);
    if (size == 0) {
        return 0;
    }
    return std::min<std::size_t>(8, size);
}

std::size_t get_type_alignment(const CoreIrType *type);

std::size_t get_type_size(const CoreIrType *type) {
    if (type == nullptr) {
        return 0;
    }
    if (is_supported_scalar_storage_type(type)) {
        return get_storage_size(type);
    }
    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
        array_type != nullptr) {
        return get_type_size(array_type->get_element_type()) *
               array_type->get_element_count();
    }
    if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
        struct_type != nullptr) {
        std::size_t offset = 0;
        std::size_t max_alignment = 1;
        for (const CoreIrType *element_type : struct_type->get_element_types()) {
            const std::size_t alignment = get_type_alignment(element_type);
            max_alignment = std::max(max_alignment, alignment);
            offset = align_to(offset, alignment);
            offset += get_type_size(element_type);
        }
        return align_to(offset, max_alignment);
    }
    return 0;
}

std::size_t get_type_alignment(const CoreIrType *type) {
    if (type == nullptr) {
        return 0;
    }
    if (is_supported_scalar_storage_type(type)) {
        return get_storage_alignment(type);
    }
    if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
        array_type != nullptr) {
        return get_type_alignment(array_type->get_element_type());
    }
    if (const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
        struct_type != nullptr) {
        std::size_t max_alignment = 1;
        for (const CoreIrType *element_type : struct_type->get_element_types()) {
            max_alignment = std::max(max_alignment, get_type_alignment(element_type));
        }
        return max_alignment;
    }
    return 0;
}

std::size_t get_struct_member_offset(const CoreIrStructType *struct_type,
                                     std::size_t index) {
    if (struct_type == nullptr || index >= struct_type->get_element_types().size()) {
        return 0;
    }
    std::size_t offset = 0;
    for (std::size_t current = 0; current < index; ++current) {
        const CoreIrType *element_type = struct_type->get_element_types()[current];
        offset = align_to(offset, get_type_alignment(element_type));
        offset += get_type_size(element_type);
    }
    return align_to(offset,
                    get_type_alignment(struct_type->get_element_types()[index]));
}

bool uses_64bit_register(const CoreIrType *type) {
    return is_pointer_type(type) ||
           (as_integer_type(type) != nullptr &&
            as_integer_type(type)->get_bit_width() > 32);
}

std::string general_register_name(unsigned index, bool use_64bit) {
    return std::string(use_64bit ? "x" : "w") + std::to_string(index);
}

std::string zero_register_name(bool use_64bit) {
    return use_64bit ? "xzr" : "wzr";
}

std::string condition_code(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return "eq";
    case CoreIrComparePredicate::NotEqual:
        return "ne";
    case CoreIrComparePredicate::SignedLess:
        return "lt";
    case CoreIrComparePredicate::SignedLessEqual:
        return "le";
    case CoreIrComparePredicate::SignedGreater:
        return "gt";
    case CoreIrComparePredicate::SignedGreaterEqual:
        return "ge";
    case CoreIrComparePredicate::UnsignedLess:
        return "lo";
    case CoreIrComparePredicate::UnsignedLessEqual:
        return "ls";
    case CoreIrComparePredicate::UnsignedGreater:
        return "hi";
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return "hs";
    }
    return "eq";
}

std::string scalar_directive(const CoreIrType *type) {
    if (is_pointer_type(type) || get_storage_size(type) == 8) {
        return ".xword";
    }
    if (get_storage_size(type) == 2) {
        return ".hword";
    }
    if (get_storage_size(type) == 1) {
        return ".byte";
    }
    return ".word";
}

std::optional<long long> get_signed_integer_constant(const CoreIrConstant *constant) {
    const auto *int_constant = dynamic_cast<const CoreIrConstantInt *>(constant);
    if (int_constant == nullptr) {
        return std::nullopt;
    }
    const auto *integer_type = as_integer_type(constant->get_type());
    if (integer_type == nullptr) {
        return std::nullopt;
    }
    const std::size_t bit_width = integer_type->get_bit_width();
    if (bit_width == 0 || bit_width > 64) {
        return std::nullopt;
    }
    const std::uint64_t value = int_constant->get_value();
    if (bit_width == 64) {
        return static_cast<long long>(value);
    }
    const std::uint64_t sign_bit = 1ULL << (bit_width - 1);
    const std::uint64_t mask =
        bit_width == 64 ? ~0ULL : ((1ULL << bit_width) - 1ULL);
    const std::uint64_t masked = value & mask;
    if ((masked & sign_bit) == 0) {
        return static_cast<long long>(masked);
    }
    return static_cast<long long>(masked | ~mask);
}

void add_backend_error(DiagnosticEngine &diagnostic_engine,
                       const std::string &message) {
    diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
}

std::pair<std::string, std::vector<AArch64MachineOperand>>
parse_machine_instruction_text(std::string text) {
    const std::size_t separator = text.find(' ');
    if (separator == std::string::npos) {
        return {std::move(text), {}};
    }

    std::string mnemonic = text.substr(0, separator);
    std::vector<AArch64MachineOperand> operands;
    const std::string operand_text = text.substr(separator + 1);
    std::string current_operand;
    int bracket_depth = 0;
    for (std::size_t index = 0; index < operand_text.size(); ++index) {
        const char ch = operand_text[index];
        if (ch == '[') {
            ++bracket_depth;
        } else if (ch == ']') {
            --bracket_depth;
        }
        if (ch == ',' && bracket_depth == 0 &&
            index + 1 < operand_text.size() && operand_text[index + 1] == ' ') {
            operands.emplace_back(std::move(current_operand));
            current_operand.clear();
            ++index;
            continue;
        }
        current_operand.push_back(ch);
    }
    if (!current_operand.empty()) {
        operands.emplace_back(std::move(current_operand));
    }
    return {std::move(mnemonic), std::move(operands)};
}

std::string render_physical_register(unsigned reg_number, bool use_64bit) {
    return std::string(use_64bit ? "x" : "w") + std::to_string(reg_number);
}

std::string vreg_token(char role, const AArch64VirtualReg &reg) {
    return "%" + std::string(1, role) + std::to_string(reg.get_id()) +
           (reg.get_use_64bit() ? "x" : "w");
}

std::string vreg_token_with_width(char role, const AArch64VirtualReg &reg,
                                  bool use_64bit) {
    return "%" + std::string(1, role) + std::to_string(reg.get_id()) +
           (use_64bit ? "x" : "w");
}

std::string use_vreg(const AArch64VirtualReg &reg) {
    return vreg_token('u', reg);
}

std::string def_vreg(const AArch64VirtualReg &reg) {
    return vreg_token('d', reg);
}

std::string use_vreg_as(const AArch64VirtualReg &reg, bool use_64bit) {
    return vreg_token_with_width('u', reg, use_64bit);
}

std::string def_vreg_as(const AArch64VirtualReg &reg, bool use_64bit) {
    return vreg_token_with_width('d', reg, use_64bit);
}

struct ParsedVirtualRegRef {
    std::size_t id = 0;
    bool use_64bit = false;
    bool is_def = false;
    std::size_t offset = 0;
    std::size_t length = 0;
};

std::vector<ParsedVirtualRegRef> parse_virtual_reg_refs(const std::string &text) {
    std::vector<ParsedVirtualRegRef> refs;
    for (std::size_t index = 0; index + 3 < text.size(); ++index) {
        if (text[index] != '%' || (text[index + 1] != 'u' && text[index + 1] != 'd')) {
            continue;
        }
        std::size_t cursor = index + 2;
        if (!std::isdigit(static_cast<unsigned char>(text[cursor]))) {
            continue;
        }
        std::size_t id = 0;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
            id = (id * 10) + static_cast<std::size_t>(text[cursor] - '0');
            ++cursor;
        }
        if (cursor >= text.size() || (text[cursor] != 'x' && text[cursor] != 'w')) {
            continue;
        }
        refs.push_back({id, text[cursor] == 'x', text[index + 1] == 'd', index,
                        cursor - index + 1});
        index = cursor;
    }
    return refs;
}

std::unordered_set<unsigned>
collect_physical_regs_in_operand_text(const std::string &text) {
    std::unordered_set<unsigned> regs;
    for (std::size_t index = 0; index + 1 < text.size(); ++index) {
        if ((text[index] != 'x' && text[index] != 'w') ||
            !std::isdigit(static_cast<unsigned char>(text[index + 1]))) {
            continue;
        }
        if (index > 0) {
            const char previous = text[index - 1];
            if (std::isalnum(static_cast<unsigned char>(previous)) != 0 ||
                previous == '%') {
                continue;
            }
        }
        std::size_t cursor = index + 1;
        unsigned reg = 0;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
            reg = (reg * 10U) +
                  static_cast<unsigned>(text[cursor] - '0');
            ++cursor;
        }
        regs.insert(reg);
        index = cursor - 1;
    }
    return regs;
}

std::vector<std::size_t> collect_explicit_vreg_ids(
    const std::vector<AArch64MachineOperand> &operands, bool defs) {
    std::vector<std::size_t> ids;
    std::unordered_set<std::size_t> seen;
    for (const AArch64MachineOperand &operand : operands) {
        for (const ParsedVirtualRegRef &ref : parse_virtual_reg_refs(operand.get_text())) {
            if (ref.is_def != defs || seen.find(ref.id) != seen.end()) {
                continue;
            }
            seen.insert(ref.id);
            ids.push_back(ref.id);
        }
    }
    return ids;
}

struct AArch64InstructionLiveness {
    std::unordered_set<std::size_t> defs;
    std::unordered_set<std::size_t> uses;
};

struct AArch64BlockLiveness {
    std::vector<AArch64InstructionLiveness> instructions;
    std::unordered_set<std::size_t> defs;
    std::unordered_set<std::size_t> uses;
    std::unordered_set<std::size_t> live_in;
    std::unordered_set<std::size_t> live_out;
    std::vector<std::size_t> successors;
};

class AArch64LivenessInfo {
  private:
    std::vector<AArch64BlockLiveness> blocks_;
    std::unordered_set<std::size_t> live_across_call_;

  public:
    AArch64LivenessInfo(std::vector<AArch64BlockLiveness> blocks,
                        std::unordered_set<std::size_t> live_across_call)
        : blocks_(std::move(blocks)),
          live_across_call_(std::move(live_across_call)) {}

    const std::vector<AArch64BlockLiveness> &get_blocks() const noexcept {
        return blocks_;
    }

    const std::unordered_set<std::size_t> &get_live_across_call() const noexcept {
        return live_across_call_;
    }
};

class AArch64InterferenceGraph {
  private:
    std::unordered_map<std::size_t, std::unordered_set<std::size_t>> adjacency_;

  public:
    void add_node(std::size_t virtual_reg_id) { adjacency_[virtual_reg_id]; }

    void add_edge(std::size_t lhs, std::size_t rhs) {
        if (lhs == rhs) {
            return;
        }
        adjacency_[lhs].insert(rhs);
        adjacency_[rhs].insert(lhs);
    }

    void add_clique(const std::unordered_set<std::size_t> &live_set) {
        if (live_set.size() < 2) {
            for (std::size_t virtual_reg_id : live_set) {
                add_node(virtual_reg_id);
            }
            return;
        }
        std::vector<std::size_t> nodes(live_set.begin(), live_set.end());
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            for (std::size_t other_index = index + 1; other_index < nodes.size();
                 ++other_index) {
                add_edge(nodes[index], nodes[other_index]);
            }
        }
    }

    const std::unordered_map<std::size_t, std::unordered_set<std::size_t>> &
    get_adjacency() const noexcept {
        return adjacency_;
    }

    const std::unordered_set<std::size_t> &
    get_neighbors(std::size_t virtual_reg_id) const noexcept {
        static const std::unordered_set<std::size_t> empty;
        const auto it = adjacency_.find(virtual_reg_id);
        if (it == adjacency_.end()) {
            return empty;
        }
        return it->second;
    }
};

AArch64InstructionLiveness
collect_instruction_defs_uses(const AArch64MachineInstr &instruction) {
    AArch64InstructionLiveness liveness;
    for (std::size_t id : instruction.get_explicit_defs()) {
        liveness.defs.insert(id);
    }
    for (std::size_t id : instruction.get_explicit_uses()) {
        liveness.uses.insert(id);
    }
    for (std::size_t id : instruction.get_implicit_defs()) {
        liveness.defs.insert(id);
    }
    for (std::size_t id : instruction.get_implicit_uses()) {
        liveness.uses.insert(id);
    }
    return liveness;
}

std::vector<std::size_t> collect_block_successors(
    const AArch64MachineFunction &function,
    const std::unordered_map<std::string, std::size_t> &label_to_index,
    std::size_t block_index) {
    const auto &blocks = function.get_blocks();
    std::vector<std::size_t> successors;
    if (block_index >= blocks.size()) {
        return successors;
    }

    const auto &instructions = blocks[block_index].get_instructions();
    if (instructions.empty()) {
        if (block_index + 1 < blocks.size()) {
            successors.push_back(block_index + 1);
        }
        return successors;
    }

    const AArch64MachineInstr &last = instructions.back();
    if (last.get_mnemonic() == "ret") {
        return successors;
    }

    if (instructions.size() >= 2) {
        const AArch64MachineInstr &second_last = instructions[instructions.size() - 2];
        if (second_last.get_mnemonic() == "cbnz" && last.get_mnemonic() == "b") {
            if (second_last.get_operands().size() >= 2) {
                const auto it =
                    label_to_index.find(second_last.get_operands()[1].get_text());
                if (it != label_to_index.end()) {
                    successors.push_back(it->second);
                }
            }
            if (!last.get_operands().empty()) {
                const auto it = label_to_index.find(last.get_operands()[0].get_text());
                if (it != label_to_index.end()) {
                    successors.push_back(it->second);
                }
            }
            return successors;
        }
    }

    if (last.get_mnemonic() == "cbnz") {
        if (last.get_operands().size() >= 2) {
            const auto it = label_to_index.find(last.get_operands()[1].get_text());
            if (it != label_to_index.end()) {
                successors.push_back(it->second);
            }
        }
        if (block_index + 1 < blocks.size()) {
            successors.push_back(block_index + 1);
        }
        return successors;
    }

    if (last.get_mnemonic() == "b") {
        if (!last.get_operands().empty()) {
            const auto it = label_to_index.find(last.get_operands()[0].get_text());
            if (it != label_to_index.end()) {
                successors.push_back(it->second);
            }
        }
        return successors;
    }

    if (block_index + 1 < blocks.size()) {
        successors.push_back(block_index + 1);
    }
    return successors;
}

AArch64LivenessInfo build_liveness_info(const AArch64MachineFunction &function) {
    std::unordered_map<std::string, std::size_t> label_to_index;
    std::vector<AArch64BlockLiveness> block_info(function.get_blocks().size());
    for (std::size_t block_index = 0; block_index < function.get_blocks().size();
         ++block_index) {
        label_to_index.emplace(function.get_blocks()[block_index].get_label(),
                               block_index);
    }

    for (std::size_t block_index = 0; block_index < function.get_blocks().size();
         ++block_index) {
        const AArch64MachineBlock &block = function.get_blocks()[block_index];
        AArch64BlockLiveness &info = block_info[block_index];
        for (const AArch64MachineInstr &instruction : block.get_instructions()) {
            AArch64InstructionLiveness instruction_liveness =
                collect_instruction_defs_uses(instruction);
            for (std::size_t virtual_reg_id : instruction_liveness.uses) {
                if (info.defs.find(virtual_reg_id) == info.defs.end()) {
                    info.uses.insert(virtual_reg_id);
                }
            }
            info.defs.insert(instruction_liveness.defs.begin(),
                             instruction_liveness.defs.end());
            info.instructions.push_back(std::move(instruction_liveness));
        }
        info.successors = collect_block_successors(function, label_to_index, block_index);
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t reverse_index = block_info.size(); reverse_index > 0;
             --reverse_index) {
            AArch64BlockLiveness &info = block_info[reverse_index - 1];
            std::unordered_set<std::size_t> new_live_out;
            for (std::size_t successor : info.successors) {
                const AArch64BlockLiveness &successor_info = block_info[successor];
                new_live_out.insert(successor_info.live_in.begin(),
                                    successor_info.live_in.end());
            }
            std::unordered_set<std::size_t> new_live_in = info.uses;
            for (std::size_t vreg : new_live_out) {
                if (info.defs.find(vreg) == info.defs.end()) {
                    new_live_in.insert(vreg);
                }
            }
            if (new_live_in != info.live_in || new_live_out != info.live_out) {
                info.live_in = std::move(new_live_in);
                info.live_out = std::move(new_live_out);
                changed = true;
            }
        }
    }
    std::unordered_set<std::size_t> live_across_call;
    for (std::size_t block_index = 0; block_index < block_info.size(); ++block_index) {
        const AArch64BlockLiveness &info = block_info[block_index];
        std::unordered_set<std::size_t> live = info.live_out;
        const AArch64MachineBlock &block = function.get_blocks()[block_index];
        for (std::size_t instruction_index = info.instructions.size();
             instruction_index > 0; --instruction_index) {
            const AArch64InstructionLiveness &instruction_liveness =
                info.instructions[instruction_index - 1];
            const AArch64MachineInstr &instruction =
                block.get_instructions()[instruction_index - 1];
            if (instruction.get_flags().is_call) {
                live_across_call.insert(live.begin(), live.end());
            }
            for (std::size_t def : instruction_liveness.defs) {
                live.erase(def);
            }
            live.insert(instruction_liveness.uses.begin(),
                        instruction_liveness.uses.end());
        }
    }
    return AArch64LivenessInfo(std::move(block_info), std::move(live_across_call));
}

AArch64InterferenceGraph build_interference_graph(
    const AArch64MachineFunction &function,
    const AArch64LivenessInfo &liveness_info) {
    AArch64InterferenceGraph graph;
    for (const auto &[virtual_reg_id, _] : function.get_virtual_reg_widths()) {
        graph.add_node(virtual_reg_id);
    }

    const auto &block_info = liveness_info.get_blocks();
    for (std::size_t block_index = 0; block_index < block_info.size(); ++block_index) {
        const AArch64BlockLiveness &info = block_info[block_index];
        std::unordered_set<std::size_t> live = info.live_out;
        graph.add_clique(live);

        for (std::size_t instruction_index = info.instructions.size();
             instruction_index > 0; --instruction_index) {
            const AArch64InstructionLiveness &instruction_liveness =
                info.instructions[instruction_index - 1];
            for (std::size_t def : instruction_liveness.defs) {
                graph.add_node(def);
                for (std::size_t live_virtual_reg : live) {
                    graph.add_edge(def, live_virtual_reg);
                }
            }
            for (std::size_t def : instruction_liveness.defs) {
                live.erase(def);
            }
            live.insert(instruction_liveness.uses.begin(),
                        instruction_liveness.uses.end());
            graph.add_clique(live);
        }
        graph.add_clique(info.live_in);
    }

    return graph;
}

std::string load_mnemonic_for_width(bool use_64bit, std::size_t size) {
    if (use_64bit || size == 8) {
        return "ldr";
    }
    if (size == 2) {
        return "ldrh";
    }
    if (size == 1) {
        return "ldrb";
    }
    return "ldr";
}

std::string store_mnemonic_for_width(bool use_64bit, std::size_t size) {
    if (use_64bit || size == 8) {
        return "str";
    }
    if (size == 2) {
        return "strh";
    }
    if (size == 1) {
        return "strb";
    }
    return "str";
}

std::vector<ParsedVirtualRegRef>
collect_spilled_virtual_refs(const AArch64MachineOperand &operand,
                             const AArch64MachineFunction &function) {
    std::vector<ParsedVirtualRegRef> spilled;
    for (const ParsedVirtualRegRef &ref : parse_virtual_reg_refs(operand.get_text())) {
        if (function.get_physical_reg_for_virtual(ref.id).has_value()) {
            continue;
        }
        if (!function.get_frame_info().get_virtual_reg_spill_offset(ref.id).has_value()) {
            continue;
        }
        spilled.push_back(ref);
    }
    return spilled;
}

struct AArch64SpillRewriteOperand {
    ParsedVirtualRegRef ref;
    bool has_use = false;
    bool has_def = false;
};

struct AArch64ScratchAssignment {
    std::size_t virtual_reg_id = 0;
    unsigned physical_reg = 0;
    bool use_64bit = false;
};

struct AArch64SpillRewritePlan {
    std::vector<AArch64SpillRewriteOperand> operands;
    std::vector<AArch64ScratchAssignment> assignments;
    bool needs_address_scratch = false;
};

void append_materialize_physical_integer_constant(
    std::vector<AArch64MachineInstr> &instructions, unsigned physical_reg,
    std::uint64_t value) {
    const std::string reg = render_physical_register(physical_reg, true);
    bool emitted = false;
    for (unsigned piece = 0; piece < 4U; ++piece) {
        const std::uint16_t imm16 =
            static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
        if (!emitted) {
            instructions.emplace_back("movz " + reg + ", #" +
                                      std::to_string(imm16) + ", lsl #" +
                                      std::to_string(piece * 16U));
            emitted = true;
            continue;
        }
        if (imm16 == 0) {
            continue;
        }
        instructions.emplace_back("movk " + reg + ", #" +
                                  std::to_string(imm16) + ", lsl #" +
                                  std::to_string(piece * 16U));
    }
    if (!emitted) {
        instructions.emplace_back("mov " + reg + ", xzr");
    }
}

void append_frame_address_into_physical_reg(
    std::vector<AArch64MachineInstr> &instructions, unsigned address_reg,
    std::size_t offset) {
    const std::string reg = render_physical_register(address_reg, true);
    if (offset <= 4095) {
        instructions.emplace_back("sub " + reg + ", x29, #" + std::to_string(offset));
        return;
    }
    append_materialize_physical_integer_constant(instructions, address_reg, offset);
    instructions.emplace_back("sub " + reg + ", x29, " + reg);
}

void append_physical_frame_load(std::vector<AArch64MachineInstr> &instructions,
                                unsigned value_reg, bool use_64bit,
                                std::size_t size, std::size_t offset,
                                unsigned address_temp_reg) {
    const std::string reg = render_physical_register(value_reg, use_64bit);
    const std::string mnemonic = load_mnemonic_for_width(use_64bit, size);
    if (offset <= 255) {
        instructions.emplace_back(mnemonic + " " + reg + ", [x29, #-" +
                                  std::to_string(offset) + "]");
        return;
    }
    append_frame_address_into_physical_reg(instructions, address_temp_reg, offset);
    instructions.emplace_back(mnemonic + " " + reg + ", [" +
                              render_physical_register(address_temp_reg, true) + "]");
}

void append_physical_frame_store(std::vector<AArch64MachineInstr> &instructions,
                                 unsigned value_reg, bool use_64bit,
                                 std::size_t size, std::size_t offset,
                                 unsigned address_temp_reg) {
    const std::string reg = render_physical_register(value_reg, use_64bit);
    const std::string mnemonic = store_mnemonic_for_width(use_64bit, size);
    if (offset <= 255) {
        instructions.emplace_back(mnemonic + " " + reg + ", [x29, #-" +
                                  std::to_string(offset) + "]");
        return;
    }
    append_frame_address_into_physical_reg(instructions, address_temp_reg, offset);
    instructions.emplace_back(mnemonic + " " + reg + ", [" +
                              render_physical_register(address_temp_reg, true) + "]");
}

std::vector<ParsedVirtualRegRef>
collect_instruction_spilled_virtual_refs(const AArch64MachineInstr &instruction,
                                         const AArch64MachineFunction &function) {
    std::vector<ParsedVirtualRegRef> spilled;
    std::unordered_set<std::size_t> seen;
    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        for (const ParsedVirtualRegRef &ref :
             collect_spilled_virtual_refs(operand, function)) {
            if (seen.find(ref.id) != seen.end()) {
                continue;
            }
            seen.insert(ref.id);
            spilled.push_back(ref);
        }
    }
    return spilled;
}

AArch64SpillRewritePlan
build_spill_rewrite_plan(const AArch64MachineInstr &instruction,
                         const AArch64MachineFunction &function) {
    AArch64SpillRewritePlan plan;
    std::unordered_map<std::size_t, std::size_t> operand_indices;

    for (const AArch64MachineOperand &operand : instruction.get_operands()) {
        for (const ParsedVirtualRegRef &ref :
             collect_spilled_virtual_refs(operand, function)) {
            std::size_t entry_index = 0;
            const auto existing_it = operand_indices.find(ref.id);
            if (existing_it == operand_indices.end()) {
                entry_index = plan.operands.size();
                operand_indices.emplace(ref.id, entry_index);
                plan.operands.push_back(AArch64SpillRewriteOperand{ref});
            } else {
                entry_index = existing_it->second;
            }
            plan.operands[entry_index].has_use =
                plan.operands[entry_index].has_use || !ref.is_def;
            plan.operands[entry_index].has_def =
                plan.operands[entry_index].has_def || ref.is_def;
        }
    }

    plan.needs_address_scratch = false;
    for (const AArch64SpillRewriteOperand &operand : plan.operands) {
        const auto maybe_offset =
            function.get_frame_info().get_virtual_reg_spill_offset(operand.ref.id);
        if (maybe_offset.has_value() && *maybe_offset > 255) {
            plan.needs_address_scratch = true;
            break;
        }
    }

    std::vector<unsigned> available_value_scratch_regs(
        std::begin(kSpillScratchPhysicalRegs), std::end(kSpillScratchPhysicalRegs));
    if (!plan.needs_address_scratch) {
        available_value_scratch_regs.push_back(kSpillAddressPhysicalReg);
    }

    for (std::size_t index = 0;
         index < plan.operands.size() && index < available_value_scratch_regs.size();
         ++index) {
        plan.assignments.push_back(AArch64ScratchAssignment{
            plan.operands[index].ref.id, available_value_scratch_regs[index],
            plan.operands[index].ref.use_64bit});
    }
    return plan;
}

std::string substitute_spilled_virtuals(const std::string &text,
                                        const std::unordered_map<std::size_t, unsigned> &mapping,
                                        const AArch64MachineFunction &function) {
    std::string rendered = text;
    const std::vector<ParsedVirtualRegRef> refs = parse_virtual_reg_refs(text);
    std::size_t delta = 0;
    for (const ParsedVirtualRegRef &ref : refs) {
        if (function.get_physical_reg_for_virtual(ref.id).has_value()) {
            continue;
        }
        const auto it = mapping.find(ref.id);
        if (it == mapping.end()) {
            continue;
        }
        const std::string reg_name =
            render_physical_register(it->second, ref.use_64bit);
        rendered.replace(ref.offset + delta, ref.length, reg_name);
        delta += reg_name.size() - ref.length;
    }
    return rendered;
}

bool rewrite_spilled_virtual_registers(AArch64MachineFunction &function,
                                       DiagnosticEngine &diagnostic_engine) {
    for (AArch64MachineBlock &block : function.get_blocks()) {
        std::vector<AArch64MachineInstr> rewritten;
        for (AArch64MachineInstr &instruction : block.get_instructions()) {
            const AArch64SpillRewritePlan rewrite_plan =
                build_spill_rewrite_plan(instruction, function);
            if (rewrite_plan.operands.size() > std::size(kSpillScratchPhysicalRegs)) {
                add_backend_error(
                    diagnostic_engine,
                    "AArch64 spill rewrite cannot assign enough value scratch registers for instruction '" +
                        instruction.get_mnemonic() + "'");
                return false;
            }

            std::unordered_map<std::size_t, unsigned> spill_mapping;
            for (const AArch64ScratchAssignment &assignment : rewrite_plan.assignments) {
                spill_mapping.emplace(assignment.virtual_reg_id, assignment.physical_reg);
                function.get_frame_info().mark_saved_physical_reg(
                    assignment.physical_reg);
            }
            if (rewrite_plan.needs_address_scratch) {
                function.get_frame_info().mark_saved_physical_reg(
                    kSpillAddressPhysicalReg);
            }

            std::unordered_set<std::size_t> loaded_ids;
            for (const AArch64SpillRewriteOperand &operand_plan : rewrite_plan.operands) {
                if (!operand_plan.has_use) {
                    continue;
                }
                const std::size_t id = operand_plan.ref.id;
                const unsigned physical_reg = spill_mapping.at(id);
                const auto maybe_offset =
                    function.get_frame_info().get_virtual_reg_spill_offset(id);
                if (!maybe_offset.has_value()) {
                    continue;
                }
                if (loaded_ids.find(id) != loaded_ids.end()) {
                    continue;
                }
                append_physical_frame_load(
                    rewritten, physical_reg, operand_plan.ref.use_64bit,
                    operand_plan.ref.use_64bit ? 8 : 4, *maybe_offset,
                    kSpillAddressPhysicalReg);
                loaded_ids.insert(id);
            }

            std::vector<AArch64MachineOperand> operands;
            operands.reserve(instruction.get_operands().size());
            for (const AArch64MachineOperand &operand : instruction.get_operands()) {
                operands.emplace_back(
                    substitute_spilled_virtuals(operand.get_text(), spill_mapping, function));
            }
            rewritten.emplace_back(instruction.get_mnemonic(), std::move(operands),
                                   instruction.get_flags(),
                                   instruction.get_implicit_defs(),
                                   instruction.get_implicit_uses(),
                                   instruction.get_call_clobber_mask());

            std::unordered_set<std::size_t> stored_ids;
            for (const AArch64SpillRewriteOperand &operand_plan : rewrite_plan.operands) {
                if (!operand_plan.has_def) {
                    continue;
                }
                const std::size_t id = operand_plan.ref.id;
                const unsigned physical_reg = spill_mapping.at(id);
                const auto maybe_offset =
                    function.get_frame_info().get_virtual_reg_spill_offset(id);
                if (!maybe_offset.has_value()) {
                    continue;
                }
                if (stored_ids.find(id) != stored_ids.end()) {
                    continue;
                }
                append_physical_frame_store(
                    rewritten, physical_reg, operand_plan.ref.use_64bit,
                    operand_plan.ref.use_64bit ? 8 : 4, *maybe_offset,
                    kSpillAddressPhysicalReg);
                stored_ids.insert(id);
            }
        }
        block.get_instructions() = std::move(rewritten);
    }
    return true;
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
    append_physical_frame_store(instructions, physical_reg, true, 8, offset,
                                static_cast<unsigned>(AArch64PhysicalReg::X9));
}

void append_saved_reg_load(std::vector<AArch64MachineInstr> &instructions,
                           unsigned physical_reg, std::size_t offset) {
    append_physical_frame_load(instructions, physical_reg, true, 8, offset,
                               static_cast<unsigned>(AArch64PhysicalReg::X9));
}

void rebuild_function_entry_exit(AArch64MachineFunction &function) {
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
    prologue.emplace_back("stp x29, x30, [sp, #-16]!");
    prologue.emplace_back("mov x29, sp");
    if (function.get_frame_info().get_frame_size() > 0) {
        prologue.emplace_back("sub sp, sp, #" +
                              std::to_string(function.get_frame_info().get_frame_size()));
    }
    for (unsigned reg : function.get_frame_info().get_saved_physical_regs()) {
        append_saved_reg_store(prologue, reg,
                               function.get_frame_info().get_saved_physical_reg_offset(reg));
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
    }
    if (function.get_frame_info().get_frame_size() > 0) {
        epilogue.emplace_back("add sp, sp, #" +
                              std::to_string(function.get_frame_info().get_frame_size()));
    }
    epilogue.emplace_back("ldp x29, x30, [sp], #16");
    epilogue.emplace_back("ret");
    function.get_blocks().back().get_instructions() = std::move(epilogue);
}

bool allocate_virtual_registers(AArch64MachineFunction &function,
                                DiagnosticEngine &diagnostic_engine) {
    const AArch64LivenessInfo liveness = build_liveness_info(function);
    const AArch64InterferenceGraph graph =
        build_interference_graph(function, liveness);
    std::unordered_map<std::size_t, std::unordered_set<std::size_t>> working_graph =
        graph.get_adjacency();

    struct SimplifyNode {
        std::size_t virtual_reg_id = 0;
        std::size_t degree = 0;
        bool live_across_call = false;
    };

    std::vector<SimplifyNode> simplify_stack;
    const std::size_t color_count =
        std::size(kCallerSavedAllocatablePhysicalRegs) +
        std::size(kCalleeSavedAllocatablePhysicalRegs);

    while (!working_graph.empty()) {
        std::optional<SimplifyNode> low_degree_choice;
        std::optional<SimplifyNode> spill_choice;
        for (const auto &[virtual_reg_id, neighbors] : working_graph) {
            const SimplifyNode candidate{
                virtual_reg_id, neighbors.size(),
                liveness.get_live_across_call().find(virtual_reg_id) !=
                    liveness.get_live_across_call().end()};
            if (candidate.degree < color_count) {
                if (!low_degree_choice.has_value() ||
                    candidate.degree > low_degree_choice->degree ||
                    (candidate.degree == low_degree_choice->degree &&
                     candidate.virtual_reg_id < low_degree_choice->virtual_reg_id)) {
                    low_degree_choice = candidate;
                }
                continue;
            }
            if (!spill_choice.has_value() ||
                candidate.degree > spill_choice->degree ||
                (candidate.degree == spill_choice->degree &&
                 candidate.virtual_reg_id < spill_choice->virtual_reg_id)) {
                spill_choice = candidate;
            }
        }

        const SimplifyNode chosen =
            low_degree_choice.has_value() ? *low_degree_choice : *spill_choice;
        simplify_stack.push_back(chosen);

        const auto node_it = working_graph.find(chosen.virtual_reg_id);
        if (node_it == working_graph.end()) {
            continue;
        }
        const std::vector<std::size_t> neighbors(node_it->second.begin(),
                                                 node_it->second.end());
        for (std::size_t neighbor : neighbors) {
            const auto neighbor_it = working_graph.find(neighbor);
            if (neighbor_it == working_graph.end()) {
                continue;
            }
            neighbor_it->second.erase(chosen.virtual_reg_id);
        }
        working_graph.erase(node_it);
    }

    while (!simplify_stack.empty()) {
        const SimplifyNode node = simplify_stack.back();
        simplify_stack.pop_back();

        std::set<unsigned> available;
        if (node.live_across_call) {
            available.insert(std::begin(kCalleeSavedAllocatablePhysicalRegs),
                             std::end(kCalleeSavedAllocatablePhysicalRegs));
        } else {
            available.insert(std::begin(kCallerSavedAllocatablePhysicalRegs),
                             std::end(kCallerSavedAllocatablePhysicalRegs));
            available.insert(std::begin(kCalleeSavedAllocatablePhysicalRegs),
                             std::end(kCalleeSavedAllocatablePhysicalRegs));
        }
        for (std::size_t neighbor : graph.get_neighbors(node.virtual_reg_id)) {
            const std::optional<unsigned> neighbor_reg =
                function.get_physical_reg_for_virtual(neighbor);
            if (neighbor_reg.has_value()) {
                available.erase(*neighbor_reg);
            }
        }

        if (!available.empty()) {
            const unsigned chosen_reg = *available.begin();
            function.set_virtual_reg_allocation(node.virtual_reg_id, chosen_reg);
            if (is_callee_saved_allocatable_physical_reg(chosen_reg)) {
                function.get_frame_info().mark_saved_physical_reg(chosen_reg);
            }
            continue;
        }

        const std::size_t spill_size =
            function.get_virtual_reg_use_64bit(node.virtual_reg_id) ? 8 : 4;
        const std::size_t spill_alignment = spill_size;
        std::size_t local_size = function.get_frame_info().get_local_size();
        local_size = align_to(local_size, spill_alignment);
        local_size += spill_size;
        function.get_frame_info().set_virtual_reg_spill_offset(node.virtual_reg_id,
                                                               local_size);
        function.get_frame_info().set_local_size(local_size);
        function.get_frame_info().set_frame_size(align_to(local_size, 16));
    }

    if (!rewrite_spilled_virtual_registers(function, diagnostic_engine)) {
        return false;
    }
    rebuild_function_entry_exit(function);
    return true;
}

std::string substitute_virtual_registers(const std::string &text,
                                         const AArch64MachineFunction &function) {
    std::string rendered = text;
    const std::vector<ParsedVirtualRegRef> refs = parse_virtual_reg_refs(text);
    std::size_t delta = 0;
    for (const ParsedVirtualRegRef &ref : refs) {
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(ref.id);
        if (!physical_reg.has_value()) {
            continue;
        }
        const std::string reg_name =
            render_physical_register(*physical_reg, ref.use_64bit);
        rendered.replace(ref.offset + delta, ref.length, reg_name);
        delta += reg_name.size() - ref.length;
    }
    return rendered;
}

enum class AArch64ValueLocationKind : unsigned char {
    VirtualReg,
    ConstantInt,
    ConstantNull,
    SymbolAddress,
    StackSlotAddress,
    None,
};

struct AArch64ValueLocation {
    AArch64ValueLocationKind kind = AArch64ValueLocationKind::None;
    AArch64VirtualReg virtual_reg;
};

class AArch64LoweringSession {
  private:
    const CoreIrModule &module_;
    const BackendOptions &backend_options_;
    DiagnosticEngine &diagnostic_engine_;
    AArch64AsmPrinter printer_;
    std::unordered_map<const CoreIrBasicBlock *, std::string> block_labels_;

    struct FunctionState {
        AArch64MachineFunction *machine_function = nullptr;
        std::unordered_map<const CoreIrParameter *, std::size_t>
            incoming_stack_argument_offsets;
        std::unordered_map<const CoreIrValue *, AArch64ValueLocation> value_locations;
        std::unordered_set<const CoreIrStackSlot *> promoted_stack_slots;
        std::unordered_map<const CoreIrStackSlot *, AArch64VirtualReg>
            promoted_stack_slot_values;
    };

  public:
    AArch64LoweringSession(const CoreIrModule &module,
                           const BackendOptions &backend_options,
                           DiagnosticEngine &diagnostic_engine)
        : module_(module),
          backend_options_(backend_options),
          diagnostic_engine_(diagnostic_engine) {}

    std::unique_ptr<AsmResult> Generate() {
        if (!backend_options_.get_target_triple().empty() &&
            backend_options_.get_target_triple() != kDefaultTargetTriple) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported AArch64 native target triple: " +
                    backend_options_.get_target_triple());
            return nullptr;
        }

        AArch64MachineModule machine_module;
        machine_module.append_global_line(".arch armv8-a");

        if (!append_globals(machine_module) || !append_functions(machine_module)) {
            return nullptr;
        }

        return std::make_unique<AsmResult>(AsmTargetKind::AArch64,
                                           printer_.print_module(machine_module));
    }

  private:
    AArch64VirtualReg create_virtual_reg(AArch64MachineFunction &function,
                                         const CoreIrType *type) const {
        return function.create_virtual_reg(uses_64bit_register(type));
    }

    AArch64VirtualReg create_pointer_virtual_reg(
        AArch64MachineFunction &function) const {
        return function.create_virtual_reg(true);
    }

    static bool instruction_has_canonical_vreg(const CoreIrInstruction &instruction) {
        switch (instruction.get_opcode()) {
        case CoreIrOpcode::Phi:
        case CoreIrOpcode::Load:
        case CoreIrOpcode::Binary:
        case CoreIrOpcode::Unary:
        case CoreIrOpcode::Compare:
        case CoreIrOpcode::Cast:
        case CoreIrOpcode::Call:
            return !is_void_type(instruction.get_type());
        case CoreIrOpcode::AddressOfStackSlot:
        case CoreIrOpcode::AddressOfGlobal:
        case CoreIrOpcode::AddressOfFunction:
        case CoreIrOpcode::GetElementPtr:
            return true;
        case CoreIrOpcode::Store:
        case CoreIrOpcode::Jump:
        case CoreIrOpcode::CondJump:
        case CoreIrOpcode::Return:
            return false;
        }
        return false;
    }

    void assign_virtual_value_location(FunctionState &state,
                                       const CoreIrValue *value,
                                       AArch64VirtualReg vreg) const {
        state.value_locations[value] =
            AArch64ValueLocation{AArch64ValueLocationKind::VirtualReg, vreg};
    }

    const AArch64ValueLocation *
    lookup_value_location(const FunctionState &state,
                          const CoreIrValue *value) const {
        const auto it = state.value_locations.find(value);
        if (it == state.value_locations.end()) {
            return nullptr;
        }
        return &it->second;
    }

    bool require_canonical_vreg(const FunctionState &state,
                                const CoreIrValue *value,
                                AArch64VirtualReg &out) const {
        const AArch64ValueLocation *location = lookup_value_location(state, value);
        if (location == nullptr ||
            location->kind != AArch64ValueLocationKind::VirtualReg ||
            !location->virtual_reg.is_valid()) {
            add_backend_error(diagnostic_engine_,
                              "missing canonical AArch64 virtual register for Core IR value");
            return false;
        }
        out = location->virtual_reg;
        return true;
    }

    bool seed_function_value_locations(const CoreIrFunction &function,
                                       FunctionState &state) {
        for (const auto &parameter : function.get_parameters()) {
            if (!is_supported_value_type(parameter->get_type())) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported parameter type in AArch64 native "
                                  "backend for function '" +
                                      function.get_name() + "'");
                return false;
            }
            assign_virtual_value_location(
                state, parameter.get(),
                create_virtual_reg(*state.machine_function, parameter->get_type()));
        }

        for (const auto &basic_block : function.get_basic_blocks()) {
            for (const auto &instruction : basic_block->get_instructions()) {
                if (!instruction_has_canonical_vreg(*instruction)) {
                    continue;
                }
                if (!is_supported_value_type(instruction->get_type())) {
                    add_backend_error(
                        diagnostic_engine_,
                        "unsupported Core IR value type in AArch64 native backend "
                        "for function '" +
                            function.get_name() + "'");
                    return false;
                }
                assign_virtual_value_location(
                    state, instruction.get(),
                    create_virtual_reg(*state.machine_function, instruction->get_type()));
            }
        }
        return true;
    }

    void seed_promoted_stack_slots(const CoreIrFunction &function,
                                   FunctionState &state) {
        const CoreIrBasicBlock *entry_block =
            function.get_basic_blocks().empty() ? nullptr :
                                                  function.get_basic_blocks().front().get();
        std::unordered_set<const CoreIrStackSlot *> address_taken;
        std::unordered_map<const CoreIrStackSlot *, std::size_t> direct_store_count;
        std::unordered_map<const CoreIrStackSlot *, const CoreIrStoreInst *> entry_store;

        for (const auto &basic_block : function.get_basic_blocks()) {
            for (const auto &instruction : basic_block->get_instructions()) {
                if (const auto *address_of_stack_slot =
                        dynamic_cast<const CoreIrAddressOfStackSlotInst *>(instruction.get());
                    address_of_stack_slot != nullptr) {
                    address_taken.insert(address_of_stack_slot->get_stack_slot());
                    continue;
                }
                const auto *store =
                    dynamic_cast<const CoreIrStoreInst *>(instruction.get());
                if (store == nullptr || store->get_stack_slot() == nullptr) {
                    continue;
                }
                ++direct_store_count[store->get_stack_slot()];
                if (basic_block.get() == entry_block) {
                    entry_store.emplace(store->get_stack_slot(), store);
                }
            }
        }

        for (const auto &stack_slot : function.get_stack_slots()) {
            if (!is_supported_value_type(stack_slot->get_allocated_type())) {
                continue;
            }
            if (address_taken.find(stack_slot.get()) != address_taken.end()) {
                continue;
            }
            const auto count_it = direct_store_count.find(stack_slot.get());
            if (count_it == direct_store_count.end() || count_it->second != 1) {
                continue;
            }
            const auto entry_store_it = entry_store.find(stack_slot.get());
            if (entry_store_it == entry_store.end()) {
                continue;
            }
            const CoreIrValue *stored_value = entry_store_it->second->get_value();
            const AArch64ValueLocation *location =
                lookup_value_location(state, stored_value);
            if (location == nullptr ||
                location->kind != AArch64ValueLocationKind::VirtualReg) {
                continue;
            }
            state.promoted_stack_slots.insert(stack_slot.get());
        }
    }

    bool is_promoted_stack_slot(const FunctionState &state,
                                const CoreIrStackSlot *stack_slot) const {
        return stack_slot != nullptr &&
               state.promoted_stack_slots.find(stack_slot) !=
                   state.promoted_stack_slots.end();
    }

    bool split_constant_address(const CoreIrConstant *constant, std::string &symbol_name,
                                long long &offset) const {
        if (const auto *global_address =
                dynamic_cast<const CoreIrConstantGlobalAddress *>(constant);
            global_address != nullptr) {
            symbol_name = global_address->get_global()->get_name();
            offset = 0;
            return true;
        }
        if (const auto *gep_constant =
                dynamic_cast<const CoreIrConstantGetElementPtr *>(constant);
            gep_constant != nullptr) {
            if (!split_constant_address(gep_constant->get_base(), symbol_name, offset)) {
                return false;
            }
            const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
                gep_constant->get_base()->get_type());
            if (base_pointer_type == nullptr) {
                return false;
            }
            const std::optional<long long> gep_offset =
                compute_constant_gep_offset(base_pointer_type->get_pointee_type(),
                                            gep_constant->get_indices());
            if (!gep_offset.has_value()) {
                return false;
            }
            offset += *gep_offset;
            return true;
        }
        return false;
    }

    std::optional<long long>
    compute_constant_gep_offset(const CoreIrType *base_type,
                                const std::vector<const CoreIrConstant *> &indices) const {
        if (base_type == nullptr) {
            return std::nullopt;
        }

        const CoreIrType *current_type = base_type;
        long long offset = 0;
        for (std::size_t index_position = 0; index_position < indices.size();
             ++index_position) {
            const std::optional<long long> maybe_index =
                get_signed_integer_constant(indices[index_position]);
            if (!maybe_index.has_value()) {
                return std::nullopt;
            }

            if (index_position == 0) {
                offset += *maybe_index * static_cast<long long>(get_type_size(current_type));
                continue;
            }

            if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(current_type);
                array_type != nullptr) {
                offset += *maybe_index *
                          static_cast<long long>(get_type_size(array_type->get_element_type()));
                current_type = array_type->get_element_type();
                continue;
            }

            if (const auto *struct_type =
                    dynamic_cast<const CoreIrStructType *>(current_type);
                struct_type != nullptr) {
                if (*maybe_index < 0 ||
                    static_cast<std::size_t>(*maybe_index) >=
                        struct_type->get_element_types().size()) {
                    return std::nullopt;
                }
                offset += static_cast<long long>(
                    get_struct_member_offset(struct_type,
                                             static_cast<std::size_t>(*maybe_index)));
                current_type =
                    struct_type->get_element_types()[static_cast<std::size_t>(*maybe_index)];
                continue;
            }

            if (const auto *pointer_type =
                    dynamic_cast<const CoreIrPointerType *>(current_type);
                pointer_type != nullptr) {
                offset += *maybe_index * static_cast<long long>(
                                             get_type_size(pointer_type->get_pointee_type()));
                current_type = pointer_type->get_pointee_type();
                continue;
            }

            return std::nullopt;
        }

        return offset;
    }

    bool append_global_constant_lines(AArch64MachineModule &machine_module,
                                      const CoreIrConstant *constant,
                                      const CoreIrType *type) {
        if (constant == nullptr) {
            return false;
        }

        if (dynamic_cast<const CoreIrConstantZeroInitializer *>(constant) != nullptr) {
            machine_module.append_global_line(
                "  .zero " + std::to_string(get_type_size(type)));
            return true;
        }
        if (const auto *int_constant =
                dynamic_cast<const CoreIrConstantInt *>(constant);
            int_constant != nullptr) {
            machine_module.append_global_line(
                "  " + scalar_directive(type) + " " +
                std::to_string(int_constant->get_value()));
            return true;
        }
        if (dynamic_cast<const CoreIrConstantNull *>(constant) != nullptr) {
            machine_module.append_global_line(
                "  " + scalar_directive(type) + " 0");
            return true;
        }
        if (const auto *byte_string =
                dynamic_cast<const CoreIrConstantByteString *>(constant);
            byte_string != nullptr) {
            std::ostringstream bytes_line;
            bytes_line << "  .byte ";
            for (std::size_t index = 0; index < byte_string->get_bytes().size();
                 ++index) {
                if (index > 0) {
                    bytes_line << ", ";
                }
                bytes_line << static_cast<unsigned>(byte_string->get_bytes()[index]);
            }
            machine_module.append_global_line(bytes_line.str());
            return true;
        }
        if (const auto *aggregate =
                dynamic_cast<const CoreIrConstantAggregate *>(constant);
            aggregate != nullptr) {
            const auto *struct_type = dynamic_cast<const CoreIrStructType *>(type);
            const auto *array_type = dynamic_cast<const CoreIrArrayType *>(type);
            for (std::size_t index = 0; index < aggregate->get_elements().size(); ++index) {
                const CoreIrType *element_type = nullptr;
                if (struct_type != nullptr &&
                    index < struct_type->get_element_types().size()) {
                    element_type = struct_type->get_element_types()[index];
                } else if (array_type != nullptr) {
                    element_type = array_type->get_element_type();
                }
                if (element_type == nullptr ||
                    !append_global_constant_lines(machine_module,
                                                 aggregate->get_elements()[index],
                                                 element_type)) {
                    return false;
                }
            }
            return true;
        }
        std::string symbol_name;
        long long offset = 0;
        if (split_constant_address(constant, symbol_name, offset)) {
            std::string line = "  " + scalar_directive(type) + " " + symbol_name;
            if (offset > 0) {
                line += " + " + std::to_string(offset);
            } else if (offset < 0) {
                line += " - " + std::to_string(-offset);
            }
            machine_module.append_global_line(line);
            return true;
        }

        return false;
    }

    bool append_globals(AArch64MachineModule &machine_module) {
        for (const auto &global : module_.get_globals()) {
            if (!append_global(machine_module, *global)) {
                return false;
            }
        }
        return true;
    }

    bool append_global(AArch64MachineModule &machine_module,
                       const CoreIrGlobal &global) {
        if (global.get_initializer() == nullptr) {
            add_backend_error(
                diagnostic_engine_,
                "AArch64 native backend requires an initializer for global '" +
                    global.get_name() + "'");
            return false;
        }
        if (is_byte_string_global(global)) {
            machine_module.append_global_line(".section .rodata");
            machine_module.append_global_line(
                ".p2align 0");
            if (!global.get_is_internal_linkage()) {
                machine_module.append_global_line(".globl " + global.get_name());
            }
            machine_module.append_global_line(global.get_name() + ":");
            const auto *byte_string =
                static_cast<const CoreIrConstantByteString *>(global.get_initializer());
            std::ostringstream bytes_line;
            bytes_line << "  .byte ";
            for (std::size_t index = 0; index < byte_string->get_bytes().size();
                 ++index) {
                if (index > 0) {
                    bytes_line << ", ";
                }
                bytes_line << static_cast<unsigned>(byte_string->get_bytes()[index]);
            }
            machine_module.append_global_line(bytes_line.str());
            machine_module.append_global_line("");
            return true;
        }

        if (!is_supported_object_type(global.get_type())) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported global type in AArch64 native backend for '" +
                    global.get_name() + "'");
            return false;
        }

        machine_module.append_global_line(global.get_is_constant() ? ".section .rodata"
                                                                   : ".data");
        machine_module.append_global_line(
            ".p2align " + std::to_string(
                get_type_alignment(global.get_type()) >= 8 ? 3 :
                get_type_alignment(global.get_type()) >= 4 ? 2 :
                get_type_alignment(global.get_type()) >= 2 ? 1 : 0));
        if (!global.get_is_internal_linkage()) {
            machine_module.append_global_line(".globl " + global.get_name());
        }
        machine_module.append_global_line(global.get_name() + ":");
        if (!append_global_constant_lines(machine_module, global.get_initializer(),
                                          global.get_type())) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported global initializer in AArch64 native backend for '" +
                    global.get_name() + "'");
            return false;
        }
        machine_module.append_global_line("");
        return true;
    }

    bool append_functions(AArch64MachineModule &machine_module) {
        for (const auto &function : module_.get_functions()) {
            if (!append_function(machine_module, *function)) {
                return false;
            }
        }
        return true;
    }

    bool append_function(AArch64MachineModule &machine_module,
                         const CoreIrFunction &function) {
        if (function.get_basic_blocks().empty()) {
            return true;
        }
        if (function.get_is_variadic()) {
            add_backend_error(diagnostic_engine_,
                              "variadic functions are not supported by the "
                              "AArch64 native backend: " +
                                  function.get_name());
            return false;
        }
        if (!is_supported_value_type(function.get_function_type()->get_return_type())) {
            add_backend_error(
                diagnostic_engine_,
                "unsupported return type in AArch64 native backend for function '" +
                    function.get_name() + "'");
            return false;
        }
        for (const auto &parameter : function.get_parameters()) {
            if (!is_supported_value_type(parameter->get_type())) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported parameter type in AArch64 native "
                                  "backend for function '" +
                                      function.get_name() + "'");
                return false;
            }
        }
        for (const auto &stack_slot : function.get_stack_slots()) {
            if (!is_supported_object_type(stack_slot->get_allocated_type())) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported stack slot type in AArch64 native "
                                  "backend for function '" +
                                      function.get_name() + "'");
                return false;
            }
        }

        const std::string function_name = function.get_name();
        AArch64MachineFunction &machine_function = machine_module.append_function(
            function_name, !function.get_is_internal_linkage(),
            ".L" + sanitize_label_fragment(function_name) + "_epilogue");
        FunctionState state;
        state.machine_function = &machine_function;

        std::size_t current_offset = 0;
        if (function.get_parameters().size() > 8) {
            for (std::size_t index = 8; index < function.get_parameters().size();
                 ++index) {
                state.incoming_stack_argument_offsets.emplace(
                    function.get_parameters()[index].get(),
                    16 + ((index - 8) * 8));
            }
        }
        for (const auto &stack_slot : function.get_stack_slots()) {
            current_offset = align_to(current_offset,
                                      get_type_alignment(stack_slot->get_allocated_type()));
            current_offset += get_type_size(stack_slot->get_allocated_type());
            machine_function.get_frame_info().set_stack_slot_offset(stack_slot.get(),
                                                                    current_offset);
        }

        const std::size_t frame_size = align_to(current_offset, 16);
        machine_function.get_frame_info().set_local_size(current_offset);
        machine_function.get_frame_info().set_frame_size(frame_size);

        if (!seed_function_value_locations(function, state)) {
            return false;
        }
        seed_promoted_stack_slots(function, state);

        block_labels_.clear();
        for (const auto &basic_block : function.get_basic_blocks()) {
            block_labels_.emplace(
                basic_block.get(),
                ".L" + sanitize_label_fragment(function_name) + "_" +
                    sanitize_label_fragment(basic_block->get_name()));
        }

        if (!emit_function(machine_function, function, state)) {
            return false;
        }
        if (!allocate_virtual_registers(machine_function, diagnostic_engine_)) {
            return false;
        }
        return true;
    }

    bool emit_function(AArch64MachineFunction &machine_function,
                       const CoreIrFunction &function, FunctionState &state) {
        AArch64MachineBlock &prologue_block =
            machine_function.append_block(function.get_name());
        prologue_block.append_instruction("stp x29, x30, [sp, #-16]!");
        prologue_block.append_instruction("mov x29, sp");
        if (machine_function.get_frame_info().get_frame_size() > 0) {
            prologue_block.append_instruction(
                "sub sp, sp, #" +
                std::to_string(machine_function.get_frame_info().get_frame_size()));
        }

        for (std::size_t index = 0; index < function.get_parameters().size(); ++index) {
            const CoreIrParameter *parameter = function.get_parameters()[index].get();
            AArch64VirtualReg parameter_reg;
            if (!require_canonical_vreg(state, parameter, parameter_reg)) {
                return false;
            }
            if (index < 8) {
                prologue_block.append_instruction(
                    "mov " + def_vreg(parameter_reg) + ", " +
                    general_register_name(static_cast<unsigned>(index),
                                          uses_64bit_register(parameter->get_type())));
                apply_truncate_to_virtual_reg(prologue_block, parameter_reg,
                                              parameter->get_type());
                continue;
            }
            append_load_from_incoming_stack_arg(
                prologue_block, parameter->get_type(), parameter_reg,
                state.incoming_stack_argument_offsets.at(parameter),
                *state.machine_function);
            apply_truncate_to_virtual_reg(prologue_block, parameter_reg,
                                          parameter->get_type());
        }

        for (const auto &basic_block : function.get_basic_blocks()) {
            AArch64MachineBlock &machine_block =
                machine_function.append_block(block_labels_.at(basic_block.get()));
            for (const auto &instruction : basic_block->get_instructions()) {
                if (!emit_instruction(machine_function, machine_block, *instruction,
                                      state)) {
                    return false;
                }
            }
        }

        AArch64MachineBlock &epilogue_block =
            machine_function.append_block(machine_function.get_epilogue_label());
        if (machine_function.get_frame_info().get_frame_size() > 0) {
            epilogue_block.append_instruction(
                "add sp, sp, #" +
                std::to_string(machine_function.get_frame_info().get_frame_size()));
        }
        epilogue_block.append_instruction("ldp x29, x30, [sp], #16");
        epilogue_block.append_instruction("ret");
        return true;
    }

    bool emit_instruction(AArch64MachineFunction &machine_function,
                          AArch64MachineBlock &machine_block,
                          const CoreIrInstruction &instruction,
                          FunctionState &state) {
        switch (instruction.get_opcode()) {
        case CoreIrOpcode::Phi:
            add_backend_error(diagnostic_engine_,
                              "phi instructions are not supported by the "
                              "AArch64 native backend");
            return false;
        case CoreIrOpcode::Load:
            return emit_load(machine_block,
                             static_cast<const CoreIrLoadInst &>(instruction),
                             state);
        case CoreIrOpcode::Store:
            return emit_store(machine_block,
                              static_cast<const CoreIrStoreInst &>(instruction),
                              state);
        case CoreIrOpcode::Binary:
            return emit_binary(machine_block,
                               static_cast<const CoreIrBinaryInst &>(instruction),
                               state);
        case CoreIrOpcode::Unary:
            return emit_unary(machine_block,
                              static_cast<const CoreIrUnaryInst &>(instruction),
                              state);
        case CoreIrOpcode::Compare:
            return emit_compare(machine_block,
                                static_cast<const CoreIrCompareInst &>(instruction),
                                state);
        case CoreIrOpcode::Cast:
            return emit_cast(machine_block,
                             static_cast<const CoreIrCastInst &>(instruction), state);
        case CoreIrOpcode::Call:
            return emit_call(machine_block,
                             static_cast<const CoreIrCallInst &>(instruction), state);
        case CoreIrOpcode::Jump:
            machine_block.append_instruction(
                "b " +
                block_labels_.at(
                    static_cast<const CoreIrJumpInst &>(instruction).get_target_block()));
            return true;
        case CoreIrOpcode::CondJump:
            return emit_cond_jump(machine_block,
                                  static_cast<const CoreIrCondJumpInst &>(instruction),
                                  state);
        case CoreIrOpcode::Return:
            return emit_return(machine_function, machine_block,
                               static_cast<const CoreIrReturnInst &>(instruction),
                               state);
        case CoreIrOpcode::AddressOfStackSlot:
            return emit_address_of_stack_slot(
                machine_block,
                static_cast<const CoreIrAddressOfStackSlotInst &>(instruction), state);
        case CoreIrOpcode::AddressOfGlobal:
            return emit_address_of_global(
                machine_block,
                static_cast<const CoreIrAddressOfGlobalInst &>(instruction), state);
        case CoreIrOpcode::AddressOfFunction:
            return emit_address_of_function(
                machine_block,
                static_cast<const CoreIrAddressOfFunctionInst &>(instruction), state);
        case CoreIrOpcode::GetElementPtr:
            return emit_getelementptr(machine_block,
                                      static_cast<const CoreIrGetElementPtrInst &>(instruction),
                                      state);
        }
        return false;
    }

    bool materialize_value(AArch64MachineBlock &machine_block,
                           const CoreIrValue *value,
                           const AArch64VirtualReg &target_reg,
                           const FunctionState &state) {
        if (const AArch64ValueLocation *location =
                lookup_value_location(state, value);
            location != nullptr &&
            location->kind == AArch64ValueLocationKind::VirtualReg) {
            if (location->virtual_reg.get_id() != target_reg.get_id()) {
                machine_block.append_instruction("mov " + def_vreg(target_reg) + ", " +
                                                 use_vreg(location->virtual_reg));
            }
            return true;
        }

        return materialize_noncanonical_value(machine_block, value, target_reg, state);
    }

    bool materialize_noncanonical_value(AArch64MachineBlock &machine_block,
                                        const CoreIrValue *value,
                                        const AArch64VirtualReg &target_reg,
                                        const FunctionState &state) {
        if (value == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "encountered null Core IR value during AArch64 lowering");
            return false;
        }

        if (const auto *int_constant =
                dynamic_cast<const CoreIrConstantInt *>(value);
            int_constant != nullptr) {
            return materialize_integer_constant(machine_block, value->get_type(),
                                                int_constant->get_value(), target_reg);
        }
        if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr ||
            dynamic_cast<const CoreIrConstantZeroInitializer *>(value) != nullptr) {
            machine_block.append_instruction("mov " + def_vreg(target_reg) + ", " +
                                             zero_register_name(target_reg.get_use_64bit()));
            return true;
        }
        if (const auto *global_address =
                dynamic_cast<const CoreIrConstantGlobalAddress *>(value);
            global_address != nullptr) {
            return materialize_global_address(machine_block,
                                              global_address->get_global()->get_name(),
                                              target_reg);
        }
        if (const auto *gep_constant =
                dynamic_cast<const CoreIrConstantGetElementPtr *>(value);
            gep_constant != nullptr) {
            const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
                gep_constant->get_base()->get_type());
            if (base_pointer_type == nullptr) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported constant gep base in AArch64 native backend");
                return false;
            }
            return materialize_gep_value(
                machine_block, gep_constant->get_base(),
                base_pointer_type->get_pointee_type(),
                gep_constant->get_indices().size(),
                [&gep_constant](std::size_t index) -> CoreIrValue * {
                    return const_cast<CoreIrConstant *>(gep_constant->get_indices()[index]);
                },
                target_reg, state);
        }
        if (const auto *address_of_stack_slot =
                dynamic_cast<const CoreIrAddressOfStackSlotInst *>(value);
            address_of_stack_slot != nullptr) {
            const std::size_t offset =
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    address_of_stack_slot->get_stack_slot());
            append_frame_address(machine_block, target_reg, offset,
                                 *state.machine_function);
            return true;
        }
        if (const auto *address_of_global =
                dynamic_cast<const CoreIrAddressOfGlobalInst *>(value);
            address_of_global != nullptr) {
            return materialize_global_address(
                machine_block, address_of_global->get_global()->get_name(),
                target_reg);
        }
        if (const auto *address_of_function =
                dynamic_cast<const CoreIrAddressOfFunctionInst *>(value);
            address_of_function != nullptr) {
            return materialize_global_address(
                machine_block, address_of_function->get_function()->get_name(),
                target_reg);
        }
        if (const auto *gep_instruction =
                dynamic_cast<const CoreIrGetElementPtrInst *>(value);
            gep_instruction != nullptr) {
            const auto *base_pointer_type = dynamic_cast<const CoreIrPointerType *>(
                gep_instruction->get_base()->get_type());
            if (base_pointer_type == nullptr) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported gep base in AArch64 native backend");
                return false;
            }
            return materialize_gep_value(
                machine_block, gep_instruction->get_base(),
                base_pointer_type->get_pointee_type(),
                gep_instruction->get_index_count(),
                [&gep_instruction](std::size_t index) -> CoreIrValue * {
                    return gep_instruction->get_index(index);
                },
                target_reg, state);
        }

        add_backend_error(diagnostic_engine_,
                          "unsupported Core IR value in AArch64 native backend");
        return false;
    }

    bool ensure_value_in_vreg(AArch64MachineBlock &machine_block,
                              const CoreIrValue *value,
                              const FunctionState &state,
                              AArch64VirtualReg &out) {
        if (value == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "encountered null Core IR value during AArch64 lowering");
            return false;
        }
        if (const AArch64ValueLocation *location =
                lookup_value_location(state, value);
            location != nullptr &&
            location->kind == AArch64ValueLocationKind::VirtualReg) {
            out = location->virtual_reg;
            return true;
        }
        out = create_virtual_reg(*state.machine_function, value->get_type());
        return materialize_noncanonical_value(machine_block, value, out, state);
    }

    const CoreIrType *create_fake_pointer_type() const {
        static CoreIrVoidType void_type;
        static CoreIrPointerType pointer_type(&void_type);
        return &pointer_type;
    }

    std::string load_mnemonic_for_type(const CoreIrType *type) const {
        if (is_pointer_type(type) || get_storage_size(type) == 8) {
            return "ldr";
        }
        if (get_storage_size(type) == 2) {
            return "ldrh";
        }
        if (get_storage_size(type) == 1) {
            return "ldrb";
        }
        return "ldr";
    }

    std::string store_mnemonic_for_type(const CoreIrType *type) const {
        if (is_pointer_type(type) || get_storage_size(type) == 8) {
            return "str";
        }
        if (get_storage_size(type) == 2) {
            return "strh";
        }
        if (get_storage_size(type) == 1) {
            return "strb";
        }
        return "str";
    }

    bool append_memory_store(AArch64MachineBlock &machine_block, const CoreIrType *type,
                             const std::string &source_reg,
                             const AArch64VirtualReg &address_reg,
                             std::size_t offset,
                             AArch64MachineFunction &function) {
        if (offset <= 4095) {
            machine_block.append_instruction(store_mnemonic_for_type(type) + " " +
                                             source_reg + ", [" +
                                             use_vreg(address_reg) + ", #" +
                                             std::to_string(offset) + "]");
            return true;
        }
        const AArch64VirtualReg offset_address_reg =
            create_pointer_virtual_reg(function);
        machine_block.append_instruction("mov " + def_vreg(offset_address_reg) + ", " +
                                         use_vreg(address_reg));
        if (!add_constant_offset(machine_block, offset_address_reg,
                                 static_cast<long long>(offset), function)) {
            return false;
        }
        machine_block.append_instruction(store_mnemonic_for_type(type) + " " +
                                         source_reg + ", [" +
                                         use_vreg(offset_address_reg) + "]");
        return true;
    }

    bool emit_zero_fill(AArch64MachineBlock &machine_block,
                        const AArch64VirtualReg &address_reg,
                        const CoreIrType *type,
                        AArch64MachineFunction &function) {
        std::size_t remaining = get_type_size(type);
        std::size_t offset = 0;
        while (remaining >= 8) {
            if (!append_memory_store(machine_block, create_fake_pointer_type(), "xzr",
                                     address_reg, offset, function)) {
                return false;
            }
            offset += 8;
            remaining -= 8;
        }
        if (remaining >= 4) {
            static CoreIrIntegerType i32_type(32);
            if (!append_memory_store(machine_block, &i32_type, "wzr", address_reg,
                                     offset, function)) {
                return false;
            }
            offset += 4;
            remaining -= 4;
        }
        if (remaining >= 2) {
            static CoreIrIntegerType i16_type(16);
            if (!append_memory_store(machine_block, &i16_type, "wzr", address_reg,
                                     offset, function)) {
                return false;
            }
            offset += 2;
            remaining -= 2;
        }
        if (remaining >= 1) {
            static CoreIrIntegerType i8_type(8);
            if (!append_memory_store(machine_block, &i8_type, "wzr", address_reg,
                                     offset, function)) {
                return false;
            }
        }
        return true;
    }

    void append_load_from_frame(AArch64MachineBlock &machine_block,
                                const CoreIrType *type,
                                const AArch64VirtualReg &target_reg,
                                std::size_t offset,
                                AArch64MachineFunction &function) {
        std::string mnemonic = "ldur";
        if (get_storage_size(type) == 2) {
            mnemonic = "ldurh";
        } else if (get_storage_size(type) == 1) {
            mnemonic = "ldurb";
        }
        if (offset <= 255) {
            machine_block.append_instruction(
                mnemonic + " " + def_vreg(target_reg) + ", [x29, #-" +
                std::to_string(offset) + "]");
            return;
        }
        const AArch64VirtualReg address_reg = create_pointer_virtual_reg(function);
        append_frame_address(machine_block, address_reg, offset, function);
        machine_block.append_instruction(load_mnemonic_for_type(type) + " " +
                                         def_vreg(target_reg) + ", [" +
                                         use_vreg(address_reg) + "]");
    }

    void append_store_to_frame(AArch64MachineBlock &machine_block,
                               const CoreIrType *type,
                               const AArch64VirtualReg &source_reg,
                               std::size_t offset,
                               AArch64MachineFunction &function) {
        std::string mnemonic = "stur";
        if (get_storage_size(type) == 2) {
            mnemonic = "sturh";
        } else if (get_storage_size(type) == 1) {
            mnemonic = "sturb";
        }
        if (offset <= 255) {
            machine_block.append_instruction(
                mnemonic + " " + use_vreg(source_reg) + ", [x29, #-" +
                std::to_string(offset) + "]");
            return;
        }
        const AArch64VirtualReg address_reg = create_pointer_virtual_reg(function);
        append_frame_address(machine_block, address_reg, offset, function);
        machine_block.append_instruction(store_mnemonic_for_type(type) + " " +
                                         use_vreg(source_reg) + ", [" +
                                         use_vreg(address_reg) + "]");
    }

    void append_load_from_incoming_stack_arg(AArch64MachineBlock &machine_block,
                                             const CoreIrType *type,
                                             const AArch64VirtualReg &target_reg,
                                             std::size_t offset,
                                             AArch64MachineFunction &function) {
        if (offset <= 4095) {
            machine_block.append_instruction(load_mnemonic_for_type(type) + " " +
                                             def_vreg(target_reg) + ", [x29, #" +
                                             std::to_string(offset) + "]");
            return;
        }
        const AArch64VirtualReg address_reg = create_pointer_virtual_reg(function);
        machine_block.append_instruction("mov " + def_vreg(address_reg) + ", x29");
        add_constant_offset(machine_block, address_reg,
                            static_cast<long long>(offset), function);
        machine_block.append_instruction(load_mnemonic_for_type(type) + " " +
                                         def_vreg(target_reg) + ", [" +
                                         use_vreg(address_reg) + "]");
    }

    void apply_truncate_to_virtual_reg(AArch64MachineBlock &machine_block,
                                       const AArch64VirtualReg &reg,
                                       const CoreIrType *type) {
        const auto *integer_type = as_integer_type(type);
        if (integer_type == nullptr) {
            return;
        }
        switch (integer_type->get_bit_width()) {
        case 1:
            machine_block.append_instruction("and " + def_vreg_as(reg, false) + ", " +
                                             use_vreg_as(reg, false) + ", #1");
            break;
        case 8:
            machine_block.append_instruction("uxtb " + def_vreg_as(reg, false) + ", " +
                                             use_vreg_as(reg, false));
            break;
        case 16:
            machine_block.append_instruction("uxth " + def_vreg_as(reg, false) + ", " +
                                             use_vreg_as(reg, false));
            break;
        default:
            break;
        }
    }

    void apply_zero_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) {
        const auto *source_integer = as_integer_type(source_type);
        if (source_integer == nullptr) {
            return;
        }
        switch (source_integer->get_bit_width()) {
        case 1:
            machine_block.append_instruction("and " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(dst_reg, false) + ", #1");
            break;
        case 8:
            machine_block.append_instruction("uxtb " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(dst_reg, false));
            break;
        case 16:
            machine_block.append_instruction("uxth " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(dst_reg, false));
            break;
        case 32:
            break;
        default:
            break;
        }
    }

    void apply_sign_extend_to_virtual_reg(AArch64MachineBlock &machine_block,
                                          const AArch64VirtualReg &dst_reg,
                                          const CoreIrType *source_type,
                                          const CoreIrType *target_type) {
        const auto *source_integer = as_integer_type(source_type);
        const bool target_uses_64bit = uses_64bit_register(target_type);
        if (source_integer == nullptr) {
            return;
        }
        switch (source_integer->get_bit_width()) {
        case 1:
            machine_block.append_instruction("and " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(dst_reg, false) + ", #1");
            machine_block.append_instruction(
                "neg " + def_vreg_as(dst_reg, target_uses_64bit) + ", " +
                use_vreg_as(dst_reg, target_uses_64bit));
            break;
        case 8:
            machine_block.append_instruction(
                std::string("sxtb ") + def_vreg_as(dst_reg, target_uses_64bit) + ", " +
                use_vreg_as(dst_reg, false));
            break;
        case 16:
            machine_block.append_instruction(
                std::string("sxth ") + def_vreg_as(dst_reg, target_uses_64bit) + ", " +
                use_vreg_as(dst_reg, false));
            break;
        case 32:
            if (target_uses_64bit) {
                machine_block.append_instruction("sxtw " + def_vreg_as(dst_reg, true) +
                                                 ", " + use_vreg_as(dst_reg, false));
            }
            break;
        default:
            break;
        }
    }

    bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                      const CoreIrType *type, std::uint64_t value,
                                      const AArch64VirtualReg &target_reg) {
        const unsigned pieces = target_reg.get_use_64bit() ? 4U : 2U;
        bool emitted = false;
        for (unsigned piece = 0; piece < pieces; ++piece) {
            const std::uint16_t imm16 =
                static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
            if (!emitted) {
                machine_block.append_instruction(
                    "movz " + def_vreg(target_reg) + ", #" +
                    std::to_string(imm16) + ", lsl #" +
                    std::to_string(piece * 16U));
                emitted = true;
                continue;
            }
            if (imm16 == 0) {
                continue;
            }
            machine_block.append_instruction(
                "movk " + use_vreg(target_reg) + ", #" +
                std::to_string(imm16) + ", lsl #" +
                std::to_string(piece * 16U));
        }
        if (!emitted) {
            machine_block.append_instruction("mov " + def_vreg(target_reg) + ", " +
                                             zero_register_name(target_reg.get_use_64bit()));
        }
        apply_truncate_to_virtual_reg(machine_block, target_reg, type);
        return true;
    }

    bool materialize_global_address(AArch64MachineBlock &machine_block,
                                    const std::string &symbol_name,
                                    const AArch64VirtualReg &target_reg) {
        machine_block.append_instruction("adrp " + def_vreg(target_reg) + ", " +
                                         symbol_name);
        machine_block.append_instruction("add " + def_vreg(target_reg) + ", " +
                                         use_vreg(target_reg) + ", :lo12:" +
                                         symbol_name);
        return true;
    }

    void append_frame_address(AArch64MachineBlock &machine_block,
                              const AArch64VirtualReg &target_reg,
                              std::size_t offset,
                              AArch64MachineFunction &function) {
        if (offset <= 4095) {
            machine_block.append_instruction("sub " + def_vreg(target_reg) +
                                             ", x29, #" + std::to_string(offset));
            return;
        }
        const AArch64VirtualReg offset_reg = create_pointer_virtual_reg(function);
        materialize_integer_constant(machine_block, create_fake_pointer_type(),
                                     static_cast<std::uint64_t>(offset), offset_reg);
        machine_block.append_instruction("sub " + def_vreg(target_reg) + ", x29, " +
                                         use_vreg(offset_reg));
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block,
                             const AArch64VirtualReg &base_reg, long long offset,
                             AArch64MachineFunction &function) {
        if (offset == 0) {
            return true;
        }
        const long long absolute_offset = offset >= 0 ? offset : -offset;
        if (absolute_offset <= 4095) {
            machine_block.append_instruction(
                std::string(offset >= 0 ? "add " : "sub ") + def_vreg(base_reg) +
                ", " + use_vreg(base_reg) + ", #" +
                std::to_string(absolute_offset));
            return true;
        }
        const AArch64VirtualReg offset_reg = create_pointer_virtual_reg(function);
        if (!materialize_integer_constant(machine_block, create_fake_pointer_type(),
                                          static_cast<std::uint64_t>(absolute_offset),
                                          offset_reg)) {
            return false;
        }
        machine_block.append_instruction(
            std::string(offset >= 0 ? "add " : "sub ") + def_vreg(base_reg) +
            ", " + use_vreg(base_reg) + ", " + use_vreg(offset_reg));
        return true;
    }

    bool add_scaled_index(AArch64MachineBlock &machine_block,
                          const AArch64VirtualReg &base_reg,
                          const CoreIrValue *index_value,
                          std::size_t scale,
                          const FunctionState &state) {
        const AArch64VirtualReg index_reg =
            create_virtual_reg(*state.machine_function, index_value->get_type());
        if (!materialize_value(machine_block, index_value, index_reg, state)) {
            return false;
        }
        const auto *index_integer_type = as_integer_type(index_value->get_type());
        if (index_integer_type != nullptr && index_integer_type->get_bit_width() <= 32) {
            machine_block.append_instruction("sxtw " + def_vreg(index_reg) + ", " +
                                             use_vreg(index_reg));
        }

        if (scale == 1) {
            machine_block.append_instruction("add " + def_vreg(base_reg) + ", " +
                                             use_vreg(base_reg) + ", " +
                                             use_vreg(index_reg));
            return true;
        }

        const bool power_of_two = scale != 0 && (scale & (scale - 1)) == 0;
        if (power_of_two) {
            std::size_t shift = 0;
            std::size_t remaining = scale;
            while (remaining > 1) {
                remaining >>= 1U;
                ++shift;
            }
            machine_block.append_instruction("add " + def_vreg(base_reg) + ", " +
                                             use_vreg(base_reg) + ", " +
                                             use_vreg(index_reg) + ", lsl #" +
                                             std::to_string(shift));
            return true;
        }

        const AArch64VirtualReg scale_reg =
            create_pointer_virtual_reg(*state.machine_function);
        if (!materialize_integer_constant(machine_block, create_fake_pointer_type(),
                                          static_cast<std::uint64_t>(scale),
                                          scale_reg)) {
            return false;
        }
        machine_block.append_instruction("mul " + def_vreg(index_reg) + ", " +
                                         use_vreg(index_reg) + ", " +
                                         use_vreg(scale_reg));
        machine_block.append_instruction("add " + def_vreg(base_reg) + ", " +
                                         use_vreg(base_reg) + ", " +
                                         use_vreg(index_reg));
        return true;
    }

    bool materialize_gep_value(
        AArch64MachineBlock &machine_block, const CoreIrValue *base_value,
        const CoreIrType *base_type, std::size_t index_count,
        const std::function<CoreIrValue *(std::size_t)> &get_index_value,
        const AArch64VirtualReg &target_reg, const FunctionState &state) {
        AArch64VirtualReg base_reg;
        if (!ensure_value_in_vreg(machine_block, base_value, state, base_reg)) {
            return false;
        }
        if (base_reg.get_id() != target_reg.get_id()) {
            machine_block.append_instruction("mov " + def_vreg(target_reg) + ", " +
                                             use_vreg(base_reg));
        }

        const CoreIrType *current_type = base_type;
        for (std::size_t index_position = 0; index_position < index_count;
             ++index_position) {
            CoreIrValue *index_value = get_index_value(index_position);
            if (index_value == nullptr) {
                add_backend_error(diagnostic_engine_,
                                  "encountered null gep index in AArch64 native backend");
                return false;
            }

            if (const auto *index_constant =
                    dynamic_cast<const CoreIrConstantInt *>(index_value);
                index_constant != nullptr) {
                const std::optional<long long> index =
                    get_signed_integer_constant(index_constant);
                if (!index.has_value()) {
                    return false;
                }
                if (index_position == 0) {
                    if (!add_constant_offset(machine_block, target_reg,
                                             *index * static_cast<long long>(
                                                          get_type_size(current_type)),
                                             *state.machine_function)) {
                        return false;
                    }
                    continue;
                }
                if (const auto *array_type =
                        dynamic_cast<const CoreIrArrayType *>(current_type);
                    array_type != nullptr) {
                    if (!add_constant_offset(
                            machine_block, target_reg,
                            *index * static_cast<long long>(
                                         get_type_size(array_type->get_element_type())),
                            *state.machine_function)) {
                        return false;
                    }
                    current_type = array_type->get_element_type();
                    continue;
                }
                if (const auto *struct_type =
                        dynamic_cast<const CoreIrStructType *>(current_type);
                    struct_type != nullptr) {
                    if (*index < 0 ||
                        static_cast<std::size_t>(*index) >=
                            struct_type->get_element_types().size()) {
                        add_backend_error(diagnostic_engine_,
                                          "unsupported gep struct index in AArch64 native backend");
                        return false;
                    }
                    if (!add_constant_offset(
                            machine_block, target_reg,
                            static_cast<long long>(
                                get_struct_member_offset(
                                    struct_type, static_cast<std::size_t>(*index))),
                            *state.machine_function)) {
                        return false;
                    }
                    current_type =
                        struct_type->get_element_types()[static_cast<std::size_t>(*index)];
                    continue;
                }
                if (const auto *pointer_type =
                        dynamic_cast<const CoreIrPointerType *>(current_type);
                    pointer_type != nullptr) {
                    if (!add_constant_offset(
                            machine_block, target_reg,
                            *index * static_cast<long long>(
                                         get_type_size(pointer_type->get_pointee_type())),
                            *state.machine_function)) {
                        return false;
                    }
                    current_type = pointer_type->get_pointee_type();
                    continue;
                }
                add_backend_error(diagnostic_engine_,
                                  "unsupported constant gep shape in AArch64 native backend");
                return false;
            }

            if (index_position == 0) {
                if (!add_scaled_index(machine_block, target_reg, index_value,
                                      get_type_size(current_type), state)) {
                    return false;
                }
                continue;
            }
            if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(current_type);
                array_type != nullptr) {
                if (!add_scaled_index(machine_block, target_reg, index_value,
                                      get_type_size(array_type->get_element_type()),
                                      state)) {
                    return false;
                }
                current_type = array_type->get_element_type();
                continue;
            }
            if (const auto *pointer_type =
                    dynamic_cast<const CoreIrPointerType *>(current_type);
                pointer_type != nullptr) {
                if (!add_scaled_index(machine_block, target_reg, index_value,
                                      get_type_size(pointer_type->get_pointee_type()),
                                      state)) {
                    return false;
                }
                current_type = pointer_type->get_pointee_type();
                continue;
            }
            add_backend_error(diagnostic_engine_,
                              "unsupported non-constant aggregate gep in AArch64 native backend");
            return false;
        }

        return true;
    }

    bool emit_address_of_stack_slot(AArch64MachineBlock &machine_block,
                                    const CoreIrAddressOfStackSlotInst &address_of_stack_slot,
                                    const FunctionState &state) {
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &address_of_stack_slot, target_reg)) {
            return false;
        }
        append_frame_address(
            machine_block, target_reg,
            state.machine_function->get_frame_info().get_stack_slot_offset(
                address_of_stack_slot.get_stack_slot()),
            *state.machine_function);
        return true;
    }

    bool emit_address_of_global(AArch64MachineBlock &machine_block,
                                const CoreIrAddressOfGlobalInst &address_of_global,
                                const FunctionState &state) {
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &address_of_global, target_reg)) {
            return false;
        }
        return materialize_global_address(machine_block,
                                          address_of_global.get_global()->get_name(),
                                          target_reg);
    }

    bool emit_address_of_function(AArch64MachineBlock &machine_block,
                                  const CoreIrAddressOfFunctionInst &address_of_function,
                                  const FunctionState &state) {
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &address_of_function, target_reg)) {
            return false;
        }
        return materialize_global_address(machine_block,
                                          address_of_function.get_function()->get_name(),
                                          target_reg);
    }

    bool emit_getelementptr(AArch64MachineBlock &machine_block,
                            const CoreIrGetElementPtrInst &gep,
                            const FunctionState &state) {
        AArch64VirtualReg target_reg;
        if (!require_canonical_vreg(state, &gep, target_reg)) {
            return false;
        }
        const auto *base_pointer_type =
            dynamic_cast<const CoreIrPointerType *>(gep.get_base()->get_type());
        if (base_pointer_type == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "unsupported gep base in AArch64 native backend");
            return false;
        }
        return materialize_gep_value(
            machine_block, gep.get_base(), base_pointer_type->get_pointee_type(),
            gep.get_index_count(),
            [&gep](std::size_t index) -> CoreIrValue * { return gep.get_index(index); },
            target_reg, state);
    }

    bool emit_load(AArch64MachineBlock &machine_block, const CoreIrLoadInst &load,
                   const FunctionState &state) {
        AArch64VirtualReg value_reg;
        if (!require_canonical_vreg(state, &load, value_reg)) {
            return false;
        }
        if (load.get_stack_slot() != nullptr) {
            if (is_promoted_stack_slot(state, load.get_stack_slot())) {
                const auto value_it =
                    state.promoted_stack_slot_values.find(load.get_stack_slot());
                if (value_it == state.promoted_stack_slot_values.end()) {
                    add_backend_error(
                        diagnostic_engine_,
                        "promoted stack slot loaded before it has a canonical value");
                    return false;
                }
                if (value_it->second.get_id() != value_reg.get_id()) {
                    machine_block.append_instruction("mov " + def_vreg(value_reg) + ", " +
                                                     use_vreg(value_it->second));
                }
                return true;
            }
            append_load_from_frame(
                machine_block, load.get_type(), value_reg,
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    load.get_stack_slot()),
                *state.machine_function);
        } else {
            AArch64VirtualReg address_reg;
            if (!ensure_value_in_vreg(machine_block, load.get_address(), state,
                                      address_reg)) {
                return false;
            }
            machine_block.append_instruction(load_mnemonic_for_type(load.get_type()) +
                                             " " + def_vreg(value_reg) + ", [" +
                                             use_vreg(address_reg) + "]");
        }
        return true;
    }

    bool emit_store(AArch64MachineBlock &machine_block,
                    const CoreIrStoreInst &store, FunctionState &state) {
        if (store.get_stack_slot() != nullptr &&
            is_promoted_stack_slot(state, store.get_stack_slot())) {
            AArch64VirtualReg value_reg;
            if (!ensure_value_in_vreg(machine_block, store.get_value(), state, value_reg)) {
                return false;
            }
            state.promoted_stack_slot_values[store.get_stack_slot()] = value_reg;
            return true;
        }

        if (const auto *zero_initializer =
                dynamic_cast<const CoreIrConstantZeroInitializer *>(store.get_value());
            zero_initializer != nullptr) {
            AArch64VirtualReg address_reg;
            if (store.get_stack_slot() != nullptr) {
                address_reg = create_pointer_virtual_reg(*state.machine_function);
                append_frame_address(
                    machine_block, address_reg,
                    state.machine_function->get_frame_info().get_stack_slot_offset(
                        store.get_stack_slot()),
                    *state.machine_function);
            } else {
                if (!ensure_value_in_vreg(machine_block, store.get_address(), state,
                                          address_reg)) {
                    return false;
                }
            }
            return emit_zero_fill(machine_block, address_reg,
                                  zero_initializer->get_type(),
                                  *state.machine_function);
        }

        AArch64VirtualReg value_reg;
        if (!ensure_value_in_vreg(machine_block, store.get_value(), state, value_reg)) {
            return false;
        }
        if (store.get_stack_slot() != nullptr) {
            append_store_to_frame(
                machine_block, store.get_value()->get_type(), value_reg,
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    store.get_stack_slot()),
                *state.machine_function);
            return true;
        }
        AArch64VirtualReg address_reg;
        if (!ensure_value_in_vreg(machine_block, store.get_address(), state,
                                  address_reg)) {
            return false;
        }
        machine_block.append_instruction(store_mnemonic_for_type(store.get_value()->get_type()) +
                                         " " + use_vreg(value_reg) + ", [" +
                                         use_vreg(address_reg) + "]");
        return true;
    }

    bool emit_binary(AArch64MachineBlock &machine_block,
                     const CoreIrBinaryInst &binary, const FunctionState &state) {
        AArch64VirtualReg lhs_reg;
        AArch64VirtualReg rhs_reg;
        AArch64VirtualReg dst_reg;
        if (!ensure_value_in_vreg(machine_block, binary.get_lhs(), state, lhs_reg) ||
            !ensure_value_in_vreg(machine_block, binary.get_rhs(), state, rhs_reg) ||
            !require_canonical_vreg(state, &binary, dst_reg)) {
            return false;
        }
        std::string opcode;
        switch (binary.get_binary_opcode()) {
        case CoreIrBinaryOpcode::Add:
            opcode = "add";
            break;
        case CoreIrBinaryOpcode::Sub:
            opcode = "sub";
            break;
        case CoreIrBinaryOpcode::Mul:
            opcode = "mul";
            break;
        case CoreIrBinaryOpcode::SDiv:
            opcode = "sdiv";
            break;
        case CoreIrBinaryOpcode::UDiv:
            opcode = "udiv";
            break;
        case CoreIrBinaryOpcode::And:
            opcode = "and";
            break;
        case CoreIrBinaryOpcode::Or:
            opcode = "orr";
            break;
        case CoreIrBinaryOpcode::Xor:
            opcode = "eor";
            break;
        case CoreIrBinaryOpcode::Shl:
            opcode = "lsl";
            break;
        case CoreIrBinaryOpcode::LShr:
            opcode = "lsr";
            break;
        case CoreIrBinaryOpcode::AShr:
            opcode = "asr";
            break;
        case CoreIrBinaryOpcode::SRem:
            {
            const AArch64VirtualReg quotient_reg =
                create_virtual_reg(*state.machine_function, binary.get_lhs()->get_type());
            const AArch64VirtualReg product_reg =
                create_virtual_reg(*state.machine_function, binary.get_lhs()->get_type());
            machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                             use_vreg(lhs_reg));
            machine_block.append_instruction("mov " + def_vreg(quotient_reg) + ", " +
                                             use_vreg(lhs_reg));
            machine_block.append_instruction("sdiv " + def_vreg(quotient_reg) + ", " +
                                             use_vreg(quotient_reg) + ", " +
                                             use_vreg(rhs_reg));
            machine_block.append_instruction("mul " + def_vreg(product_reg) + ", " +
                                             use_vreg(quotient_reg) + ", " +
                                             use_vreg(rhs_reg));
            machine_block.append_instruction("sub " + def_vreg(dst_reg) + ", " +
                                             use_vreg(dst_reg) + ", " +
                                             use_vreg(product_reg));
            return true;
            }
        case CoreIrBinaryOpcode::URem:
            {
            const AArch64VirtualReg quotient_reg =
                create_virtual_reg(*state.machine_function, binary.get_lhs()->get_type());
            const AArch64VirtualReg product_reg =
                create_virtual_reg(*state.machine_function, binary.get_lhs()->get_type());
            machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                             use_vreg(lhs_reg));
            machine_block.append_instruction("mov " + def_vreg(quotient_reg) + ", " +
                                             use_vreg(lhs_reg));
            machine_block.append_instruction("udiv " + def_vreg(quotient_reg) + ", " +
                                             use_vreg(quotient_reg) + ", " +
                                             use_vreg(rhs_reg));
            machine_block.append_instruction("mul " + def_vreg(product_reg) + ", " +
                                             use_vreg(quotient_reg) + ", " +
                                             use_vreg(rhs_reg));
            machine_block.append_instruction("sub " + def_vreg(dst_reg) + ", " +
                                             use_vreg(dst_reg) + ", " +
                                             use_vreg(product_reg));
            return true;
            }
        }
        machine_block.append_instruction(opcode + " " + def_vreg(dst_reg) + ", " +
                                         use_vreg(lhs_reg) + ", " +
                                         use_vreg(rhs_reg));
        return true;
    }

    bool emit_unary(AArch64MachineBlock &machine_block, const CoreIrUnaryInst &unary,
                    const FunctionState &state) {
        AArch64VirtualReg operand_reg;
        AArch64VirtualReg dst_reg;
        if (!ensure_value_in_vreg(machine_block, unary.get_operand(), state, operand_reg) ||
            !require_canonical_vreg(state, &unary, dst_reg)) {
            return false;
        }
        switch (unary.get_unary_opcode()) {
        case CoreIrUnaryOpcode::Negate:
            machine_block.append_instruction("neg " + def_vreg(dst_reg) + ", " +
                                             use_vreg(operand_reg));
            break;
        case CoreIrUnaryOpcode::BitwiseNot:
            machine_block.append_instruction("mvn " + def_vreg(dst_reg) + ", " +
                                             use_vreg(operand_reg));
            break;
        case CoreIrUnaryOpcode::LogicalNot:
            machine_block.append_instruction("cmp " + use_vreg(operand_reg) + ", #0");
            machine_block.append_instruction("cset " + def_vreg(dst_reg) + ", eq");
            break;
        }
        return true;
    }

    bool emit_compare(AArch64MachineBlock &machine_block,
                      const CoreIrCompareInst &compare,
                      const FunctionState &state) {
        AArch64VirtualReg lhs_reg;
        AArch64VirtualReg rhs_reg;
        AArch64VirtualReg dst_reg;
        if (!ensure_value_in_vreg(machine_block, compare.get_lhs(), state, lhs_reg) ||
            !ensure_value_in_vreg(machine_block, compare.get_rhs(), state, rhs_reg) ||
            !require_canonical_vreg(state, &compare, dst_reg)) {
            return false;
        }
        machine_block.append_instruction("cmp " + use_vreg(lhs_reg) + ", " +
                                         use_vreg(rhs_reg));
        machine_block.append_instruction("cset " + def_vreg(dst_reg) + ", " +
                                         condition_code(compare.get_predicate()));
        return true;
    }

    bool emit_cast(AArch64MachineBlock &machine_block, const CoreIrCastInst &cast,
                   const FunctionState &state) {
        AArch64VirtualReg operand_reg;
        AArch64VirtualReg dst_reg;
        if (!ensure_value_in_vreg(machine_block, cast.get_operand(), state, operand_reg) ||
            !require_canonical_vreg(state, &cast, dst_reg)) {
            return false;
        }
        switch (cast.get_cast_kind()) {
        case CoreIrCastKind::Truncate:
            machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                             use_vreg_as(operand_reg,
                                                         uses_64bit_register(cast.get_type())));
            apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
            break;
        case CoreIrCastKind::ZeroExtend:
            machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) + ", " +
                                             use_vreg_as(operand_reg, false));
            apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                             cast.get_operand()->get_type(),
                                             cast.get_type());
            break;
        case CoreIrCastKind::SignExtend:
            if (uses_64bit_register(cast.get_type())) {
                machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) +
                                                 ", " + use_vreg_as(operand_reg, false));
            } else {
                machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                                 use_vreg(operand_reg));
            }
            apply_sign_extend_to_virtual_reg(machine_block, dst_reg,
                                             cast.get_operand()->get_type(),
                                             cast.get_type());
            break;
        case CoreIrCastKind::PtrToInt:
            if (uses_64bit_register(cast.get_operand()->get_type()) &&
                !uses_64bit_register(cast.get_type())) {
                machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) +
                                                 ", " + use_vreg_as(operand_reg, false));
            } else {
                machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                                 use_vreg_as(operand_reg,
                                                             uses_64bit_register(cast.get_type())));
            }
            apply_truncate_to_virtual_reg(machine_block, dst_reg, cast.get_type());
            break;
        case CoreIrCastKind::IntToPtr:
            if (uses_64bit_register(cast.get_operand()->get_type())) {
                machine_block.append_instruction("mov " + def_vreg(dst_reg) + ", " +
                                                 use_vreg_as(operand_reg, true));
            } else {
                machine_block.append_instruction("mov " + def_vreg_as(dst_reg, false) +
                                                 ", " + use_vreg_as(operand_reg, false));
                apply_zero_extend_to_virtual_reg(machine_block, dst_reg,
                                                 cast.get_operand()->get_type(),
                                                 cast.get_type());
            }
            break;
        case CoreIrCastKind::SignedIntToFloat:
        case CoreIrCastKind::UnsignedIntToFloat:
        case CoreIrCastKind::FloatToSignedInt:
        case CoreIrCastKind::FloatToUnsignedInt:
        case CoreIrCastKind::FloatExtend:
        case CoreIrCastKind::FloatTruncate:
            add_backend_error(diagnostic_engine_,
                              "floating-point casts are not supported by the "
                              "AArch64 native backend");
            return false;
        }
        return true;
    }

    bool emit_call(AArch64MachineBlock &machine_block, const CoreIrCallInst &call,
                   const FunctionState &state) {
        if (call.get_callee_type() != nullptr && call.get_callee_type()->get_is_variadic()) {
            add_backend_error(diagnostic_engine_,
                              "variadic calls are not supported by the AArch64 "
                              "native backend");
            return false;
        }

        const auto &arguments = call.get_operands();
        const std::size_t argument_count =
            arguments.size() - call.get_argument_begin_index();
        const std::size_t stack_arg_bytes =
            argument_count > 8 ? align_to((argument_count - 8) * 8, 16) : 0;
        if (stack_arg_bytes > 0) {
            machine_block.append_instruction(
                "sub sp, sp, #" + std::to_string(stack_arg_bytes));
        }
        for (std::size_t index = call.get_argument_begin_index();
             index < arguments.size(); ++index) {
            CoreIrValue *argument = arguments[index];
            if (!is_supported_value_type(argument->get_type())) {
                add_backend_error(diagnostic_engine_,
                                  "unsupported call argument type in the "
                                  "AArch64 native backend");
                return false;
            }
            const std::size_t argument_index =
                index - call.get_argument_begin_index();
            if (argument_index < 8) {
                AArch64VirtualReg arg_value;
                if (!ensure_value_in_vreg(machine_block, argument, state, arg_value)) {
                    return false;
                }
                machine_block.append_instruction(
                    "mov " +
                    general_register_name(static_cast<unsigned>(argument_index),
                                          uses_64bit_register(argument->get_type())) +
                    ", " + use_vreg(arg_value));
                continue;
            }
            AArch64VirtualReg arg_value;
            if (!ensure_value_in_vreg(machine_block, argument, state, arg_value)) {
                return false;
            }
            const std::size_t stack_slot_offset = (argument_index - 8) * 8;
            machine_block.append_instruction(
                store_mnemonic_for_type(argument->get_type()) + " " +
                use_vreg(arg_value) +
                ", [sp, #" + std::to_string(stack_slot_offset) + "]");
        }
        if (!call.get_is_direct_call()) {
            AArch64VirtualReg callee_reg;
            if (!ensure_value_in_vreg(machine_block, call.get_callee_value(), state,
                                      callee_reg)) {
                return false;
            }
            machine_block.append_instruction(AArch64MachineInstr(
                "blr",
                {AArch64MachineOperand(use_vreg(callee_reg))},
                AArch64InstructionFlags{.is_call = true}, {}, {},
                make_default_call_clobber_mask()));
        } else {
            machine_block.append_instruction(AArch64MachineInstr(
                "bl", {AArch64MachineOperand(call.get_callee_name())},
                AArch64InstructionFlags{.is_call = true}, {}, {},
                make_default_call_clobber_mask()));
        }
        if (stack_arg_bytes > 0) {
            machine_block.append_instruction(
                "add sp, sp, #" + std::to_string(stack_arg_bytes));
        }
        if (!is_void_type(call.get_type())) {
            AArch64VirtualReg result_reg;
            if (!require_canonical_vreg(state, &call, result_reg)) {
                return false;
            }
            machine_block.append_instruction("mov " + def_vreg(result_reg) + ", " +
                                             general_register_name(
                                                 0, uses_64bit_register(call.get_type())));
        }
        return true;
    }

    bool emit_cond_jump(AArch64MachineBlock &machine_block,
                        const CoreIrCondJumpInst &cond_jump,
                        const FunctionState &state) {
        AArch64VirtualReg condition_reg;
        if (!ensure_value_in_vreg(machine_block, cond_jump.get_condition(), state,
                                  condition_reg)) {
            return false;
        }
        machine_block.append_instruction(
            "cbnz " + use_vreg(condition_reg) + ", " +
            block_labels_.at(cond_jump.get_true_block()));
        machine_block.append_instruction(
            "b " + block_labels_.at(cond_jump.get_false_block()));
        return true;
    }

    bool emit_return(AArch64MachineFunction &machine_function,
                     AArch64MachineBlock &machine_block,
                     const CoreIrReturnInst &return_inst,
                     const FunctionState &state) {
        if (return_inst.get_return_value() != nullptr) {
            AArch64VirtualReg return_reg;
            if (!ensure_value_in_vreg(machine_block, return_inst.get_return_value(),
                                      state, return_reg)) {
                return false;
            }
            const bool use_64bit =
                uses_64bit_register(return_inst.get_return_value()->get_type());
            machine_block.append_instruction("mov " + general_register_name(0, use_64bit) +
                                             ", " + use_vreg(return_reg));
        }
        machine_block.append_instruction("b " + machine_function.get_epilogue_label());
        return true;
    }
};

} // namespace

AArch64MachineInstr::AArch64MachineInstr(std::string text) {
    auto parsed = parse_machine_instruction_text(std::move(text));
    mnemonic_ = std::move(parsed.first);
    operands_ = std::move(parsed.second);
    explicit_defs_ = collect_explicit_vreg_ids(operands_, true);
    explicit_uses_ = collect_explicit_vreg_ids(operands_, false);
}

AArch64MachineInstr::AArch64MachineInstr(
    std::string mnemonic, std::vector<AArch64MachineOperand> operands,
    AArch64InstructionFlags flags, std::vector<std::size_t> implicit_defs,
    std::vector<std::size_t> implicit_uses,
    std::optional<AArch64CallClobberMask> call_clobber_mask)
    : mnemonic_(std::move(mnemonic)),
      operands_(std::move(operands)),
      flags_(flags),
      explicit_defs_(collect_explicit_vreg_ids(operands_, true)),
      explicit_uses_(collect_explicit_vreg_ids(operands_, false)),
      implicit_defs_(std::move(implicit_defs)),
      implicit_uses_(std::move(implicit_uses)),
      call_clobber_mask_(std::move(call_clobber_mask)) {}

std::size_t
AArch64FunctionFrameInfo::get_stack_slot_offset(
    const CoreIrStackSlot *stack_slot) const {
    return stack_slot_offsets_.at(stack_slot);
}

std::string
AArch64AsmPrinter::print_module(const AArch64MachineModule &module) const {
    std::ostringstream output;
    for (const std::string &line : module.get_global_lines()) {
        output << line << "\n";
    }
    for (const AArch64MachineFunction &function : module.get_functions()) {
        if (!module.get_global_lines().empty() || &function != &module.get_functions().front()) {
            output << "\n";
        }
        output << ".text\n";
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
                        output << substitute_virtual_registers(
                            instruction.get_operands()[index].get_text(), function);
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
AArch64AsmBackend::Generate(const CoreIrModule &module,
                            const BackendOptions &backend_options,
                            DiagnosticEngine &diagnostic_engine) const {
    AArch64LoweringSession session(module, backend_options, diagnostic_engine);
    return session.Generate();
}

} // namespace sysycc
