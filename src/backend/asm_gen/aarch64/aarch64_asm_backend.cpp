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
constexpr unsigned kAllocatablePhysicalRegs[] = {9, 10, 11, 12, 13, 14, 15, 17};

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

std::string use_vreg(const AArch64VirtualReg &reg) {
    return vreg_token('u', reg);
}

std::string def_vreg(const AArch64VirtualReg &reg) {
    return vreg_token('d', reg);
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

struct AArch64LivenessInterval {
    std::size_t virtual_reg_id = 0;
    bool use_64bit = false;
    std::size_t start = 0;
    std::size_t end = 0;
};

class AArch64LivenessInfo {
  private:
    std::vector<AArch64LivenessInterval> intervals_;

  public:
    explicit AArch64LivenessInfo(std::vector<AArch64LivenessInterval> intervals)
        : intervals_(std::move(intervals)) {}

    const std::vector<AArch64LivenessInterval> &get_intervals() const noexcept {
        return intervals_;
    }
};

AArch64LivenessInfo build_liveness_info(const AArch64MachineFunction &function) {
    std::unordered_map<std::size_t, AArch64LivenessInterval> intervals;
    std::unordered_set<std::size_t> initialized;
    std::size_t instruction_index = 0;
    for (const AArch64MachineBlock &block : function.get_blocks()) {
        for (const AArch64MachineInstr &instruction : block.get_instructions()) {
            for (const AArch64MachineOperand &operand : instruction.get_operands()) {
                for (const ParsedVirtualRegRef &ref :
                     parse_virtual_reg_refs(operand.get_text())) {
                    auto &interval = intervals[ref.id];
                    interval.virtual_reg_id = ref.id;
                    interval.use_64bit = ref.use_64bit;
                    if (initialized.insert(ref.id).second) {
                        interval.start = instruction_index;
                        interval.end = instruction_index;
                        continue;
                    }
                    interval.end = std::max(interval.end, instruction_index);
                }
            }
            ++instruction_index;
        }
    }

    std::vector<AArch64LivenessInterval> result;
    result.reserve(intervals.size());
    for (auto &[_, interval] : intervals) {
        result.push_back(interval);
    }
    std::sort(result.begin(), result.end(),
              [](const AArch64LivenessInterval &lhs,
                 const AArch64LivenessInterval &rhs) {
                  if (lhs.start != rhs.start) {
                      return lhs.start < rhs.start;
                  }
                  return lhs.virtual_reg_id < rhs.virtual_reg_id;
              });
    return AArch64LivenessInfo(std::move(result));
}

bool is_temp_physical_reg_number(unsigned reg_number) {
    switch (reg_number) {
    case 9:
    case 10:
    case 11:
    case 14:
    case 15:
    case 16:
        return true;
    default:
        return false;
    }
}

struct ParsedPhysicalRegRef {
    unsigned reg_number = 0;
    bool use_64bit = false;
    std::size_t offset = 0;
    std::size_t length = 0;
};

std::vector<ParsedPhysicalRegRef> parse_temp_physical_regs(const std::string &text) {
    std::vector<ParsedPhysicalRegRef> refs;
    for (std::size_t index = 0; index + 1 < text.size(); ++index) {
        if (text[index] != 'x' && text[index] != 'w') {
            continue;
        }
        if (index > 0 &&
            (std::isalnum(static_cast<unsigned char>(text[index - 1])) != 0 ||
             text[index - 1] == '_')) {
            continue;
        }
        std::size_t cursor = index + 1;
        if (cursor >= text.size() ||
            std::isdigit(static_cast<unsigned char>(text[cursor])) == 0) {
            continue;
        }
        unsigned reg_number = 0;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])) != 0) {
            reg_number =
                (reg_number * 10U) + static_cast<unsigned>(text[cursor] - '0');
            ++cursor;
        }
        if (!is_temp_physical_reg_number(reg_number)) {
            continue;
        }
        if (cursor < text.size() &&
            (std::isalnum(static_cast<unsigned char>(text[cursor])) != 0 ||
             text[cursor] == '_')) {
            continue;
        }
        refs.push_back({reg_number, text[index] == 'x', index, cursor - index});
        index = cursor - 1;
    }
    return refs;
}

bool instruction_defines_first_operand(const std::string &mnemonic) {
    return mnemonic == "mov" || mnemonic == "movz" || mnemonic == "movk" ||
           mnemonic == "adrp" || mnemonic == "add" || mnemonic == "sub" ||
           mnemonic == "ldr" || mnemonic == "ldur" || mnemonic == "ldrb" ||
           mnemonic == "ldrh" || mnemonic == "ldurb" || mnemonic == "ldurh" ||
           mnemonic == "mul" || mnemonic == "sdiv" || mnemonic == "udiv" ||
           mnemonic == "msub" || mnemonic == "and" || mnemonic == "orr" ||
           mnemonic == "eor" || mnemonic == "lsl" || mnemonic == "lsr" ||
           mnemonic == "asr" || mnemonic == "neg" || mnemonic == "mvn" ||
           mnemonic == "cset" || mnemonic == "sxtw" || mnemonic == "uxtb" ||
           mnemonic == "uxth" || mnemonic == "sxtb" || mnemonic == "sxth";
}

bool instruction_keeps_first_operand_mapping(const std::string &mnemonic) {
    return mnemonic == "movk";
}

std::string replace_temp_reg_with_token(
    const std::string &text,
    const std::function<std::string(const ParsedPhysicalRegRef &)> &mapper) {
    std::string rendered = text;
    const std::vector<ParsedPhysicalRegRef> refs = parse_temp_physical_regs(text);
    std::size_t delta = 0;
    for (const ParsedPhysicalRegRef &ref : refs) {
        const std::string replacement = mapper(ref);
        if (replacement.empty()) {
            continue;
        }
        rendered.replace(ref.offset + delta, ref.length, replacement);
        delta += replacement.size() - ref.length;
    }
    return rendered;
}

void rename_temporary_registers_to_virtuals(AArch64MachineFunction &function) {
    std::unordered_map<unsigned, AArch64VirtualReg> current_mapping;
    for (AArch64MachineBlock &block : function.get_blocks()) {
        for (AArch64MachineInstr &instruction : block.get_instructions()) {
            auto &operands = instruction.get_operands();
            std::unordered_map<unsigned, AArch64VirtualReg> previous_mapping = current_mapping;

            for (std::size_t operand_index = 0; operand_index < operands.size();
                 ++operand_index) {
                const bool defines_first =
                    operand_index == 0 &&
                    instruction_defines_first_operand(instruction.get_mnemonic());
                if (defines_first) {
                    continue;
                }
                std::string rewritten = replace_temp_reg_with_token(
                    operands[operand_index].get_text(),
                    [&](const ParsedPhysicalRegRef &ref) -> std::string {
                        const auto it = previous_mapping.find(ref.reg_number);
                        if (it == previous_mapping.end()) {
                            return "";
                        }
                        return use_vreg(it->second);
                    });
                operands[operand_index] = AArch64MachineOperand(std::move(rewritten));
            }

            if (!operands.empty() &&
                instruction_defines_first_operand(instruction.get_mnemonic())) {
                const std::vector<ParsedPhysicalRegRef> defs =
                    parse_temp_physical_regs(operands.front().get_text());
                if (!defs.empty()) {
                    const ParsedPhysicalRegRef &def_ref = defs.front();
                    AArch64VirtualReg vreg;
                    if (instruction_keeps_first_operand_mapping(
                            instruction.get_mnemonic()) &&
                        previous_mapping.find(def_ref.reg_number) !=
                            previous_mapping.end()) {
                        vreg = previous_mapping.at(def_ref.reg_number);
                    } else {
                        vreg = function.create_virtual_reg(def_ref.use_64bit);
                        current_mapping[def_ref.reg_number] = vreg;
                    }
                    std::string rewritten = replace_temp_reg_with_token(
                        operands.front().get_text(),
                        [&](const ParsedPhysicalRegRef &ref) -> std::string {
                            if (ref.reg_number != def_ref.reg_number) {
                                const auto it = previous_mapping.find(ref.reg_number);
                                if (it == previous_mapping.end()) {
                                    return "";
                                }
                                return use_vreg(it->second);
                            }
                            return instruction_keeps_first_operand_mapping(
                                       instruction.get_mnemonic())
                                       ? use_vreg(vreg)
                                       : def_vreg(vreg);
                        });
                    operands.front() = AArch64MachineOperand(std::move(rewritten));
                }
            }
        }
    }
}

bool allocate_virtual_registers(AArch64MachineFunction &function,
                                DiagnosticEngine &diagnostic_engine) {
    const AArch64LivenessInfo liveness = build_liveness_info(function);
    struct ActiveInterval {
        AArch64LivenessInterval interval;
        unsigned physical_reg = 0;
    };

    std::vector<ActiveInterval> active;
    std::set<unsigned> available(std::begin(kAllocatablePhysicalRegs),
                                 std::end(kAllocatablePhysicalRegs));

    for (const AArch64LivenessInterval &interval : liveness.get_intervals()) {
        active.erase(
            std::remove_if(active.begin(), active.end(),
                           [&](const ActiveInterval &entry) {
                               if (entry.interval.end < interval.start) {
                                   available.insert(entry.physical_reg);
                                   return true;
                               }
                               return false;
                           }),
            active.end());

        if (available.empty()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 register allocator ran out of temporary registers");
            return false;
        }

        const unsigned physical_reg = *available.begin();
        available.erase(available.begin());
        function.set_virtual_reg_allocation(interval.virtual_reg_id, physical_reg);
        active.push_back({interval, physical_reg});
        std::sort(active.begin(), active.end(),
                  [](const ActiveInterval &lhs, const ActiveInterval &rhs) {
                      return lhs.interval.end < rhs.interval.end;
                  });
    }

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

class AArch64LoweringSession {
  private:
    const CoreIrModule &module_;
    const BackendOptions &backend_options_;
    DiagnosticEngine &diagnostic_engine_;
    AArch64AsmPrinter printer_;
    std::unordered_map<const CoreIrBasicBlock *, std::string> block_labels_;

    struct FunctionState {
        AArch64MachineFunction *machine_function = nullptr;
        std::unordered_map<const CoreIrParameter *, std::size_t> parameter_offsets;
        std::unordered_map<const CoreIrParameter *, std::size_t>
            incoming_stack_argument_offsets;
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
        for (const auto &parameter : function.get_parameters()) {
            current_offset = align_to(current_offset,
                                      get_storage_alignment_for_value(*parameter));
            current_offset += get_storage_size_for_value(*parameter);
            machine_function.get_frame_info().set_value_offset(parameter.get(),
                                                               current_offset);
            state.parameter_offsets.emplace(parameter.get(), current_offset);
        }
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
        for (const auto &basic_block : function.get_basic_blocks()) {
            for (const auto &instruction : basic_block->get_instructions()) {
                if (!instruction_produces_spill_value(*instruction)) {
                    continue;
                }
                if (!is_supported_value_type(instruction->get_type())) {
                    add_backend_error(
                        diagnostic_engine_,
                        "unsupported Core IR value type in AArch64 native backend "
                        "for function '" +
                            function_name + "'");
                    return false;
                }
                current_offset =
                    align_to(current_offset, get_storage_alignment_for_value(*instruction));
                current_offset += get_storage_size_for_value(*instruction);
                machine_function.get_frame_info().set_value_offset(instruction.get(),
                                                                   current_offset);
            }
        }

        const std::size_t frame_size = align_to(current_offset, 16);
        machine_function.get_frame_info().set_local_size(current_offset);
        machine_function.get_frame_info().set_frame_size(frame_size);

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
        rename_temporary_registers_to_virtuals(machine_function);
        if (!allocate_virtual_registers(machine_function, diagnostic_engine_)) {
            return false;
        }
        return true;
    }

    static bool instruction_produces_spill_value(const CoreIrInstruction &instruction) {
        switch (instruction.get_opcode()) {
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
        case CoreIrOpcode::Store:
        case CoreIrOpcode::Jump:
        case CoreIrOpcode::CondJump:
        case CoreIrOpcode::Return:
            return false;
        }
        return false;
    }

    static std::size_t get_storage_size_for_value(const CoreIrValue &value) {
        const auto *integer_type = as_integer_type(value.get_type());
        if (integer_type != nullptr && integer_type->get_bit_width() == 1) {
            return 4;
        }
        return get_storage_size(value.get_type());
    }

    static std::size_t
    get_storage_alignment_for_value(const CoreIrValue &value) {
        const auto *integer_type = as_integer_type(value.get_type());
        if (integer_type != nullptr && integer_type->get_bit_width() == 1) {
            return 4;
        }
        return get_storage_alignment(value.get_type());
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
            const std::size_t offset = state.parameter_offsets.at(parameter);
            if (index < 8) {
                append_store_to_frame(
                    prologue_block, parameter->get_type(),
                    general_register_name(static_cast<unsigned>(index),
                                          uses_64bit_register(parameter->get_type())),
                    offset);
                continue;
            }
            append_load_from_incoming_stack_arg(
                prologue_block, parameter->get_type(), 14,
                state.incoming_stack_argument_offsets.at(parameter));
            append_store_to_frame(
                prologue_block, parameter->get_type(),
                general_register_name(14, uses_64bit_register(parameter->get_type())),
                offset);
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
                          const FunctionState &state) {
        switch (instruction.get_opcode()) {
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
        case CoreIrOpcode::AddressOfGlobal:
        case CoreIrOpcode::AddressOfFunction:
        case CoreIrOpcode::GetElementPtr:
            return true;
        }
        return false;
    }

    bool materialize_value(AArch64MachineBlock &machine_block,
                           const CoreIrValue *value, unsigned register_index,
                           const FunctionState &state) {
        if (value == nullptr) {
            add_backend_error(diagnostic_engine_,
                              "encountered null Core IR value during AArch64 "
                              "lowering");
            return false;
        }

        if (const auto *int_constant =
                dynamic_cast<const CoreIrConstantInt *>(value);
            int_constant != nullptr) {
            return materialize_integer_constant(machine_block, value->get_type(),
                                                int_constant->get_value(),
                                                register_index);
        }
        if (dynamic_cast<const CoreIrConstantNull *>(value) != nullptr) {
            machine_block.append_instruction(
                "mov " +
                general_register_name(register_index, uses_64bit_register(value->get_type())) +
                ", " + zero_register_name(uses_64bit_register(value->get_type())));
            return true;
        }
        if (dynamic_cast<const CoreIrConstantZeroInitializer *>(value) != nullptr) {
            machine_block.append_instruction(
                "mov " +
                general_register_name(register_index, uses_64bit_register(value->get_type())) +
                ", " + zero_register_name(uses_64bit_register(value->get_type())));
            return true;
        }
        if (const auto *global_address =
                dynamic_cast<const CoreIrConstantGlobalAddress *>(value);
            global_address != nullptr) {
            return materialize_global_address(machine_block,
                                              global_address->get_global()->get_name(),
                                              register_index);
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
                register_index, state);
        }
        if (const auto *parameter = dynamic_cast<const CoreIrParameter *>(value);
            parameter != nullptr) {
            const std::size_t offset = state.parameter_offsets.at(parameter);
            append_load_from_frame(machine_block, parameter->get_type(), register_index,
                                   offset);
            return true;
        }
        if (const auto *address_of_stack_slot =
                dynamic_cast<const CoreIrAddressOfStackSlotInst *>(value);
            address_of_stack_slot != nullptr) {
            const std::size_t offset =
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    address_of_stack_slot->get_stack_slot());
            append_frame_address(machine_block, register_index, offset);
            return true;
        }
        if (const auto *address_of_global =
                dynamic_cast<const CoreIrAddressOfGlobalInst *>(value);
            address_of_global != nullptr) {
            return materialize_global_address(
                machine_block, address_of_global->get_global()->get_name(),
                register_index);
        }
        if (const auto *address_of_function =
                dynamic_cast<const CoreIrAddressOfFunctionInst *>(value);
            address_of_function != nullptr) {
            return materialize_global_address(
                machine_block, address_of_function->get_function()->get_name(),
                register_index);
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
                register_index, state);
        }
        if (const auto *instruction = dynamic_cast<const CoreIrInstruction *>(value);
            instruction != nullptr &&
            state.machine_function->get_frame_info().has_value_offset(instruction)) {
            const std::size_t offset =
                state.machine_function->get_frame_info().get_value_offset(instruction);
            append_load_from_frame(machine_block, instruction->get_type(),
                                   register_index, offset);
            return true;
        }

        add_backend_error(diagnostic_engine_,
                          "unsupported Core IR value in AArch64 native backend");
        return false;
    }

    bool materialize_integer_constant(AArch64MachineBlock &machine_block,
                                      const CoreIrType *type, std::uint64_t value,
                                      unsigned register_index) {
        const bool use_64bit = uses_64bit_register(type);
        const std::string reg = general_register_name(register_index, use_64bit);
        const unsigned pieces = use_64bit ? 4U : 2U;
        bool emitted = false;
        for (unsigned piece = 0; piece < pieces; ++piece) {
            const std::uint16_t imm16 =
                static_cast<std::uint16_t>((value >> (piece * 16U)) & 0xFFFFU);
            if (!emitted) {
                machine_block.append_instruction("movz " + reg + ", #" +
                                                 std::to_string(imm16) + ", lsl #" +
                                                 std::to_string(piece * 16U));
                emitted = true;
                continue;
            }
            if (imm16 == 0) {
                continue;
            }
            machine_block.append_instruction("movk " + reg + ", #" +
                                             std::to_string(imm16) + ", lsl #" +
                                             std::to_string(piece * 16U));
        }
        if (!emitted) {
            machine_block.append_instruction("mov " + reg + ", " +
                                             zero_register_name(use_64bit));
        }
        return true;
    }

    bool materialize_global_address(AArch64MachineBlock &machine_block,
                                    const std::string &symbol_name,
                                    unsigned register_index) {
        const std::string reg = general_register_name(register_index, true);
        machine_block.append_instruction("adrp " + reg + ", " + symbol_name);
        machine_block.append_instruction("add " + reg + ", " + reg +
                                         ", :lo12:" + symbol_name);
        return true;
    }

    void append_frame_address(AArch64MachineBlock &machine_block,
                              unsigned register_index, std::size_t offset) {
        const std::string reg = general_register_name(register_index, true);
        if (offset <= 4095) {
            machine_block.append_instruction("sub " + reg + ", x29, #" +
                                             std::to_string(offset));
            return;
        }
        materialize_integer_constant(machine_block,
                                     create_fake_pointer_type(), offset, register_index);
        machine_block.append_instruction("sub " + reg + ", x29, " + reg);
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

    void truncate_register_to_type(AArch64MachineBlock &machine_block,
                                   unsigned register_index,
                                   const CoreIrType *type) {
        const auto *integer_type = as_integer_type(type);
        if (integer_type == nullptr) {
            return;
        }
        switch (integer_type->get_bit_width()) {
        case 1:
            machine_block.append_instruction(
                "and w" + std::to_string(register_index) + ", w" +
                std::to_string(register_index) + ", #1");
            break;
        case 8:
            machine_block.append_instruction(
                "uxtb w" + std::to_string(register_index) + ", w" +
                std::to_string(register_index));
            break;
        case 16:
            machine_block.append_instruction(
                "uxth w" + std::to_string(register_index) + ", w" +
                std::to_string(register_index));
            break;
        default:
            break;
        }
    }

    void sign_extend_register_for_type(AArch64MachineBlock &machine_block,
                                       unsigned register_index,
                                       const CoreIrType *type) {
        const auto *integer_type = as_integer_type(type);
        if (integer_type == nullptr) {
            return;
        }
        switch (integer_type->get_bit_width()) {
        case 1:
            machine_block.append_instruction(
                "and w" + std::to_string(register_index) + ", w" +
                std::to_string(register_index) + ", #1");
            break;
        case 8:
            machine_block.append_instruction(
                "sxtb w" + std::to_string(register_index) + ", w" +
                std::to_string(register_index));
            break;
        case 16:
            machine_block.append_instruction(
                "sxth w" + std::to_string(register_index) + ", w" +
                std::to_string(register_index));
            break;
        case 32:
            if (uses_64bit_register(type)) {
                machine_block.append_instruction(
                    "sxtw x" + std::to_string(register_index) + ", w" +
                    std::to_string(register_index));
            }
            break;
        default:
            break;
        }
    }

    bool add_constant_offset(AArch64MachineBlock &machine_block, unsigned register_index,
                             long long offset) {
        if (offset == 0) {
            return true;
        }
        const std::string reg = general_register_name(register_index, true);
        const long long absolute_offset = offset >= 0 ? offset : -offset;
        if (absolute_offset <= 4095) {
            machine_block.append_instruction(
                std::string(offset >= 0 ? "add " : "sub ") + reg + ", " + reg +
                ", #" + std::to_string(absolute_offset));
            return true;
        }
        const unsigned temp_register_index =
            register_index == 15 ? 14U : 15U;
        if (!materialize_integer_constant(machine_block, create_fake_pointer_type(),
                                          static_cast<std::uint64_t>(absolute_offset),
                                          temp_register_index)) {
            return false;
        }
        machine_block.append_instruction(
            std::string(offset >= 0 ? "add " : "sub ") + reg + ", " + reg +
            ", " + general_register_name(temp_register_index, true));
        return true;
    }

    bool add_scaled_index(AArch64MachineBlock &machine_block, unsigned base_register_index,
                          const CoreIrValue *index_value, std::size_t scale,
                          const FunctionState &state) {
        if (!materialize_value(machine_block, index_value, 14, state)) {
            return false;
        }
        const auto *index_integer_type = as_integer_type(index_value->get_type());
        if (index_integer_type != nullptr && index_integer_type->get_bit_width() <= 32) {
            machine_block.append_instruction("sxtw x14, w14");
        }

        const std::string base_reg =
            general_register_name(base_register_index, true);
        if (scale == 1) {
            machine_block.append_instruction("add " + base_reg + ", " + base_reg +
                                             ", x14");
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
            machine_block.append_instruction("add " + base_reg + ", " + base_reg +
                                             ", x14, lsl #" +
                                             std::to_string(shift));
            return true;
        }

        if (!materialize_integer_constant(machine_block, create_fake_pointer_type(),
                                          static_cast<std::uint64_t>(scale), 15)) {
            return false;
        }
        machine_block.append_instruction("mul x14, x14, x15");
        machine_block.append_instruction("add " + base_reg + ", " + base_reg +
                                         ", x14");
        return true;
    }

    bool materialize_gep_value(AArch64MachineBlock &machine_block,
                               const CoreIrValue *base_value,
                               const CoreIrType *base_type,
                               std::size_t index_count,
                               const std::function<CoreIrValue *(std::size_t)> &get_index_value,
                               unsigned register_index,
                               const FunctionState &state) {
        if (!materialize_value(machine_block, base_value, register_index, state)) {
            return false;
        }

        const CoreIrType *current_type = base_type;
        for (std::size_t index_position = 0; index_position < index_count;
             ++index_position) {
            CoreIrValue *index_value = get_index_value(index_position);
            if (index_value == nullptr) {
                add_backend_error(diagnostic_engine_,
                                  "encountered null gep index in AArch64 native "
                                  "backend");
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
                    if (!add_constant_offset(machine_block, register_index,
                                             *index *
                                                 static_cast<long long>(
                                                     get_type_size(current_type)))) {
                        return false;
                    }
                    continue;
                }
                if (const auto *array_type =
                        dynamic_cast<const CoreIrArrayType *>(current_type);
                    array_type != nullptr) {
                    if (!add_constant_offset(machine_block, register_index,
                                             *index * static_cast<long long>(
                                                          get_type_size(array_type->get_element_type())))) {
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
                                          "unsupported gep struct index in "
                                          "AArch64 native backend");
                        return false;
                    }
                    if (!add_constant_offset(machine_block, register_index,
                                             static_cast<long long>(
                                                 get_struct_member_offset(
                                                     struct_type,
                                                     static_cast<std::size_t>(*index))))) {
                        return false;
                    }
                    current_type = struct_type->get_element_types()
                                       [static_cast<std::size_t>(*index)];
                    continue;
                }
                if (const auto *pointer_type =
                        dynamic_cast<const CoreIrPointerType *>(current_type);
                    pointer_type != nullptr) {
                    if (!add_constant_offset(machine_block, register_index,
                                             *index * static_cast<long long>(
                                                          get_type_size(pointer_type->get_pointee_type())))) {
                        return false;
                    }
                    current_type = pointer_type->get_pointee_type();
                    continue;
                }
                add_backend_error(diagnostic_engine_,
                                  "unsupported constant gep shape in AArch64 "
                                  "native backend");
                return false;
            }

            if (index_position == 0) {
                if (!add_scaled_index(machine_block, register_index, index_value,
                                      get_type_size(current_type), state)) {
                    return false;
                }
                continue;
            }
            if (const auto *array_type = dynamic_cast<const CoreIrArrayType *>(current_type);
                array_type != nullptr) {
                if (!add_scaled_index(machine_block, register_index, index_value,
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
                if (!add_scaled_index(machine_block, register_index, index_value,
                                      get_type_size(pointer_type->get_pointee_type()),
                                      state)) {
                    return false;
                }
                current_type = pointer_type->get_pointee_type();
                continue;
            }

            add_backend_error(diagnostic_engine_,
                              "unsupported non-constant aggregate gep in "
                              "AArch64 native backend");
            return false;
        }

        return true;
    }

    bool append_memory_store(AArch64MachineBlock &machine_block, const CoreIrType *type,
                             const std::string &source_reg, unsigned address_reg_index,
                             std::size_t offset) {
        const std::string address_reg =
            general_register_name(address_reg_index, true);
        if (offset <= 4095) {
            machine_block.append_instruction(store_mnemonic_for_type(type) + " " +
                                             source_reg + ", [" + address_reg +
                                             ", #" + std::to_string(offset) + "]");
            return true;
        }
        machine_block.append_instruction("mov x15, " + address_reg);
        if (!add_constant_offset(machine_block, 15, static_cast<long long>(offset))) {
            return false;
        }
        machine_block.append_instruction(store_mnemonic_for_type(type) + " " +
                                         source_reg + ", [x15]");
        return true;
    }

    bool emit_zero_fill(AArch64MachineBlock &machine_block, unsigned address_reg_index,
                        const CoreIrType *type) {
        std::size_t remaining = get_type_size(type);
        std::size_t offset = 0;
        while (remaining >= 8) {
            append_memory_store(machine_block, create_fake_pointer_type(), "xzr",
                                address_reg_index, offset);
            offset += 8;
            remaining -= 8;
        }
        if (remaining >= 4) {
            static CoreIrIntegerType i32_type(32);
            append_memory_store(machine_block, &i32_type, "wzr", address_reg_index,
                                offset);
            offset += 4;
            remaining -= 4;
        }
        if (remaining >= 2) {
            static CoreIrIntegerType i16_type(16);
            append_memory_store(machine_block, &i16_type, "wzr", address_reg_index,
                                offset);
            offset += 2;
            remaining -= 2;
        }
        if (remaining >= 1) {
            static CoreIrIntegerType i8_type(8);
            append_memory_store(machine_block, &i8_type, "wzr", address_reg_index,
                                offset);
        }
        return true;
    }

    void append_load_from_frame(AArch64MachineBlock &machine_block,
                                const CoreIrType *type, unsigned register_index,
                                std::size_t offset) {
        const bool use_64bit = uses_64bit_register(type);
        const std::string reg = general_register_name(register_index, use_64bit);
        std::string mnemonic = "ldur";
        if (get_storage_size(type) == 2) {
            mnemonic = "ldurh";
        } else if (get_storage_size(type) == 1) {
            mnemonic = "ldurb";
        }
        if (offset <= 255) {
            machine_block.append_instruction(mnemonic + " " + reg +
                                             ", [x29, #-" +
                                             std::to_string(offset) + "]");
            if (is_narrow_integer_type(type)) {
                truncate_register_to_type(machine_block, register_index, type);
            }
            return;
        }
        append_frame_address(machine_block, 15, offset);
        machine_block.append_instruction(load_mnemonic_for_type(type) + " " + reg +
                                         ", [x15]");
        if (is_narrow_integer_type(type)) {
            truncate_register_to_type(machine_block, register_index, type);
        }
    }

    void append_store_to_frame(AArch64MachineBlock &machine_block,
                               const CoreIrType *type, const std::string &reg,
                               std::size_t offset) {
        std::string mnemonic = "stur";
        if (get_storage_size(type) == 2) {
            mnemonic = "sturh";
        } else if (get_storage_size(type) == 1) {
            mnemonic = "sturb";
        }
        if (offset <= 255) {
            machine_block.append_instruction(mnemonic + " " + reg + ", [x29, #-" +
                                             std::to_string(offset) + "]");
            return;
        }
        append_frame_address(machine_block, 15, offset);
        machine_block.append_instruction(store_mnemonic_for_type(type) + " " + reg +
                                         ", [x15]");
    }

    void append_load_from_incoming_stack_arg(AArch64MachineBlock &machine_block,
                                             const CoreIrType *type,
                                             unsigned register_index,
                                             std::size_t offset) {
        const bool use_64bit = uses_64bit_register(type);
        const std::string reg = general_register_name(register_index, use_64bit);
        if (offset <= 4095) {
            machine_block.append_instruction(load_mnemonic_for_type(type) + " " + reg +
                                             ", [x29, #" + std::to_string(offset) +
                                             "]");
        } else {
            machine_block.append_instruction("mov x15, x29");
            add_constant_offset(machine_block, 15, static_cast<long long>(offset));
            machine_block.append_instruction(load_mnemonic_for_type(type) + " " + reg +
                                             ", [x15]");
        }
        if (is_narrow_integer_type(type)) {
            truncate_register_to_type(machine_block, register_index, type);
        }
    }

    bool emit_load(AArch64MachineBlock &machine_block, const CoreIrLoadInst &load,
                   const FunctionState &state) {
        const bool use_64bit = uses_64bit_register(load.get_type());
        const std::string reg = general_register_name(9, use_64bit);
        if (load.get_stack_slot() != nullptr) {
            append_load_from_frame(
                machine_block, load.get_type(), 9,
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    load.get_stack_slot()));
        } else {
            if (!materialize_value(machine_block, load.get_address(), 10, state)) {
                return false;
            }
            machine_block.append_instruction(load_mnemonic_for_type(load.get_type()) +
                                             " " + reg + ", [x10]");
            if (is_narrow_integer_type(load.get_type())) {
                truncate_register_to_type(machine_block, 9, load.get_type());
            }
        }
        append_store_to_frame(machine_block, load.get_type(), reg,
                              state.machine_function->get_frame_info().get_value_offset(
                                  &load));
        return true;
    }

    bool emit_store(AArch64MachineBlock &machine_block,
                    const CoreIrStoreInst &store, const FunctionState &state) {
        if (const auto *zero_initializer =
                dynamic_cast<const CoreIrConstantZeroInitializer *>(store.get_value());
            zero_initializer != nullptr) {
            if (store.get_stack_slot() != nullptr) {
                append_frame_address(
                    machine_block, 10,
                    state.machine_function->get_frame_info().get_stack_slot_offset(
                        store.get_stack_slot()));
            } else if (!materialize_value(machine_block, store.get_address(), 10,
                                          state)) {
                return false;
            }
            return emit_zero_fill(machine_block, 10, zero_initializer->get_type());
        }

        if (!materialize_value(machine_block, store.get_value(), 9, state)) {
            return false;
        }
        truncate_register_to_type(machine_block, 9, store.get_value()->get_type());
        const std::string reg =
            general_register_name(9, uses_64bit_register(store.get_value()->get_type()));
        if (store.get_stack_slot() != nullptr) {
            append_store_to_frame(
                machine_block, store.get_value()->get_type(), reg,
                state.machine_function->get_frame_info().get_stack_slot_offset(
                    store.get_stack_slot()));
            return true;
        }
        if (!materialize_value(machine_block, store.get_address(), 10, state)) {
            return false;
        }
        machine_block.append_instruction(store_mnemonic_for_type(store.get_value()->get_type()) +
                                         " " + reg + ", [x10]");
        return true;
    }

    bool emit_binary(AArch64MachineBlock &machine_block,
                     const CoreIrBinaryInst &binary, const FunctionState &state) {
        if (!materialize_value(machine_block, binary.get_lhs(), 9, state) ||
            !materialize_value(machine_block, binary.get_rhs(), 10, state)) {
            return false;
        }
        const auto *result_integer_type = as_integer_type(binary.get_type());
        if (result_integer_type != nullptr) {
            if (binary.get_binary_opcode() == CoreIrBinaryOpcode::AShr ||
                binary.get_binary_opcode() == CoreIrBinaryOpcode::SDiv ||
                binary.get_binary_opcode() == CoreIrBinaryOpcode::SRem) {
                sign_extend_register_for_type(machine_block, 9, binary.get_type());
                sign_extend_register_for_type(machine_block, 10, binary.get_type());
            } else {
                truncate_register_to_type(machine_block, 9, binary.get_type());
                truncate_register_to_type(machine_block, 10, binary.get_type());
            }
        }
        const bool use_64bit = uses_64bit_register(binary.get_type());
        const std::string dst = general_register_name(9, use_64bit);
        const std::string rhs = general_register_name(10, use_64bit);
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
            machine_block.append_instruction("mov " +
                                             general_register_name(11, use_64bit) +
                                             ", " + dst);
            machine_block.append_instruction("sdiv " + dst + ", " + dst + ", " +
                                             rhs);
            machine_block.append_instruction("msub " + dst + ", " + dst + ", " +
                                             rhs + ", " +
                                             general_register_name(11, use_64bit));
            truncate_register_to_type(machine_block, 9, binary.get_type());
            append_store_to_frame(machine_block, binary.get_type(), dst,
                                  state.machine_function->get_frame_info()
                                      .get_value_offset(&binary));
            return true;
        case CoreIrBinaryOpcode::URem:
            machine_block.append_instruction("mov " +
                                             general_register_name(11, use_64bit) +
                                             ", " + dst);
            machine_block.append_instruction("udiv " + dst + ", " + dst + ", " +
                                             rhs);
            machine_block.append_instruction("msub " + dst + ", " + dst + ", " +
                                             rhs + ", " +
                                             general_register_name(11, use_64bit));
            truncate_register_to_type(machine_block, 9, binary.get_type());
            append_store_to_frame(machine_block, binary.get_type(), dst,
                                  state.machine_function->get_frame_info()
                                      .get_value_offset(&binary));
            return true;
        }
        machine_block.append_instruction(opcode + " " + dst + ", " + dst + ", " +
                                         rhs);
        truncate_register_to_type(machine_block, 9, binary.get_type());
        append_store_to_frame(machine_block, binary.get_type(), dst,
                              state.machine_function->get_frame_info().get_value_offset(
                                  &binary));
        return true;
    }

    bool emit_unary(AArch64MachineBlock &machine_block, const CoreIrUnaryInst &unary,
                    const FunctionState &state) {
        if (!materialize_value(machine_block, unary.get_operand(), 9, state)) {
            return false;
        }
        const bool use_64bit = uses_64bit_register(unary.get_type());
        const std::string dst = general_register_name(9, use_64bit);
        switch (unary.get_unary_opcode()) {
        case CoreIrUnaryOpcode::Negate:
            machine_block.append_instruction("neg " + dst + ", " + dst);
            break;
        case CoreIrUnaryOpcode::BitwiseNot:
            machine_block.append_instruction("mvn " + dst + ", " + dst);
            break;
        case CoreIrUnaryOpcode::LogicalNot:
            machine_block.append_instruction("cmp " + dst + ", #0");
            machine_block.append_instruction("cset w9, eq");
            break;
        }
        append_store_to_frame(machine_block, unary.get_type(), dst,
                              state.machine_function->get_frame_info().get_value_offset(
                                  &unary));
        return true;
    }

    bool emit_compare(AArch64MachineBlock &machine_block,
                      const CoreIrCompareInst &compare,
                      const FunctionState &state) {
        if (!materialize_value(machine_block, compare.get_lhs(), 9, state) ||
            !materialize_value(machine_block, compare.get_rhs(), 10, state)) {
            return false;
        }
        switch (compare.get_predicate()) {
        case CoreIrComparePredicate::SignedLess:
        case CoreIrComparePredicate::SignedLessEqual:
        case CoreIrComparePredicate::SignedGreater:
        case CoreIrComparePredicate::SignedGreaterEqual:
            sign_extend_register_for_type(machine_block, 9,
                                          compare.get_lhs()->get_type());
            sign_extend_register_for_type(machine_block, 10,
                                          compare.get_rhs()->get_type());
            break;
        default:
            truncate_register_to_type(machine_block, 9, compare.get_lhs()->get_type());
            truncate_register_to_type(machine_block, 10, compare.get_rhs()->get_type());
            break;
        }
        const bool use_64bit = uses_64bit_register(compare.get_lhs()->get_type());
        machine_block.append_instruction(
            "cmp " + general_register_name(9, use_64bit) + ", " +
            general_register_name(10, use_64bit));
        machine_block.append_instruction("cset w9, " +
                                         condition_code(compare.get_predicate()));
        append_store_to_frame(
            machine_block, compare.get_type(), "w9",
            state.machine_function->get_frame_info().get_value_offset(&compare));
        return true;
    }

    bool emit_cast(AArch64MachineBlock &machine_block, const CoreIrCastInst &cast,
                   const FunctionState &state) {
        if (!materialize_value(machine_block, cast.get_operand(), 9, state)) {
            return false;
        }
        switch (cast.get_cast_kind()) {
        case CoreIrCastKind::Truncate:
            truncate_register_to_type(machine_block, 9, cast.get_type());
            break;
        case CoreIrCastKind::ZeroExtend:
            truncate_register_to_type(machine_block, 9,
                                      cast.get_operand()->get_type());
            break;
        case CoreIrCastKind::SignExtend:
            sign_extend_register_for_type(machine_block, 9,
                                          cast.get_operand()->get_type());
            if (uses_64bit_register(cast.get_type()) &&
                !uses_64bit_register(cast.get_operand()->get_type())) {
                machine_block.append_instruction("sxtw x9, w9");
            }
            break;
        case CoreIrCastKind::PtrToInt:
            truncate_register_to_type(machine_block, 9, cast.get_type());
        case CoreIrCastKind::IntToPtr:
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
        append_store_to_frame(
            machine_block, cast.get_type(),
            general_register_name(9, uses_64bit_register(cast.get_type())),
            state.machine_function->get_frame_info().get_value_offset(&cast));
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
                if (!materialize_value(machine_block, argument,
                                       static_cast<unsigned>(argument_index),
                                       state)) {
                    return false;
                }
                truncate_register_to_type(
                    machine_block, static_cast<unsigned>(argument_index),
                    argument->get_type());
                continue;
            }
            if (!materialize_value(machine_block, argument, 9, state)) {
                return false;
            }
            truncate_register_to_type(machine_block, 9, argument->get_type());
            const std::size_t stack_slot_offset = (argument_index - 8) * 8;
            machine_block.append_instruction(
                store_mnemonic_for_type(argument->get_type()) + " " +
                general_register_name(9, uses_64bit_register(argument->get_type())) +
                ", [sp, #" + std::to_string(stack_slot_offset) + "]");
        }
        if (!call.get_is_direct_call()) {
            if (!materialize_value(machine_block, call.get_callee_value(), 16, state)) {
                return false;
            }
            machine_block.append_instruction("blr x16");
        } else {
            machine_block.append_instruction("bl " + call.get_callee_name());
        }
        if (stack_arg_bytes > 0) {
            machine_block.append_instruction(
                "add sp, sp, #" + std::to_string(stack_arg_bytes));
        }
        if (!is_void_type(call.get_type())) {
            truncate_register_to_type(machine_block, 0, call.get_type());
            append_store_to_frame(
                machine_block, call.get_type(),
                general_register_name(0, uses_64bit_register(call.get_type())),
                state.machine_function->get_frame_info().get_value_offset(&call));
        }
        return true;
    }

    bool emit_cond_jump(AArch64MachineBlock &machine_block,
                        const CoreIrCondJumpInst &cond_jump,
                        const FunctionState &state) {
        if (!materialize_value(machine_block, cond_jump.get_condition(), 9, state)) {
            return false;
        }
        machine_block.append_instruction(
            "cbnz " +
            general_register_name(9,
                                  uses_64bit_register(cond_jump.get_condition()->get_type())) +
            ", " + block_labels_.at(cond_jump.get_true_block()));
        machine_block.append_instruction(
            "b " + block_labels_.at(cond_jump.get_false_block()));
        return true;
    }

    bool emit_return(AArch64MachineFunction &machine_function,
                     AArch64MachineBlock &machine_block,
                     const CoreIrReturnInst &return_inst,
                     const FunctionState &state) {
        if (return_inst.get_return_value() != nullptr) {
            if (!materialize_value(machine_block, return_inst.get_return_value(), 9,
                                   state)) {
                return false;
            }
            truncate_register_to_type(machine_block, 9,
                                      return_inst.get_return_value()->get_type());
            const bool use_64bit =
                uses_64bit_register(return_inst.get_return_value()->get_type());
            machine_block.append_instruction("mov " + general_register_name(0, use_64bit) +
                                             ", " + general_register_name(9, use_64bit));
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
}

AArch64MachineInstr::AArch64MachineInstr(
    std::string mnemonic, std::vector<AArch64MachineOperand> operands)
    : mnemonic_(std::move(mnemonic)), operands_(std::move(operands)) {}

std::size_t
AArch64FunctionFrameInfo::get_stack_slot_offset(
    const CoreIrStackSlot *stack_slot) const {
    return stack_slot_offsets_.at(stack_slot);
}

std::size_t
AArch64FunctionFrameInfo::get_value_offset(const CoreIrValue *value) const {
    return value_offsets_.at(value);
}

bool AArch64FunctionFrameInfo::has_value_offset(const CoreIrValue *value) const {
    return value_offsets_.find(value) != value_offsets_.end();
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
