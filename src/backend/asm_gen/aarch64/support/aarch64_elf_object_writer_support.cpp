#include "backend/asm_gen/aarch64/support/aarch64_elf_object_writer_support.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "backend/asm_gen/aarch64/support/aarch64_text_support.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

constexpr unsigned char kElfClass64 = 2;
constexpr unsigned char kElfDataLittleEndian = 1;
constexpr unsigned char kElfVersionCurrent = 1;
constexpr std::uint16_t kElfTypeRelocatable = 1;
constexpr std::uint16_t kElfMachineAArch64 = 183;
constexpr std::uint32_t kElfSectionNull = 0;
constexpr std::uint32_t kElfSectionProgBits = 1;
constexpr std::uint32_t kElfSectionSymTab = 2;
constexpr std::uint32_t kElfSectionStrTab = 3;
constexpr std::uint32_t kElfSectionRela = 4;
constexpr std::uint32_t kElfSectionNoBits = 8;
constexpr std::uint64_t kElfSectionFlagWrite = 0x1;
constexpr std::uint64_t kElfSectionFlagAlloc = 0x2;
constexpr std::uint64_t kElfSectionFlagExecInstr = 0x4;
constexpr std::uint16_t kElfSymbolUndefined = 0;
constexpr std::uint8_t kElfSymbolBindingLocal = 0;
constexpr std::uint8_t kElfSymbolBindingGlobal = 1;
constexpr std::uint8_t kElfSymbolTypeNoType = 0;
constexpr std::uint8_t kElfSymbolTypeObject = 1;
constexpr std::uint8_t kElfSymbolTypeFunction = 2;
constexpr std::uint32_t kAArch64RelocAbs64 = 257;
constexpr std::uint32_t kAArch64RelocAbs32 = 258;
constexpr std::uint32_t kAArch64RelocPrel32 = 261;
constexpr std::uint32_t kAArch64RelocAdrPrelPgHi21 = 275;
constexpr std::uint32_t kAArch64RelocAddAbsLo12Nc = 277;
constexpr std::uint32_t kAArch64RelocJump26 = 282;
constexpr std::uint32_t kAArch64RelocCall26 = 283;
constexpr std::uint32_t kAArch64RelocAdrGotPage = 311;
constexpr std::uint32_t kAArch64RelocLd64GotLo12Nc = 312;

struct Elf64Header {
    std::array<unsigned char, 16> ident{};
    std::uint16_t type = 0;
    std::uint16_t machine = 0;
    std::uint32_t version = 0;
    std::uint64_t entry = 0;
    std::uint64_t program_header_offset = 0;
    std::uint64_t section_header_offset = 0;
    std::uint32_t flags = 0;
    std::uint16_t header_size = 0;
    std::uint16_t program_header_entry_size = 0;
    std::uint16_t program_header_count = 0;
    std::uint16_t section_header_entry_size = 0;
    std::uint16_t section_header_count = 0;
    std::uint16_t section_name_string_table_index = 0;
};

struct Elf64SectionHeader {
    std::uint32_t name = 0;
    std::uint32_t type = 0;
    std::uint64_t flags = 0;
    std::uint64_t address = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::uint32_t link = 0;
    std::uint32_t info = 0;
    std::uint64_t address_align = 0;
    std::uint64_t entry_size = 0;
};

struct Elf64Symbol {
    std::uint32_t name = 0;
    unsigned char info = 0;
    unsigned char other = 0;
    std::uint16_t section_index = 0;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
};

struct Elf64Rela {
    std::uint64_t offset = 0;
    std::uint64_t info = 0;
    std::int64_t addend = 0;
};

static_assert(sizeof(Elf64Header) == 64);
static_assert(sizeof(Elf64SectionHeader) == 64);
static_assert(sizeof(Elf64Symbol) == 24);
static_assert(sizeof(Elf64Rela) == 24);

struct PendingRelocation {
    std::size_t offset = 0;
    AArch64RelocationRecord record;
};

struct SectionImage {
    AArch64SectionKind kind = AArch64SectionKind::Data;
    std::string name;
    std::uint32_t type = kElfSectionProgBits;
    std::uint64_t flags = 0;
    std::uint64_t align = 1;
    std::uint64_t entry_size = 0;
    std::vector<std::uint8_t> bytes;
    std::size_t nobits_size = 0;
    std::vector<PendingRelocation> relocations;
    std::uint32_t section_index = 0;
};

struct FinalSection {
    std::string name;
    std::uint32_t type = kElfSectionProgBits;
    std::uint64_t flags = 0;
    std::uint64_t align = 1;
    std::uint64_t entry_size = 0;
    std::vector<std::uint8_t> bytes;
    std::size_t logical_size = 0;
    std::uint32_t link = 0;
    std::uint32_t info = 0;
    std::uint64_t file_offset = 0;
};

struct SymbolEntry {
    std::string name;
    std::uint8_t binding = kElfSymbolBindingLocal;
    std::uint8_t type = kElfSymbolTypeObject;
    std::uint16_t section_index = kElfSymbolUndefined;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
    std::uint32_t name_offset = 0;
    std::uint32_t index = 0;
};

struct DefinedSymbol {
    AArch64SectionKind section_kind = AArch64SectionKind::Data;
    std::size_t offset = 0;
    std::size_t size = 0;
};

struct SourceLocationNote {
    std::size_t pc_offset = 0;
    unsigned file_id = 0;
    int line = 0;
    int column = 0;
};

enum class ParsedCfiNoteKind : unsigned char {
    DefCfa,
    DefCfaRegister,
    DefCfaOffset,
    Offset,
    Restore,
};

struct ParsedCfiNote {
    std::size_t pc_offset = 0;
    ParsedCfiNoteKind kind = ParsedCfiNoteKind::DefCfa;
    unsigned reg = 0;
    long long offset = 0;
};

struct FunctionScanInfo {
    std::unordered_map<std::string, std::size_t> label_offsets;
    std::vector<SourceLocationNote> source_locations;
    std::size_t code_size = 0;
};

struct EncodedGeneralReg {
    unsigned code = 0;
    bool use_64bit = false;
    bool is_stack_pointer = false;
    bool is_zero_register = false;
};

struct EncodedFloatReg {
    unsigned code = 0;
    AArch64VirtualRegKind kind = AArch64VirtualRegKind::Float64;
};

struct EncodedInstruction {
    std::uint32_t word = 0;
    std::vector<AArch64RelocationRecord> relocations;
};

void merge_symbol_descriptor(AArch64SymbolDescriptor &dst,
                             const AArch64SymbolDescriptor &src) {
    if (dst.kind == AArch64SymbolKind::Object && src.kind != AArch64SymbolKind::Object) {
        dst.kind = src.kind;
    }
    if (!dst.section_kind.has_value() && src.section_kind.has_value()) {
        dst.section_kind = src.section_kind;
    }
    if (dst.binding == AArch64SymbolBinding::Unknown &&
        src.binding != AArch64SymbolBinding::Unknown) {
        dst.binding = src.binding;
    }
    if (!dst.is_defined && src.is_defined) {
        dst.is_defined = true;
    }
}

template <typename T>
void append_pod(std::vector<std::uint8_t> &bytes, const T &value) {
    const auto *raw = reinterpret_cast<const std::uint8_t *>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(T));
}

std::size_t align_to(std::size_t value, std::size_t alignment) {
    if (alignment <= 1) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

void overwrite_u32(std::vector<std::uint8_t> &bytes, std::size_t offset,
                   std::uint32_t value) {
    if (offset + sizeof(value) > bytes.size()) {
        return;
    }
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void append_uleb128(std::vector<std::uint8_t> &bytes, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0) {
            byte |= 0x80U;
        }
        bytes.push_back(byte);
    } while (value != 0);
}

void append_sleb128(std::vector<std::uint8_t> &bytes, std::int64_t value) {
    bool more = true;
    while (more) {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7f);
        const bool sign_bit_set = (byte & 0x40U) != 0;
        value >>= 7;
        more = !((value == 0 && !sign_bit_set) || (value == -1 && sign_bit_set));
        if (more) {
            byte |= 0x80U;
        }
        bytes.push_back(byte);
    }
}

bool starts_with(const std::string &text, const char *prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::optional<long long> parse_signed_immediate_text(const std::string &text) {
    std::string trimmed;
    trimmed.reserve(text.size());
    for (char ch : text) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            trimmed.push_back(ch);
        }
    }
    if (trimmed.empty()) {
        return std::nullopt;
    }
    if (trimmed.front() == '#') {
        trimmed.erase(trimmed.begin());
    }
    if (trimmed.empty()) {
        return std::nullopt;
    }
    try {
        std::size_t parsed = 0;
        const long long value = std::stoll(trimmed, &parsed, 0);
        if (parsed != trimmed.size()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<unsigned> parse_unsigned_immediate_text(const std::string &text) {
    const auto value = parse_signed_immediate_text(text);
    if (!value.has_value() || *value < 0 ||
        *value > static_cast<long long>(std::numeric_limits<unsigned>::max())) {
        return std::nullopt;
    }
    return static_cast<unsigned>(*value);
}

const char *section_name_for_object(AArch64SectionKind kind) {
    switch (kind) {
    case AArch64SectionKind::Text:
        return ".text";
    case AArch64SectionKind::Data:
        return ".data";
    case AArch64SectionKind::ReadOnlyData:
        return ".rodata";
    case AArch64SectionKind::Bss:
        return ".bss";
    case AArch64SectionKind::EhFrame:
        return ".eh_frame";
    case AArch64SectionKind::DebugLine:
        return ".debug_line";
    default:
        return nullptr;
    }
}

std::uint64_t section_flags_for_object(AArch64SectionKind kind) {
    switch (kind) {
    case AArch64SectionKind::Text:
        return kElfSectionFlagAlloc | kElfSectionFlagExecInstr;
    case AArch64SectionKind::Data:
    case AArch64SectionKind::Bss:
        return kElfSectionFlagAlloc | kElfSectionFlagWrite;
    case AArch64SectionKind::ReadOnlyData:
    case AArch64SectionKind::EhFrame:
        return kElfSectionFlagAlloc;
    case AArch64SectionKind::DebugLine:
    case AArch64SectionKind::DebugFrame:
        return 0;
    default:
        return 0;
    }
}

std::uint32_t relocation_type_for_record(AArch64RelocationKind kind) {
    switch (kind) {
    case AArch64RelocationKind::Absolute32:
        return kAArch64RelocAbs32;
    case AArch64RelocationKind::Absolute64:
        return kAArch64RelocAbs64;
    case AArch64RelocationKind::Prel32:
        return kAArch64RelocPrel32;
    case AArch64RelocationKind::Page21:
        return kAArch64RelocAdrPrelPgHi21;
    case AArch64RelocationKind::PageOffset12:
        return kAArch64RelocAddAbsLo12Nc;
    case AArch64RelocationKind::Branch26:
        return kAArch64RelocJump26;
    case AArch64RelocationKind::Call26:
        return kAArch64RelocCall26;
    case AArch64RelocationKind::GotPage21:
        return kAArch64RelocAdrGotPage;
    case AArch64RelocationKind::GotLo12:
        return kAArch64RelocLd64GotLo12Nc;
    default:
        return 0;
    }
}

std::uint8_t elf_symbol_type(AArch64SymbolKind kind) {
    switch (kind) {
    case AArch64SymbolKind::Function:
    case AArch64SymbolKind::Helper:
        return kElfSymbolTypeFunction;
    case AArch64SymbolKind::Object:
    default:
        return kElfSymbolTypeObject;
    }
}

bool is_real_text_instruction(const AArch64MachineInstr &instruction) {
    return instruction.get_mnemonic().empty() || instruction.get_mnemonic().front() != '.';
}

bool is_loc_instruction(const AArch64MachineInstr &instruction) {
    return instruction.get_mnemonic() == ".loc";
}

bool is_cfi_instruction(const AArch64MachineInstr &instruction) {
    return starts_with(instruction.get_mnemonic(), ".cfi_");
}

std::optional<AArch64MachineSymbolReference>
get_symbol_reference_operand(const AArch64MachineOperand &operand) {
    const auto *symbol = operand.get_symbol_operand();
    if (symbol == nullptr) {
        return std::nullopt;
    }
    return symbol->reference;
}

std::optional<EncodedGeneralReg>
resolve_general_reg_operand(const AArch64MachineOperand &operand,
                            const AArch64MachineFunction &function,
                            bool allow_stack_pointer, bool allow_zero_register,
                            DiagnosticEngine &diagnostic_engine,
                            const std::string &context) {
    if (const auto *stack_pointer = operand.get_stack_pointer_operand();
        stack_pointer != nullptr) {
        if (!allow_stack_pointer) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unexpected stack pointer in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{31, stack_pointer->use_64bit, true, false};
    }
    if (const auto *zero_register = operand.get_zero_register_operand();
        zero_register != nullptr) {
        if (!allow_zero_register) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unexpected zero register in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{31, zero_register->use_64bit, false, true};
    }
    if (const auto *physical_reg = operand.get_physical_reg_operand();
        physical_reg != nullptr) {
        if (is_float_physical_reg(physical_reg->reg_number)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unexpected floating-point register in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{physical_reg->reg_number,
                                 uses_general_64bit_register(physical_reg->kind),
                                 false, false};
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        if (!virtual_reg->reg.is_general()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unexpected floating-point virtual register in " +
                    context);
            return std::nullopt;
        }
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(virtual_reg->reg.get_id());
        if (!physical_reg.has_value()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unresolved virtual register in " +
                    context);
            return std::nullopt;
        }
        if (is_float_physical_reg(*physical_reg)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: virtual register resolved to floating-point register in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{*physical_reg, virtual_reg->reg.get_use_64bit(),
                                 false, false};
    }

    diagnostic_engine.add_error(
        DiagnosticStage::Compiler,
        "AArch64 direct object writer: expected general register operand in " +
            context);
    return std::nullopt;
}

std::optional<EncodedFloatReg>
resolve_float_reg_operand(const AArch64MachineOperand &operand,
                          const AArch64MachineFunction &function,
                          DiagnosticEngine &diagnostic_engine,
                          const std::string &context) {
    if (const auto *physical_reg = operand.get_physical_reg_operand();
        physical_reg != nullptr) {
        if (!is_float_physical_reg(physical_reg->reg_number)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: expected floating-point register in " +
                    context);
            return std::nullopt;
        }
        return EncodedFloatReg{
            physical_reg->reg_number -
                static_cast<unsigned>(AArch64PhysicalReg::V0),
            physical_reg->kind};
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        if (!virtual_reg->reg.is_floating_point()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: expected floating-point virtual register in " +
                    context);
            return std::nullopt;
        }
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(virtual_reg->reg.get_id());
        if (!physical_reg.has_value()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unresolved floating-point virtual register in " +
                    context);
            return std::nullopt;
        }
        if (!is_float_physical_reg(*physical_reg)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: floating-point virtual register resolved to general register in " +
                    context);
            return std::nullopt;
        }
        return EncodedFloatReg{*physical_reg -
                                   static_cast<unsigned>(AArch64PhysicalReg::V0),
                               virtual_reg->reg.get_kind()};
    }

    diagnostic_engine.add_error(
        DiagnosticStage::Compiler,
        "AArch64 direct object writer: expected floating-point register operand in " +
            context);
    return std::nullopt;
}

bool is_supported_scalar_fp_kind(AArch64VirtualRegKind kind) {
    return kind == AArch64VirtualRegKind::Float16 ||
           kind == AArch64VirtualRegKind::Float32 ||
           kind == AArch64VirtualRegKind::Float64;
}

std::size_t scalar_fp_size(AArch64VirtualRegKind kind) {
    switch (kind) {
    case AArch64VirtualRegKind::Float16:
        return 2;
    case AArch64VirtualRegKind::Float32:
        return 4;
    case AArch64VirtualRegKind::Float64:
        return 8;
    default:
        return 0;
    }
}

std::optional<std::uint32_t> fp_reg_move_base(AArch64VirtualRegKind kind) {
    switch (kind) {
    case AArch64VirtualRegKind::Float16:
        return 0x1EE04000U;
    case AArch64VirtualRegKind::Float32:
        return 0x1E204000U;
    case AArch64VirtualRegKind::Float64:
        return 0x1E604000U;
    default:
        return std::nullopt;
    }
}

std::optional<std::uint32_t>
fp_gp_move_base(AArch64VirtualRegKind fp_kind, bool gp_is_64bit, bool gp_to_fp) {
    if (gp_to_fp) {
        if (fp_kind == AArch64VirtualRegKind::Float32 && !gp_is_64bit) {
            return 0x1E270000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64 && gp_is_64bit) {
            return 0x9E670000U;
        }
        return std::nullopt;
    }
    if (fp_kind == AArch64VirtualRegKind::Float32 && !gp_is_64bit) {
        return 0x1E260000U;
    }
    if (fp_kind == AArch64VirtualRegKind::Float64 && gp_is_64bit) {
        return 0x9E660000U;
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
fp_binary_base(const std::string &mnemonic, AArch64VirtualRegKind kind) {
    if (kind == AArch64VirtualRegKind::Float16) {
        if (mnemonic == "fadd")
            return 0x1EE02800U;
        if (mnemonic == "fsub")
            return 0x1EE03800U;
        if (mnemonic == "fmul")
            return 0x1EE00800U;
        if (mnemonic == "fdiv")
            return 0x1EE01800U;
    }
    if (kind == AArch64VirtualRegKind::Float32) {
        if (mnemonic == "fadd")
            return 0x1E202800U;
        if (mnemonic == "fsub")
            return 0x1E203800U;
        if (mnemonic == "fmul")
            return 0x1E200800U;
        if (mnemonic == "fdiv")
            return 0x1E201800U;
    }
    if (kind == AArch64VirtualRegKind::Float64) {
        if (mnemonic == "fadd")
            return 0x1E602800U;
        if (mnemonic == "fsub")
            return 0x1E603800U;
        if (mnemonic == "fmul")
            return 0x1E600800U;
        if (mnemonic == "fdiv")
            return 0x1E601800U;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> fcmp_base(AArch64VirtualRegKind kind) {
    switch (kind) {
    case AArch64VirtualRegKind::Float16:
        return 0x1EE02000U;
    case AArch64VirtualRegKind::Float32:
        return 0x1E202000U;
    case AArch64VirtualRegKind::Float64:
        return 0x1E602000U;
    default:
        return std::nullopt;
    }
}

std::optional<std::uint32_t>
int_to_fp_base(const std::string &mnemonic, AArch64VirtualRegKind fp_kind,
               bool src_is_64bit) {
    if (mnemonic == "scvtf") {
        if (fp_kind == AArch64VirtualRegKind::Float16) {
            return src_is_64bit ? 0x9EE20000U : 0x1EE20000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float32) {
            return src_is_64bit ? 0x9E220000U : 0x1E220000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64) {
            return src_is_64bit ? 0x9E620000U : 0x1E620000U;
        }
    } else if (mnemonic == "ucvtf") {
        if (fp_kind == AArch64VirtualRegKind::Float16) {
            return src_is_64bit ? 0x9EE30000U : 0x1EE30000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float32) {
            return src_is_64bit ? 0x9E230000U : 0x1E230000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64) {
            return src_is_64bit ? 0x9E630000U : 0x1E630000U;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
fp_to_int_base(const std::string &mnemonic, AArch64VirtualRegKind fp_kind,
               bool dst_is_64bit) {
    if (mnemonic == "fcvtzs") {
        if (fp_kind == AArch64VirtualRegKind::Float16) {
            return dst_is_64bit ? 0x9EF80000U : 0x1EF80000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float32) {
            return dst_is_64bit ? 0x9E380000U : 0x1E380000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64) {
            return dst_is_64bit ? 0x9E780000U : 0x1E780000U;
        }
    } else if (mnemonic == "fcvtzu") {
        if (fp_kind == AArch64VirtualRegKind::Float16) {
            return dst_is_64bit ? 0x9EF90000U : 0x1EF90000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float32) {
            return dst_is_64bit ? 0x9E390000U : 0x1E390000U;
        }
        if (fp_kind == AArch64VirtualRegKind::Float64) {
            return dst_is_64bit ? 0x9E790000U : 0x1E790000U;
        }
    }
    return std::nullopt;
}

std::optional<std::uint32_t>
fp_convert_base(AArch64VirtualRegKind dst_kind, AArch64VirtualRegKind src_kind) {
    if (dst_kind == AArch64VirtualRegKind::Float16 &&
        src_kind == AArch64VirtualRegKind::Float32) {
        return 0x1E23C000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float32 &&
        src_kind == AArch64VirtualRegKind::Float16) {
        return 0x1EE24000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float16 &&
        src_kind == AArch64VirtualRegKind::Float64) {
        return 0x1E63C000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float64 &&
        src_kind == AArch64VirtualRegKind::Float16) {
        return 0x1EE2C000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float64 &&
        src_kind == AArch64VirtualRegKind::Float32) {
        return 0x1E22C000U;
    }
    if (dst_kind == AArch64VirtualRegKind::Float32 &&
        src_kind == AArch64VirtualRegKind::Float64) {
        return 0x1E624000U;
    }
    return std::nullopt;
}

bool encode_fp_reg_reg(std::uint32_t base, unsigned rd, unsigned rn,
                       EncodedInstruction &encoded) {
    encoded.word = base | ((rn & 0x1fU) << 5) | (rd & 0x1fU);
    return true;
}

bool operand_is_float_reg_like(const AArch64MachineOperand &operand) {
    if (const auto *physical_reg = operand.get_physical_reg_operand();
        physical_reg != nullptr) {
        return is_float_physical_reg(physical_reg->reg_number);
    }
    if (const auto *virtual_reg = operand.get_virtual_reg_operand();
        virtual_reg != nullptr) {
        return virtual_reg->reg.is_floating_point();
    }
    return false;
}

std::optional<EncodedGeneralReg>
resolve_memory_base_reg(const AArch64MachineMemoryAddressOperand &memory,
                        const AArch64MachineFunction &function,
                        DiagnosticEngine &diagnostic_engine,
                        const std::string &context) {
    switch (memory.base_kind) {
    case AArch64MachineMemoryAddressOperand::BaseKind::StackPointer:
        return EncodedGeneralReg{31, true, true, false};
    case AArch64MachineMemoryAddressOperand::BaseKind::PhysicalReg:
        if (is_float_physical_reg(memory.physical_reg)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: floating-point register cannot be used as memory base in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{memory.physical_reg, true, false, false};
    case AArch64MachineMemoryAddressOperand::BaseKind::VirtualReg: {
        const std::optional<unsigned> physical_reg =
            function.get_physical_reg_for_virtual(memory.virtual_reg.get_id());
        if (!physical_reg.has_value()) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: unresolved memory base register in " +
                    context);
            return std::nullopt;
        }
        if (is_float_physical_reg(*physical_reg)) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 direct object writer: floating-point register cannot be used as memory base in " +
                    context);
            return std::nullopt;
        }
        return EncodedGeneralReg{*physical_reg, true, false, false};
    }
    }
    return std::nullopt;
}

unsigned encode_condition_code(const std::string &code) {
    if (code == "eq")
        return 0;
    if (code == "ne")
        return 1;
    if (code == "cs" || code == "hs")
        return 2;
    if (code == "cc" || code == "lo")
        return 3;
    if (code == "mi")
        return 4;
    if (code == "pl")
        return 5;
    if (code == "vs")
        return 6;
    if (code == "vc")
        return 7;
    if (code == "hi")
        return 8;
    if (code == "ls")
        return 9;
    if (code == "ge")
        return 10;
    if (code == "lt")
        return 11;
    if (code == "gt")
        return 12;
    if (code == "le")
        return 13;
    if (code == "al")
        return 14;
    return 0xffU;
}

unsigned invert_condition_code(unsigned code) {
    return code ^ 1U;
}

std::optional<long long>
parse_operand_immediate(const AArch64MachineOperand &operand) {
    const auto *immediate = operand.get_immediate_operand();
    if (immediate == nullptr) {
        return std::nullopt;
    }
    return parse_signed_immediate_text(immediate->asm_text);
}

std::optional<unsigned> parse_shift_amount(const AArch64MachineOperand &operand,
                                           std::string &mnemonic) {
    const auto *shift = operand.get_shift_operand();
    if (shift == nullptr) {
        return std::nullopt;
    }
    mnemonic = shift->mnemonic;
    return shift->amount;
}

unsigned shift_type_bits(const std::string &mnemonic) {
    if (mnemonic == "lsl")
        return 0;
    if (mnemonic == "lsr")
        return 1;
    if (mnemonic == "asr")
        return 2;
    return 0xffU;
}

std::optional<unsigned>
parse_cfi_register_operand(const AArch64MachineOperand &operand) {
    if (const auto *stack_pointer = operand.get_stack_pointer_operand();
        stack_pointer != nullptr) {
        return 31U;
    }
    return operand.get_immediate_operand() != nullptr
               ? parse_unsigned_immediate_text(
                     operand.get_immediate_operand()->asm_text)
               : std::nullopt;
}

FunctionScanInfo scan_function_layout(const AArch64MachineFunction &function,
                                      DiagnosticEngine &diagnostic_engine) {
    (void)diagnostic_engine;
    FunctionScanInfo info;
    std::size_t current_pc = 0;
    for (const AArch64MachineBlock &block : function.get_blocks()) {
        info.label_offsets[block.get_label()] = current_pc;
        for (const AArch64MachineInstr &instruction : block.get_instructions()) {
            if (is_real_text_instruction(instruction)) {
                if (instruction.get_debug_location().has_value()) {
                    info.source_locations.push_back(SourceLocationNote{
                        current_pc, instruction.get_debug_location()->file_id,
                        instruction.get_debug_location()->line,
                        instruction.get_debug_location()->column});
                }
                current_pc += 4;
            }
        }
    }
    info.code_size = current_pc;
    return info;
}

bool check_signed_range(long long value, long long min_value, long long max_value,
                        DiagnosticEngine &diagnostic_engine,
                        const std::string &message) {
    if (value < min_value || value > max_value) {
        diagnostic_engine.add_error(DiagnosticStage::Compiler, message);
        return false;
    }
    return true;
}

std::optional<long long> resolve_branch_delta(const std::string &label,
                                              std::size_t pc_offset,
                                              const FunctionScanInfo &scan_info,
                                              DiagnosticEngine &diagnostic_engine,
                                              const std::string &context) {
    const auto it = scan_info.label_offsets.find(label);
    if (it == scan_info.label_offsets.end()) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 direct object writer: unknown branch label '" + label +
                "' in " + context);
        return std::nullopt;
    }
    return static_cast<long long>(it->second) - static_cast<long long>(pc_offset);
}

std::uint32_t encode_add_sub_immediate_word(bool is_sub, bool use_64bit, unsigned rd,
                                            unsigned rn, unsigned imm12,
                                            unsigned shift12) {
    const std::uint32_t base =
        use_64bit ? (is_sub ? 0xD1000000U : 0x91000000U)
                  : (is_sub ? 0x51000000U : 0x11000000U);
    return base | ((shift12 & 0x1U) << 22) | ((imm12 & 0xfffU) << 10) |
           ((rn & 0x1fU) << 5) | (rd & 0x1fU);
}

std::uint32_t encode_add_sub_register_word(bool is_sub, bool use_64bit, unsigned rd,
                                           unsigned rn, unsigned rm,
                                           unsigned shift_type, unsigned amount) {
    const std::uint32_t base =
        use_64bit ? (is_sub ? 0xCB000000U : 0x8B000000U)
                  : (is_sub ? 0x4B000000U : 0x0B000000U);
    return base | ((rm & 0x1fU) << 16) | ((shift_type & 0x3U) << 22) |
           ((amount & 0x3fU) << 10) | ((rn & 0x1fU) << 5) | (rd & 0x1fU);
}

std::uint32_t encode_logical_register_word(std::uint32_t base, unsigned rd,
                                           unsigned rn, unsigned rm,
                                           unsigned shift_type, unsigned amount) {
    return base | ((rm & 0x1fU) << 16) | ((shift_type & 0x3U) << 22) |
           ((amount & 0x3fU) << 10) | ((rn & 0x1fU) << 5) | (rd & 0x1fU);
}

std::uint32_t encode_unconditional_branch_word(std::uint32_t base, long long delta) {
    const std::uint32_t imm26 =
        static_cast<std::uint32_t>((delta >> 2) & 0x03ffffffU);
    return base | imm26;
}

std::uint32_t encode_conditional_branch_word(unsigned condition_bits,
                                             long long delta) {
    const std::uint32_t imm19 =
        static_cast<std::uint32_t>((delta >> 2) & 0x7ffffU);
    return 0x54000000U | (imm19 << 5) | (condition_bits & 0xfU);
}

std::uint32_t encode_wide_move_word(std::uint32_t base, unsigned rd,
                                    unsigned imm16, unsigned hw) {
    return base | ((hw & 0x3U) << 21) | ((imm16 & 0xffffU) << 5) |
           (rd & 0x1fU);
}

std::uint32_t encode_load_store_unsigned_word(std::uint32_t base, unsigned rt,
                                              unsigned rn, unsigned imm12) {
    return base | ((imm12 & 0xfffU) << 10) | ((rn & 0x1fU) << 5) |
           (rt & 0x1fU);
}

std::uint32_t encode_load_store_unscaled_word(std::uint32_t base, unsigned rt,
                                              unsigned rn, long long imm9) {
    return base |
           ((static_cast<std::uint32_t>(imm9) & 0x1ffU) << 12) |
           ((rn & 0x1fU) << 5) | (rt & 0x1fU);
}

std::uint32_t encode_pair_word(std::uint32_t base, unsigned rt, unsigned rt2,
                               unsigned rn, long long scaled_imm7) {
    return base |
           ((static_cast<std::uint32_t>(scaled_imm7) & 0x7fU) << 15) |
           ((rt2 & 0x1fU) << 10) | ((rn & 0x1fU) << 5) | (rt & 0x1fU);
}

std::optional<EncodedInstruction> encode_machine_instruction(
    const AArch64MachineInstr &instruction, const AArch64MachineFunction &function,
    const FunctionScanInfo &scan_info, std::size_t pc_offset,
    DiagnosticEngine &diagnostic_engine) {
    EncodedInstruction encoded;
    const auto unsupported = [&](const std::string &detail)
        -> std::optional<EncodedInstruction> {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 direct object writer: unsupported instruction '" +
                instruction.get_mnemonic() + "' (" + detail + ")");
        return std::nullopt;
    };

    const std::string &mnemonic = instruction.get_mnemonic();
    const auto &operands = instruction.get_operands();

    if (mnemonic == "ret") {
        unsigned reg = 30;
        if (!operands.empty()) {
            const auto rn = resolve_general_reg_operand(
                operands[0], function, false, false, diagnostic_engine, "ret");
            if (!rn.has_value()) {
                return std::nullopt;
            }
            reg = rn->code;
        }
        encoded.word = 0xD65F0000U | ((reg & 0x1fU) << 5);
        return encoded;
    }

    if (mnemonic == "b" || mnemonic == "bl") {
        if (operands.size() != 1) {
            return unsupported("branch operand shape");
        }
        const std::uint32_t base = mnemonic == "bl" ? 0x94000000U : 0x14000000U;
        if (const auto *label = operands[0].get_label_operand(); label != nullptr) {
            const auto delta = resolve_branch_delta(label->label_text, pc_offset,
                                                    scan_info, diagnostic_engine,
                                                    mnemonic);
            if (!delta.has_value() || (*delta % 4) != 0 ||
                !check_signed_range(*delta >> 2, -(1 << 25), (1 << 25) - 1,
                                    diagnostic_engine,
                                    "AArch64 direct object writer: branch target out of range")) {
                return std::nullopt;
            }
            encoded.word = encode_unconditional_branch_word(base, *delta);
            return encoded;
        }
        const auto symbol = get_symbol_reference_operand(operands[0]);
        if (!symbol.has_value()) {
            return unsupported("branch target operand");
        }
        encoded.word = base;
        encoded.relocations.push_back(AArch64RelocationRecord{
            mnemonic == "bl" ? AArch64RelocationKind::Call26
                             : AArch64RelocationKind::Branch26,
            symbol->target, pc_offset});
        return encoded;
    }

    if (starts_with(mnemonic, "b.")) {
        if (operands.size() != 1 || operands[0].get_label_operand() == nullptr) {
            return unsupported("conditional branch operand shape");
        }
        const unsigned cond = encode_condition_code(mnemonic.substr(2));
        if (cond == 0xffU) {
            return unsupported("conditional branch condition");
        }
        const auto delta = resolve_branch_delta(
            operands[0].get_label_operand()->label_text, pc_offset, scan_info,
            diagnostic_engine, mnemonic);
        if (!delta.has_value() || (*delta % 4) != 0 ||
            !check_signed_range(*delta >> 2, -(1 << 18), (1 << 18) - 1,
                                diagnostic_engine,
                                "AArch64 direct object writer: conditional branch target out of range")) {
            return std::nullopt;
        }
        encoded.word = encode_conditional_branch_word(cond, *delta);
        return encoded;
    }

    if (mnemonic == "movz" || mnemonic == "movk") {
        if (operands.size() < 2) {
            return unsupported("wide move operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, mnemonic);
        const auto imm = parse_operand_immediate(operands[1]);
        if (!rd.has_value() || !imm.has_value() ||
            !check_signed_range(*imm, 0, 0xffff, diagnostic_engine,
                                "AArch64 direct object writer: wide move immediate out of range")) {
            return std::nullopt;
        }
        unsigned hw = 0;
        if (operands.size() >= 3) {
            std::string shift_mnemonic;
            const auto amount = parse_shift_amount(operands[2], shift_mnemonic);
            if (!amount.has_value() || shift_mnemonic != "lsl" || (*amount % 16) != 0 ||
                *amount > 48) {
                return unsupported("wide move shift");
            }
            hw = *amount / 16;
        }
        encoded.word = encode_wide_move_word(
            rd->use_64bit ? (mnemonic == "movk" ? 0xF2800000U : 0xD2800000U)
                          : (mnemonic == "movk" ? 0x72800000U : 0x52800000U),
            rd->code, static_cast<unsigned>(*imm), hw);
        return encoded;
    }

    if (mnemonic == "mov") {
        if (operands.size() != 2) {
            return unsupported("mov operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, true, false, diagnostic_engine, "mov dst");
        if (!rd.has_value()) {
            return std::nullopt;
        }
        if (operands[1].get_stack_pointer_operand() != nullptr || rd->is_stack_pointer) {
            const auto rn = resolve_general_reg_operand(
                operands[1], function, true, false, diagnostic_engine, "mov src");
            if (!rn.has_value() || rd->use_64bit != rn->use_64bit) {
                return std::nullopt;
            }
            encoded.word = encode_add_sub_immediate_word(false, rd->use_64bit,
                                                         rd->code, rn->code, 0, 0);
            return encoded;
        }
        const auto rm = resolve_general_reg_operand(
            operands[1], function, false, true, diagnostic_engine, "mov src");
        if (!rm.has_value() || rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        encoded.word = encode_logical_register_word(
            rd->use_64bit ? 0xAA000000U : 0x2A000000U, rd->code, 31, rm->code, 0,
            0);
        return encoded;
    }

    if (mnemonic == "add" || mnemonic == "sub") {
        if (operands.size() < 3) {
            return unsupported("add/sub operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, true, false, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_general_reg_operand(
            operands[1], function, true, false, diagnostic_engine, mnemonic + " lhs");
        if (!rd.has_value() || !rn.has_value() || rd->use_64bit != rn->use_64bit) {
            return std::nullopt;
        }
        if (const auto imm = parse_operand_immediate(operands[2]); imm.has_value()) {
            unsigned shift12 = 0;
            long long imm_value = *imm;
            if ((imm_value & 0xfffLL) == 0 && imm_value <= 0xfff000LL && imm_value >= 0) {
                imm_value >>= 12;
                shift12 = 1;
            }
            if (!check_signed_range(imm_value, 0, 0xfff, diagnostic_engine,
                                    "AArch64 direct object writer: add/sub immediate out of range")) {
                return std::nullopt;
            }
            encoded.word = encode_add_sub_immediate_word(
                mnemonic == "sub", rd->use_64bit, rd->code, rn->code,
                static_cast<unsigned>(imm_value), shift12);
            return encoded;
        }
        if (const auto symbol = get_symbol_reference_operand(operands[2]);
            symbol.has_value()) {
            if (mnemonic != "add" ||
                symbol->modifier != AArch64MachineSymbolReference::Modifier::Lo12) {
                return unsupported("symbolic add/sub form");
            }
            encoded.word = encode_add_sub_immediate_word(false, rd->use_64bit,
                                                         rd->code, rn->code, 0, 0);
            encoded.relocations.push_back(AArch64RelocationRecord{
                AArch64RelocationKind::PageOffset12, symbol->target, pc_offset});
            return encoded;
        }
        const auto rm = resolve_general_reg_operand(
            operands[2], function, false, false, diagnostic_engine, mnemonic + " rhs");
        if (!rm.has_value() || rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        unsigned shift_type = 0;
        unsigned amount = 0;
        if (operands.size() >= 4) {
            std::string shift_mnemonic;
            const auto shift_amount = parse_shift_amount(operands[3], shift_mnemonic);
            if (!shift_amount.has_value()) {
                return unsupported("add/sub shift");
            }
            shift_type = shift_type_bits(shift_mnemonic);
            amount = *shift_amount;
        }
        encoded.word = encode_add_sub_register_word(
            mnemonic == "sub", rd->use_64bit, rd->code, rn->code, rm->code,
            shift_type, amount);
        return encoded;
    }

    if (mnemonic == "and" || mnemonic == "orr" || mnemonic == "eor") {
        if (operands.size() < 3) {
            return unsupported("logical operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_general_reg_operand(
            operands[1], function, false, true, diagnostic_engine, mnemonic + " lhs");
        const auto rm = resolve_general_reg_operand(
            operands[2], function, false, true, diagnostic_engine, mnemonic + " rhs");
        if (!rd.has_value() || !rn.has_value() || !rm.has_value() ||
            rd->use_64bit != rn->use_64bit || rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        unsigned shift_type = 0;
        unsigned amount = 0;
        if (operands.size() >= 4) {
            std::string shift_mnemonic;
            const auto shift_amount = parse_shift_amount(operands[3], shift_mnemonic);
            if (!shift_amount.has_value()) {
                return unsupported("logical shift");
            }
            shift_type = shift_type_bits(shift_mnemonic);
            amount = *shift_amount;
        }
        std::uint32_t base = rd->use_64bit ? 0x8A000000U : 0x0A000000U;
        if (mnemonic == "orr") {
            base = rd->use_64bit ? 0xAA000000U : 0x2A000000U;
        } else if (mnemonic == "eor") {
            base = rd->use_64bit ? 0xCA000000U : 0x4A000000U;
        }
        encoded.word =
            encode_logical_register_word(base, rd->code, rn->code, rm->code,
                                         shift_type, amount);
        return encoded;
    }

    if (mnemonic == "mul" || mnemonic == "sdiv" || mnemonic == "udiv" ||
        mnemonic == "lsl" || mnemonic == "lsr" || mnemonic == "asr") {
        if (operands.size() != 3) {
            return unsupported("arithmetic operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_general_reg_operand(
            operands[1], function, false, false, diagnostic_engine, mnemonic + " lhs");
        const auto rm = resolve_general_reg_operand(
            operands[2], function, false, false, diagnostic_engine, mnemonic + " rhs");
        if (!rd.has_value() || !rn.has_value() || !rm.has_value() ||
            rd->use_64bit != rn->use_64bit || rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        if (mnemonic == "mul") {
            encoded.word = (rd->use_64bit ? 0x9B007C00U : 0x1B007C00U) |
                           ((rm->code & 0x1fU) << 16) |
                           ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
            return encoded;
        }
        if (mnemonic == "sdiv") {
            encoded.word = (rd->use_64bit ? 0x9AC00C00U : 0x1AC00C00U) |
                           ((rm->code & 0x1fU) << 16) |
                           ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
            return encoded;
        }
        if (mnemonic == "udiv") {
            encoded.word = (rd->use_64bit ? 0x9AC00800U : 0x1AC00800U) |
                           ((rm->code & 0x1fU) << 16) |
                           ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
            return encoded;
        }
        const std::uint32_t base = rd->use_64bit
                                       ? (mnemonic == "lsl" ? 0x9AC02000U
                                                            : mnemonic == "lsr"
                                                                  ? 0x9AC02400U
                                                                  : 0x9AC02800U)
                                       : (mnemonic == "lsl" ? 0x1AC02000U
                                                            : mnemonic == "lsr"
                                                                  ? 0x1AC02400U
                                                                  : 0x1AC02800U);
        encoded.word = base | ((rm->code & 0x1fU) << 16) |
                       ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (mnemonic == "cmp") {
        if (operands.size() != 2) {
            return unsupported("cmp operand shape");
        }
        const auto rn = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "cmp lhs");
        if (!rn.has_value()) {
            return std::nullopt;
        }
        if (const auto imm = parse_operand_immediate(operands[1]); imm.has_value()) {
            if (!check_signed_range(*imm, 0, 0xfff, diagnostic_engine,
                                    "AArch64 direct object writer: cmp immediate out of range")) {
                return std::nullopt;
            }
            encoded.word = (rn->use_64bit ? 0xF100001FU : 0x7100001FU) |
                           ((static_cast<unsigned>(*imm) & 0xfffU) << 10) |
                           ((rn->code & 0x1fU) << 5);
            return encoded;
        }
        const auto rm = resolve_general_reg_operand(
            operands[1], function, false, false, diagnostic_engine, "cmp rhs");
        if (!rm.has_value() || rn->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        encoded.word = (rn->use_64bit ? 0xEB00001FU : 0x6B00001FU) |
                       ((rm->code & 0x1fU) << 16) |
                       ((rn->code & 0x1fU) << 5);
        return encoded;
    }

    if (mnemonic == "csel") {
        if (operands.size() != 4) {
            return unsupported("csel operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "csel dst");
        const auto rn = resolve_general_reg_operand(
            operands[1], function, false, false, diagnostic_engine, "csel lhs");
        const auto rm = resolve_general_reg_operand(
            operands[2], function, false, false, diagnostic_engine, "csel rhs");
        const auto *condition = operands[3].get_condition_code_operand();
        if (!rd.has_value() || !rn.has_value() || !rm.has_value() ||
            condition == nullptr || rd->use_64bit != rn->use_64bit ||
            rd->use_64bit != rm->use_64bit) {
            return std::nullopt;
        }
        const unsigned cond = encode_condition_code(condition->code);
        if (cond == 0xffU) {
            return unsupported("csel condition");
        }
        encoded.word = (rd->use_64bit ? 0x9A800000U : 0x1A800000U) |
                       ((rm->code & 0x1fU) << 16) | ((cond & 0xfU) << 12) |
                       ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (mnemonic == "cset") {
        if (operands.size() != 2) {
            return unsupported("cset operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "cset dst");
        const auto *condition = operands[1].get_condition_code_operand();
        if (!rd.has_value() || condition == nullptr) {
            return std::nullopt;
        }
        const unsigned cond = encode_condition_code(condition->code);
        if (cond == 0xffU) {
            return unsupported("cset condition");
        }
        encoded.word = (rd->use_64bit ? 0x9A800400U : 0x1A800400U) |
                       (31U << 16) | ((invert_condition_code(cond) & 0xfU) << 12) |
                       (31U << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (mnemonic == "adrp") {
        if (operands.size() != 2) {
            return unsupported("adrp operand shape");
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "adrp dst");
        const auto symbol = get_symbol_reference_operand(operands[1]);
        if (!rd.has_value() || !rd->use_64bit || !symbol.has_value()) {
            return std::nullopt;
        }
        encoded.word = 0x90000000U | (rd->code & 0x1fU);
        encoded.relocations.push_back(AArch64RelocationRecord{
            symbol->modifier == AArch64MachineSymbolReference::Modifier::Got
                ? AArch64RelocationKind::GotPage21
                : AArch64RelocationKind::Page21,
            symbol->target, pc_offset});
        return encoded;
    }

    if (mnemonic == "stp" || mnemonic == "ldp") {
        if (operands.size() != 3) {
            return unsupported("pair load/store operand shape");
        }
        const auto rt = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, mnemonic + " rt");
        const auto rt2 = resolve_general_reg_operand(
            operands[1], function, false, false, diagnostic_engine, mnemonic + " rt2");
        const auto *memory = operands[2].get_memory_address_operand();
        if (!rt.has_value() || !rt2.has_value() || !rt->use_64bit ||
            !rt2->use_64bit || memory == nullptr) {
            return std::nullopt;
        }
        if (memory->get_symbolic_offset() != nullptr) {
            return unsupported("symbolic pair load/store");
        }
        const auto base = resolve_memory_base_reg(*memory, function, diagnostic_engine,
                                                  mnemonic + " base");
        if (!base.has_value()) {
            return std::nullopt;
        }
        const long long offset = memory->get_immediate_offset().value_or(0);
        if ((offset % 8) != 0 ||
            !check_signed_range(offset / 8, -64, 63, diagnostic_engine,
                                "AArch64 direct object writer: pair load/store offset out of range")) {
            return std::nullopt;
        }
        std::uint32_t base_word = mnemonic == "stp" ? 0xA9000000U : 0xA9400000U;
        if (memory->address_mode ==
            AArch64MachineMemoryAddressOperand::AddressMode::PreIndex) {
            base_word = mnemonic == "stp" ? 0xA9800000U : 0xA9C00000U;
        } else if (memory->address_mode ==
                   AArch64MachineMemoryAddressOperand::AddressMode::PostIndex) {
            base_word = mnemonic == "stp" ? 0xA8800000U : 0xA8C00000U;
        }
        encoded.word = encode_pair_word(base_word, rt->code, rt2->code, base->code,
                                        offset / 8);
        return encoded;
    }

    if (mnemonic == "ldr" || mnemonic == "str" || mnemonic == "ldrb" ||
        mnemonic == "strb" || mnemonic == "ldrh" || mnemonic == "strh" ||
        mnemonic == "ldur" || mnemonic == "stur" || mnemonic == "ldurb" ||
        mnemonic == "sturb" || mnemonic == "ldurh" || mnemonic == "sturh") {
        if (operands.size() != 2) {
            return unsupported("load/store operand shape");
        }
        const bool is_load = mnemonic[0] == 'l';
        const AArch64MachineOperand &value_operand = operands[0];
        const auto *memory = operands[1].get_memory_address_operand();
        if (memory == nullptr) {
            return unsupported("load/store memory operand");
        }
        std::optional<EncodedGeneralReg> general_value;
        std::optional<EncodedFloatReg> float_value;
        bool is_float = false;
        if (operand_is_float_reg_like(value_operand)) {
            float_value = resolve_float_reg_operand(value_operand, function,
                                                   diagnostic_engine, mnemonic);
            is_float = float_value.has_value();
        } else {
            general_value = resolve_general_reg_operand(
                value_operand, function, false, false, diagnostic_engine,
                mnemonic + " value");
        }
        if (!is_float && !general_value.has_value()) {
            return std::nullopt;
        }
        unsigned rt = is_float ? float_value->code : general_value->code;
        const bool use_64bit = is_float ? true : general_value->use_64bit;
        unsigned access_size = 4;
        std::uint32_t unsigned_base = 0;
        std::uint32_t unscaled_base = 0;
        if (mnemonic.find("b") != std::string::npos &&
            mnemonic != "sub") {
            access_size = 1;
            unsigned_base = is_load ? 0x39400000U : 0x39000000U;
            unscaled_base = is_load ? 0x38400000U : 0x38000000U;
        } else if (mnemonic.find("h") != std::string::npos) {
            access_size = 2;
            unsigned_base = is_load ? 0x79400000U : 0x79000000U;
            unscaled_base = is_load ? 0x78400000U : 0x78000000U;
        } else if (is_float) {
            access_size = static_cast<unsigned>(scalar_fp_size(float_value->kind));
            if (float_value->kind == AArch64VirtualRegKind::Float16) {
                unsigned_base = is_load ? 0x7D400000U : 0x7D000000U;
                unscaled_base = is_load ? 0x7C400000U : 0x7C000000U;
            } else if (float_value->kind == AArch64VirtualRegKind::Float32) {
                unsigned_base = is_load ? 0xBD400000U : 0xBD000000U;
                unscaled_base = is_load ? 0xBC400000U : 0xBC000000U;
            } else if (float_value->kind == AArch64VirtualRegKind::Float64) {
                unsigned_base = is_load ? 0xFD400000U : 0xFD000000U;
                unscaled_base = is_load ? 0xFC400000U : 0xFC000000U;
            } else {
                return unsupported("floating load/store kind");
            }
        } else if (use_64bit) {
            access_size = 8;
            unsigned_base = is_load ? 0xF9400000U : 0xF9000000U;
            unscaled_base = is_load ? 0xF8400000U : 0xF8000000U;
        } else {
            access_size = 4;
            unsigned_base = is_load ? 0xB9400000U : 0xB9000000U;
            unscaled_base = is_load ? 0xB8400000U : 0xB8000000U;
        }
        const auto base = resolve_memory_base_reg(*memory, function, diagnostic_engine,
                                                  mnemonic + " base");
        if (!base.has_value()) {
            return std::nullopt;
        }
        const unsigned rn = base->code;
        if (const auto *symbolic = memory->get_symbolic_offset();
            symbolic != nullptr) {
            if (!is_load || !use_64bit ||
                symbolic->modifier != AArch64MachineSymbolReference::Modifier::GotLo12) {
                return unsupported("symbolic memory offset");
            }
            encoded.word = encode_load_store_unsigned_word(unsigned_base, rt, rn, 0);
            encoded.relocations.push_back(AArch64RelocationRecord{
                AArch64RelocationKind::GotLo12, symbolic->target, pc_offset});
            return encoded;
        }
        const long long offset = memory->get_immediate_offset().value_or(0);
        const bool force_unscaled = starts_with(mnemonic, "ldur") ||
                                    starts_with(mnemonic, "stur") ||
                                    offset < 0 ||
                                    (offset % static_cast<long long>(access_size)) != 0;
        if (force_unscaled) {
            if (!check_signed_range(offset, -256, 255, diagnostic_engine,
                                    "AArch64 direct object writer: unscaled load/store offset out of range")) {
                return std::nullopt;
            }
            encoded.word = encode_load_store_unscaled_word(unscaled_base, rt, rn, offset);
            return encoded;
        }
        const long long scaled = offset / static_cast<long long>(access_size);
        if (!check_signed_range(scaled, 0, 4095, diagnostic_engine,
                                "AArch64 direct object writer: unsigned load/store offset out of range")) {
            return std::nullopt;
        }
        encoded.word =
            encode_load_store_unsigned_word(unsigned_base, rt, rn,
                                            static_cast<unsigned>(scaled));
        return encoded;
    }

    if (mnemonic == "fmov") {
        if (operands.size() != 2) {
            return unsupported("fmov operand shape");
        }
        if (operand_is_float_reg_like(operands[0])) {
            const auto dst_float = resolve_float_reg_operand(
                operands[0], function, diagnostic_engine, "fmov dst");
            if (!dst_float.has_value()) {
                return std::nullopt;
            }
            if (operand_is_float_reg_like(operands[1])) {
                const auto src_float = resolve_float_reg_operand(
                    operands[1], function, diagnostic_engine, "fmov src");
                if (!src_float.has_value()) {
                    return std::nullopt;
                }
                if (!is_supported_scalar_fp_kind(dst_float->kind) ||
                    dst_float->kind != src_float->kind) {
                    return unsupported("unsupported scalar fmov register kind");
                }
                const auto base = fp_reg_move_base(dst_float->kind);
                if (!base.has_value()) {
                    return unsupported("unsupported scalar fmov register kind");
                }
                encoded.word = *base | ((src_float->code & 0x1fU) << 5) |
                               (dst_float->code & 0x1fU);
                return encoded;
            }
            const auto src_general = resolve_general_reg_operand(
                operands[1], function, false, false, diagnostic_engine, "fmov src");
            const auto base =
                src_general.has_value()
                    ? fp_gp_move_base(dst_float->kind, src_general->use_64bit, true)
                    : std::nullopt;
            if (!src_general.has_value() || !base.has_value()) {
                return std::nullopt;
            }
            encoded.word = *base | ((src_general->code & 0x1fU) << 5) |
                           (dst_float->code & 0x1fU);
            return encoded;
        }
        const auto dst_general = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, "fmov dst");
        const auto src_float = resolve_float_reg_operand(
            operands[1], function, diagnostic_engine, "fmov src");
        const auto base = (dst_general.has_value() && src_float.has_value())
                              ? fp_gp_move_base(src_float->kind, dst_general->use_64bit,
                                                false)
                              : std::nullopt;
        if (!dst_general.has_value() || !src_float.has_value() || !base.has_value()) {
            return std::nullopt;
        }
        encoded.word = *base | ((src_float->code & 0x1fU) << 5) |
                       (dst_general->code & 0x1fU);
        return encoded;
    }

    if (mnemonic == "fadd" || mnemonic == "fsub" || mnemonic == "fmul" ||
        mnemonic == "fdiv") {
        if (operands.size() != 3) {
            return unsupported("floating binary operand shape");
        }
        const auto rd = resolve_float_reg_operand(
            operands[0], function, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_float_reg_operand(
            operands[1], function, diagnostic_engine, mnemonic + " lhs");
        const auto rm = resolve_float_reg_operand(
            operands[2], function, diagnostic_engine, mnemonic + " rhs");
        const auto base = (rd.has_value() && rn.has_value() && rm.has_value())
                              ? fp_binary_base(mnemonic, rd->kind)
                              : std::nullopt;
        if (!rd.has_value() || !rn.has_value() || !rm.has_value() ||
            rd->kind != rn->kind || rd->kind != rm->kind ||
            !base.has_value()) {
            return std::nullopt;
        }
        encoded.word = *base | ((rm->code & 0x1fU) << 16) |
                       ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (mnemonic == "fcmp") {
        if (operands.size() != 2) {
            return unsupported("fcmp operand shape");
        }
        const auto rn = resolve_float_reg_operand(
            operands[0], function, diagnostic_engine, "fcmp lhs");
        const auto base = rn.has_value() ? fcmp_base(rn->kind) : std::nullopt;
        if (!rn.has_value() || !base.has_value()) {
            return std::nullopt;
        }
        if (const auto rm = resolve_float_reg_operand(
                operands[1], function, diagnostic_engine, "fcmp rhs");
            rm.has_value()) {
            if (rm->kind != rn->kind) {
                return std::nullopt;
            }
            encoded.word = *base | ((rm->code & 0x1fU) << 16) |
                           ((rn->code & 0x1fU) << 5);
            return encoded;
        }
        const auto imm = parse_operand_immediate(operands[1]);
        const auto *immediate = operands[1].get_immediate_operand();
        if ((imm.has_value() && *imm == 0) ||
            (immediate != nullptr && immediate->asm_text.find("0.0") != std::string::npos)) {
            encoded.word = *base | ((rn->code & 0x1fU) << 5);
            return encoded;
        }
        return unsupported("fcmp immediate");
    }

    if (mnemonic == "scvtf" || mnemonic == "ucvtf" || mnemonic == "fcvtzs" ||
        mnemonic == "fcvtzu") {
        if (operands.size() != 2) {
            return unsupported("int/float conversion operand shape");
        }
        if (mnemonic == "scvtf" || mnemonic == "ucvtf") {
            const auto rd = resolve_float_reg_operand(
                operands[0], function, diagnostic_engine, mnemonic + " dst");
            const auto rn = resolve_general_reg_operand(
                operands[1], function, false, false, diagnostic_engine, mnemonic + " src");
            const auto base =
                (rd.has_value() && rn.has_value())
                    ? int_to_fp_base(mnemonic, rd->kind, rn->use_64bit)
                    : std::nullopt;
            if (!rd.has_value() || !rn.has_value() || !base.has_value()) {
                return std::nullopt;
            }
            encoded.word = *base |
                           ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
            return encoded;
        }
        const auto rd = resolve_general_reg_operand(
            operands[0], function, false, false, diagnostic_engine, mnemonic + " dst");
        const auto rn = resolve_float_reg_operand(
            operands[1], function, diagnostic_engine, mnemonic + " src");
        const auto base =
            (rd.has_value() && rn.has_value())
                ? fp_to_int_base(mnemonic, rn->kind, rd->use_64bit)
                : std::nullopt;
        if (!rd.has_value() || !rn.has_value() || !base.has_value()) {
            return std::nullopt;
        }
        encoded.word = *base |
                       ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    if (mnemonic == "fcvt") {
        if (operands.size() != 2) {
            return unsupported("fcvt operand shape");
        }
        const auto rd = resolve_float_reg_operand(
            operands[0], function, diagnostic_engine, "fcvt dst");
        const auto rn = resolve_float_reg_operand(
            operands[1], function, diagnostic_engine, "fcvt src");
        const auto base = (rd.has_value() && rn.has_value())
                              ? fp_convert_base(rd->kind, rn->kind)
                              : std::nullopt;
        if (!rd.has_value() || !rn.has_value() || !base.has_value()) {
            return std::nullopt;
        }
        encoded.word = *base | ((rn->code & 0x1fU) << 5) | (rd->code & 0x1fU);
        return encoded;
    }

    return unsupported("encoder coverage gap");
}

bool append_fragment_to_section(
    SectionImage &section, const AArch64DataFragment &fragment,
    DiagnosticEngine &diagnostic_engine) {
    const std::size_t fragment_base =
        section.type == kElfSectionNoBits ? section.nobits_size : section.bytes.size();
    if (const auto *zero_fill = fragment.get_zero_fill(); zero_fill != nullptr) {
        if (section.type == kElfSectionNoBits) {
            section.nobits_size += zero_fill->size;
        } else {
            section.bytes.insert(section.bytes.end(), zero_fill->size, 0);
        }
        return true;
    }
    if (const auto *bytes = fragment.get_byte_sequence(); bytes != nullptr) {
        if (section.type == kElfSectionNoBits) {
            diagnostic_engine.add_error(
                DiagnosticStage::Compiler,
                "AArch64 native data-only object writer cannot place initialized bytes into .bss");
            return false;
        }
        section.bytes.insert(section.bytes.end(), bytes->bytes.begin(),
                             bytes->bytes.end());
        for (const AArch64RelocationRecord &relocation : fragment.get_relocations()) {
            section.relocations.push_back(
                PendingRelocation{fragment_base + relocation.offset, relocation});
        }
        return true;
    }
    if (fragment.is_scalar_value()) {
        const std::size_t scalar_size = fragment.get_scalar_size();
        const std::uint64_t scalar_bits = fragment.get_scalar_bits();
        if (section.type == kElfSectionNoBits) {
            if (scalar_bits != 0 || !fragment.get_relocations().empty()) {
                diagnostic_engine.add_error(
                    DiagnosticStage::Compiler,
                    "AArch64 native data-only object writer cannot place non-zero or relocatable scalars into .bss");
                return false;
            }
            section.nobits_size += scalar_size;
            return true;
        }
        for (std::size_t byte_index = 0; byte_index < scalar_size; ++byte_index) {
            section.bytes.push_back(static_cast<std::uint8_t>(
                (scalar_bits >> (byte_index * 8)) & 0xffU));
        }
        for (const AArch64RelocationRecord &relocation : fragment.get_relocations()) {
            section.relocations.push_back(
                PendingRelocation{fragment_base + relocation.offset, relocation});
        }
        return true;
    }
    return true;
}

bool build_data_object_section_images(
    const AArch64ObjectModule &object_module, std::vector<SectionImage> &sections,
    std::unordered_map<std::string, DefinedSymbol> &defined_symbols,
    DiagnosticEngine &diagnostic_engine) {
    auto ensure_section = [&](AArch64SectionKind kind) -> SectionImage & {
        for (SectionImage &section : sections) {
            if (section.kind == kind) {
                return section;
            }
        }
        SectionImage section;
        section.kind = kind;
        section.name = section_name_for_object(kind);
        section.flags = section_flags_for_object(kind);
        section.type = kind == AArch64SectionKind::Bss ? kElfSectionNoBits
                                                       : kElfSectionProgBits;
        sections.push_back(std::move(section));
        return sections.back();
    };

    for (const AArch64DataObject &data_object : object_module.get_data_objects()) {
        const char *section_name = section_name_for_object(data_object.get_section_kind());
        if (section_name == nullptr) {
            continue;
        }
        SectionImage &section = ensure_section(data_object.get_section_kind());
        const std::size_t align =
            std::max<std::size_t>(1, static_cast<std::size_t>(1)
                                         << data_object.get_align_log2());
        section.align = std::max<std::uint64_t>(section.align, align);
        const std::size_t section_size =
            section.type == kElfSectionNoBits ? section.nobits_size : section.bytes.size();
        const std::size_t aligned_size = align_to(section_size, align);
        if (section.type == kElfSectionNoBits) {
            section.nobits_size = aligned_size;
        } else {
            section.bytes.insert(section.bytes.end(), aligned_size - section_size, 0);
        }
        defined_symbols[data_object.get_symbol_name()] = DefinedSymbol{
            data_object.get_section_kind(), aligned_size, data_object.get_size()};
        for (const AArch64DataFragment &fragment : data_object.get_fragments()) {
            if (!append_fragment_to_section(section, fragment, diagnostic_engine)) {
                return false;
            }
        }
    }
    return true;
}

std::uint32_t append_string(std::vector<std::uint8_t> &table, const std::string &text) {
    const std::uint32_t offset = static_cast<std::uint32_t>(table.size());
    table.insert(table.end(), text.begin(), text.end());
    table.push_back('\0');
    return offset;
}

bool write_output_file(const std::filesystem::path &path,
                       const std::vector<std::uint8_t> &bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output.is_open()) {
        return false;
    }
    output.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

SectionImage &ensure_section_image(std::vector<SectionImage> &sections,
                                   AArch64SectionKind kind) {
    for (SectionImage &section : sections) {
        if (section.kind == kind) {
            return section;
        }
    }
    SectionImage section;
    section.kind = kind;
    section.name = section_name_for_object(kind);
    section.flags = section_flags_for_object(kind);
    section.type = kind == AArch64SectionKind::Bss ? kElfSectionNoBits
                                                   : kElfSectionProgBits;
    section.align = kind == AArch64SectionKind::Text ? 4 : 1;
    sections.push_back(std::move(section));
    return sections.back();
}

bool build_text_section_image(
    const AArch64MachineModule &machine_module, std::vector<SectionImage> &sections,
    std::unordered_map<std::string, DefinedSymbol> &defined_symbols,
    std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    DiagnosticEngine &diagnostic_engine) {
    if (machine_module.get_functions().empty()) {
        return true;
    }
    SectionImage &text_section =
        ensure_section_image(sections, AArch64SectionKind::Text);
    text_section.align = std::max<std::uint64_t>(text_section.align, 4);

    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        FunctionScanInfo scan_info = scan_function_layout(function, diagnostic_engine);
        const std::size_t function_offset = align_to(text_section.bytes.size(), 4);
        text_section.bytes.resize(function_offset, 0);
        defined_symbols[function.get_name()] =
            DefinedSymbol{AArch64SectionKind::Text, function_offset,
                          scan_info.code_size};

        std::size_t local_pc = 0;
        for (const AArch64MachineBlock &block : function.get_blocks()) {
            for (const AArch64MachineInstr &instruction : block.get_instructions()) {
                if (!is_real_text_instruction(instruction)) {
                    continue;
                }
                const auto encoded = encode_machine_instruction(
                    instruction, function, scan_info, local_pc, diagnostic_engine);
                if (!encoded.has_value()) {
                    return false;
                }
                append_pod(text_section.bytes, encoded->word);
                for (AArch64RelocationRecord relocation : encoded->relocations) {
                    relocation.offset += function_offset;
                    text_section.relocations.push_back(
                        PendingRelocation{relocation.offset, std::move(relocation)});
                }
                local_pc += 4;
            }
        }
        scanned_functions.emplace(function.get_name(), std::move(scan_info));
    }
    return true;
}

bool build_full_symbol_entries(
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, DefinedSymbol> &defined_symbols,
    const std::vector<SectionImage> &sections,
    bool force_defined_symbols_global, std::vector<SymbolEntry> &symbols,
    std::unordered_map<std::string, std::uint32_t> &symbol_indices) {
    std::vector<std::string> needed_symbol_names;
    std::unordered_map<std::string, AArch64SymbolDescriptor> referenced_symbols;
    auto note_symbol_descriptor = [&](const AArch64SymbolDescriptor &descriptor) {
        auto [it, inserted] =
            referenced_symbols.emplace(descriptor.name, descriptor);
        if (!inserted) {
            merge_symbol_descriptor(it->second, descriptor);
        }
    };

    for (const auto &[name, definition] : defined_symbols) {
        (void)definition;
        needed_symbol_names.push_back(name);
    }
    for (const SectionImage &section : sections) {
        for (const PendingRelocation &relocation : section.relocations) {
            note_symbol_descriptor(relocation.record.target.symbol);
            if (std::find(needed_symbol_names.begin(), needed_symbol_names.end(),
                          relocation.record.target.get_name()) ==
                needed_symbol_names.end()) {
                needed_symbol_names.push_back(relocation.record.target.get_name());
            }
        }
    }

    std::vector<SymbolEntry> local_symbols;
    std::vector<SymbolEntry> global_symbols;
    for (const std::string &name : needed_symbol_names) {
        SymbolEntry entry;
        entry.name = name;

        const auto module_symbol_it = object_module.get_symbols().find(name);
        const bool has_module_symbol =
            module_symbol_it != object_module.get_symbols().end();
        const auto referenced_symbol_it = referenced_symbols.find(name);
        const AArch64SymbolKind symbol_kind =
            has_module_symbol ? module_symbol_it->second.get_kind()
                              : (referenced_symbol_it != referenced_symbols.end()
                                     ? referenced_symbol_it->second.kind
                                     : AArch64SymbolKind::Object);
        const AArch64SymbolBinding symbol_binding =
            has_module_symbol
                ? module_symbol_it->second.get_binding()
                : (referenced_symbol_it != referenced_symbols.end()
                       ? referenced_symbol_it->second.binding
                       : AArch64SymbolBinding::Unknown);
        entry.type = elf_symbol_type(symbol_kind);

        const auto definition_it = defined_symbols.find(name);
        if (definition_it != defined_symbols.end()) {
            const DefinedSymbol &definition = definition_it->second;
            const auto section_it = std::find_if(
                sections.begin(), sections.end(), [&](const SectionImage &section) {
                    return section.kind == definition.section_kind;
                });
            if (section_it == sections.end()) {
                return false;
            }
            entry.section_index = section_it->section_index;
            entry.value = definition.offset;
            entry.size = definition.size;
            entry.binding = (force_defined_symbols_global ||
                             symbol_binding == AArch64SymbolBinding::Global)
                                ? kElfSymbolBindingGlobal
                                : kElfSymbolBindingLocal;
        } else {
            entry.binding =
                symbol_binding == AArch64SymbolBinding::Local
                    ? kElfSymbolBindingLocal
                    : kElfSymbolBindingGlobal;
        }

        if (entry.binding == kElfSymbolBindingLocal) {
            local_symbols.push_back(std::move(entry));
        } else {
            global_symbols.push_back(std::move(entry));
        }
    }

    symbols.clear();
    symbols.push_back(SymbolEntry{});
    for (SymbolEntry &entry : local_symbols) {
        entry.index = static_cast<std::uint32_t>(symbols.size());
        symbol_indices[entry.name] = entry.index;
        symbols.push_back(std::move(entry));
    }
    for (SymbolEntry &entry : global_symbols) {
        entry.index = static_cast<std::uint32_t>(symbols.size());
        symbol_indices[entry.name] = entry.index;
        symbols.push_back(std::move(entry));
    }
    return true;
}

void append_dwarf_advance(std::vector<std::uint8_t> &bytes,
                          std::size_t byte_delta) {
    const std::size_t units = byte_delta / 4;
    if (units == 0) {
        return;
    }
    if (units < 0x40) {
        bytes.push_back(static_cast<std::uint8_t>(0x40U | units));
        return;
    }
    if (units <= 0xffU) {
        bytes.push_back(0x02U);
        bytes.push_back(static_cast<std::uint8_t>(units));
        return;
    }
    if (units <= 0xffffU) {
        bytes.push_back(0x03U);
        std::uint16_t value = static_cast<std::uint16_t>(units);
        append_pod(bytes, value);
        return;
    }
    bytes.push_back(0x04U);
    std::uint32_t value = static_cast<std::uint32_t>(units);
    append_pod(bytes, value);
}

void append_cfi_note_bytes(std::vector<std::uint8_t> &bytes,
                           const ParsedCfiNote &note) {
    switch (note.kind) {
    case ParsedCfiNoteKind::DefCfa:
        bytes.push_back(0x0cU);
        append_uleb128(bytes, note.reg);
        append_uleb128(bytes, static_cast<std::uint64_t>(note.offset));
        return;
    case ParsedCfiNoteKind::DefCfaRegister:
        bytes.push_back(0x0dU);
        append_uleb128(bytes, note.reg);
        return;
    case ParsedCfiNoteKind::DefCfaOffset:
        bytes.push_back(0x0eU);
        append_uleb128(bytes, static_cast<std::uint64_t>(note.offset));
        return;
    case ParsedCfiNoteKind::Offset: {
        const std::uint64_t scaled = static_cast<std::uint64_t>((-note.offset) / 8);
        if (note.reg < 64) {
            bytes.push_back(static_cast<std::uint8_t>(0x80U | note.reg));
        } else {
            bytes.push_back(0x05U);
            append_uleb128(bytes, note.reg);
        }
        append_uleb128(bytes, scaled);
        return;
    }
    case ParsedCfiNoteKind::Restore:
        if (note.reg < 64) {
            bytes.push_back(static_cast<std::uint8_t>(0xc0U | note.reg));
        } else {
            bytes.push_back(0x06U);
            append_uleb128(bytes, note.reg);
        }
        return;
    }
}

bool build_eh_frame_section_image(
    const AArch64MachineModule &machine_module,
    const std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    std::vector<SectionImage> &sections) {
    if (machine_module.get_functions().empty()) {
        return true;
    }
    SectionImage &section =
        ensure_section_image(sections, AArch64SectionKind::EhFrame);
    section.align = std::max<std::uint64_t>(section.align, 8);

    const std::size_t cie_start = section.bytes.size();
    append_pod(section.bytes, std::uint32_t{0});
    append_pod(section.bytes, std::uint32_t{0});
    section.bytes.push_back(1);
    section.bytes.push_back('z');
    section.bytes.push_back('R');
    section.bytes.push_back('\0');
    append_uleb128(section.bytes, 4);
    append_sleb128(section.bytes, -8);
    append_uleb128(section.bytes, 30);
    append_uleb128(section.bytes, 1);
    section.bytes.push_back(0x1bU);
    section.bytes.push_back(0x0cU);
    append_uleb128(section.bytes, 31);
    append_uleb128(section.bytes, 0);
    section.bytes.resize(align_to(section.bytes.size(), 4), 0);
    overwrite_u32(section.bytes, cie_start,
                  static_cast<std::uint32_t>(section.bytes.size() - cie_start - 4));

    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        const auto scan_it = scanned_functions.find(function.get_name());
        if (scan_it == scanned_functions.end()) {
            continue;
        }
        const FunctionScanInfo &scan = scan_it->second;
        const std::size_t fde_start = section.bytes.size();
        append_pod(section.bytes, std::uint32_t{0});
        append_pod(section.bytes,
                   static_cast<std::uint32_t>((fde_start + 4) - cie_start));
        const std::size_t reloc_offset = section.bytes.size();
        append_pod(section.bytes, std::uint32_t{0});
        section.relocations.push_back(PendingRelocation{
            reloc_offset,
            AArch64RelocationRecord{
                AArch64RelocationKind::Prel32,
                AArch64SymbolReference::direct(function.get_name(),
                                               AArch64SymbolKind::Function,
                                               AArch64SymbolBinding::Unknown,
                                               AArch64SectionKind::Text, 0, true),
                reloc_offset}});
        append_pod(section.bytes, static_cast<std::uint32_t>(scan.code_size));
        append_uleb128(section.bytes, 0);

        std::size_t current_pc = 0;
        for (const AArch64CfiDirective &directive :
             function.get_frame_record().get_cfi_directives()) {
            if (directive.kind == AArch64CfiDirectiveKind::StartProcedure ||
                directive.kind == AArch64CfiDirectiveKind::EndProcedure) {
                continue;
            }
            append_dwarf_advance(section.bytes,
                                 directive.code_offset - current_pc);
            current_pc = directive.code_offset;
            switch (directive.kind) {
            case AArch64CfiDirectiveKind::DefCfa:
                append_cfi_note_bytes(section.bytes,
                                      ParsedCfiNote{.pc_offset = directive.code_offset,
                                                    .kind = ParsedCfiNoteKind::DefCfa,
                                                    .reg = directive.reg,
                                                    .offset = directive.offset});
                break;
            case AArch64CfiDirectiveKind::DefCfaRegister:
                append_cfi_note_bytes(
                    section.bytes,
                    ParsedCfiNote{
                        .pc_offset = directive.code_offset,
                        .kind = ParsedCfiNoteKind::DefCfaRegister,
                        .reg = directive.reg});
                break;
            case AArch64CfiDirectiveKind::DefCfaOffset:
                append_cfi_note_bytes(section.bytes,
                                      ParsedCfiNote{.pc_offset = directive.code_offset,
                                                    .kind = ParsedCfiNoteKind::DefCfaOffset,
                                                    .offset = directive.offset});
                break;
            case AArch64CfiDirectiveKind::Offset:
                append_cfi_note_bytes(section.bytes,
                                      ParsedCfiNote{.pc_offset = directive.code_offset,
                                                    .kind = ParsedCfiNoteKind::Offset,
                                                    .reg = directive.reg,
                                                    .offset = directive.offset});
                break;
            case AArch64CfiDirectiveKind::Restore:
                append_cfi_note_bytes(section.bytes,
                                      ParsedCfiNote{.pc_offset = directive.code_offset,
                                                    .kind = ParsedCfiNoteKind::Restore,
                                                    .reg = directive.reg});
                break;
            case AArch64CfiDirectiveKind::StartProcedure:
            case AArch64CfiDirectiveKind::EndProcedure:
                break;
            }
        }
        section.bytes.resize(align_to(section.bytes.size(), 4), 0);
        overwrite_u32(section.bytes, fde_start,
                      static_cast<std::uint32_t>(section.bytes.size() - fde_start -
                                                 4));
    }

    return true;
}

void append_debug_line_set_address(SectionImage &section,
                                   const std::string &function_name,
                                   long long addend) {
    section.bytes.push_back(0);
    append_uleb128(section.bytes, 1 + 8);
    section.bytes.push_back(2);
    const std::size_t reloc_offset = section.bytes.size();
    append_pod(section.bytes, std::uint64_t{0});
    section.relocations.push_back(PendingRelocation{
        reloc_offset,
        AArch64RelocationRecord{
            AArch64RelocationKind::Absolute64,
            AArch64SymbolReference::direct(function_name, AArch64SymbolKind::Function,
                                           AArch64SymbolBinding::Unknown,
                                           AArch64SectionKind::Text, addend, true),
            reloc_offset}});
}

bool build_debug_line_section_image(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const std::unordered_map<std::string, FunctionScanInfo> &scanned_functions,
    std::vector<SectionImage> &sections) {
    if (object_module.get_debug_file_entries().empty()) {
        return true;
    }

    bool has_locations = false;
    for (const auto &[name, scan] : scanned_functions) {
        (void)name;
        if (!scan.source_locations.empty()) {
            has_locations = true;
            break;
        }
    }
    if (!has_locations) {
        return true;
    }

    SectionImage &section =
        ensure_section_image(sections, AArch64SectionKind::DebugLine);
    section.align = 1;

    const std::size_t unit_length_offset = section.bytes.size();
    append_pod(section.bytes, std::uint32_t{0});
    append_pod(section.bytes, std::uint16_t{2});
    const std::size_t header_length_offset = section.bytes.size();
    append_pod(section.bytes, std::uint32_t{0});
    const std::size_t header_start = section.bytes.size();
    section.bytes.push_back(4);
    section.bytes.push_back(1);
    section.bytes.push_back(static_cast<std::uint8_t>(-5));
    section.bytes.push_back(14);
    section.bytes.push_back(10);
    const std::uint8_t standard_opcode_lengths[] = {0, 1, 1, 1, 1, 0, 0, 0, 1};
    section.bytes.insert(section.bytes.end(), std::begin(standard_opcode_lengths),
                         std::end(standard_opcode_lengths));
    section.bytes.push_back('\0');
    for (const AArch64DebugFileEntry &entry : object_module.get_debug_file_entries()) {
        section.bytes.insert(section.bytes.end(), entry.path.begin(), entry.path.end());
        section.bytes.push_back('\0');
        append_uleb128(section.bytes, 0);
        append_uleb128(section.bytes, 0);
        append_uleb128(section.bytes, 0);
    }
    section.bytes.push_back('\0');
    overwrite_u32(section.bytes, header_length_offset,
                  static_cast<std::uint32_t>(section.bytes.size() - header_start));

    for (const AArch64MachineFunction &function : machine_module.get_functions()) {
        const auto scan_it = scanned_functions.find(function.get_name());
        if (scan_it == scanned_functions.end() ||
            scan_it->second.source_locations.empty()) {
            continue;
        }
        const FunctionScanInfo &scan = scan_it->second;
        const SourceLocationNote &first = scan.source_locations.front();
        append_debug_line_set_address(section, function.get_name(),
                                      static_cast<long long>(first.pc_offset));

        unsigned current_file = 1;
        int current_line = 1;
        int current_column = 0;
        std::size_t current_pc = first.pc_offset;

        auto append_note = [&](const SourceLocationNote &note, bool is_first) {
            if (!is_first && note.pc_offset > current_pc) {
                section.bytes.push_back(2);
                append_uleb128(section.bytes,
                               static_cast<std::uint64_t>((note.pc_offset - current_pc) /
                                                          4));
            }
            if (note.file_id != current_file) {
                section.bytes.push_back(4);
                append_uleb128(section.bytes, note.file_id);
            }
            if (note.column != current_column) {
                section.bytes.push_back(5);
                append_uleb128(section.bytes, static_cast<std::uint64_t>(note.column));
            }
            if (note.line != current_line) {
                section.bytes.push_back(3);
                append_sleb128(section.bytes,
                               static_cast<std::int64_t>(note.line - current_line));
            }
            section.bytes.push_back(1);
            current_file = note.file_id;
            current_line = note.line;
            current_column = note.column;
            current_pc = note.pc_offset;
        };

        append_note(first, true);
        for (std::size_t index = 1; index < scan.source_locations.size(); ++index) {
            append_note(scan.source_locations[index], false);
        }
        if (scan.code_size > current_pc) {
            section.bytes.push_back(2);
            append_uleb128(section.bytes,
                           static_cast<std::uint64_t>((scan.code_size - current_pc) /
                                                      4));
        }
        section.bytes.push_back(0);
        append_uleb128(section.bytes, 1);
        section.bytes.push_back(1);
    }

    overwrite_u32(section.bytes, unit_length_offset,
                  static_cast<std::uint32_t>(section.bytes.size() -
                                             unit_length_offset - 4));
    return true;
}

bool write_sectioned_object(const std::filesystem::path &object_file,
                           const std::vector<SectionImage> &section_images,
                           std::vector<SymbolEntry> symbols,
                           const std::unordered_map<std::string, std::uint32_t> &symbol_indices,
                           DiagnosticEngine &diagnostic_engine,
                           const char *error_prefix) {
    std::vector<FinalSection> sections;
    sections.reserve(section_images.size() * 2 + 3);
    for (const SectionImage &image : section_images) {
        FinalSection section;
        section.name = image.name;
        section.type = image.type;
        section.flags = image.flags;
        section.align = image.align;
        section.entry_size = image.entry_size;
        section.bytes = image.bytes;
        section.logical_size =
            image.type == kElfSectionNoBits ? image.nobits_size : image.bytes.size();
        sections.push_back(std::move(section));
    }

    std::vector<std::uint8_t> symbol_string_table(1, '\0');
    for (std::size_t index = 1; index < symbols.size(); ++index) {
        symbols[index].name_offset =
            append_string(symbol_string_table, symbols[index].name);
    }

    std::vector<std::uint8_t> symtab_bytes;
    symtab_bytes.reserve(symbols.size() * sizeof(Elf64Symbol));
    std::uint32_t first_global_symbol_index = 1;
    bool saw_global_symbol = false;
    for (const SymbolEntry &entry : symbols) {
        if (!saw_global_symbol && entry.binding == kElfSymbolBindingGlobal &&
            entry.index != 0) {
            first_global_symbol_index = entry.index;
            saw_global_symbol = true;
        }
        Elf64Symbol elf_symbol;
        elf_symbol.name = entry.name_offset;
        elf_symbol.info = static_cast<unsigned char>((entry.binding << 4) |
                                                     (entry.type & 0x0fU));
        elf_symbol.section_index = entry.section_index;
        elf_symbol.value = entry.value;
        elf_symbol.size = entry.size;
        append_pod(symtab_bytes, elf_symbol);
    }

    std::vector<FinalSection> relocation_sections;
    for (const SectionImage &image : section_images) {
        if (image.relocations.empty()) {
            continue;
        }
        FinalSection rela_section;
        rela_section.name = ".rela" + image.name;
        rela_section.type = kElfSectionRela;
        rela_section.align = 8;
        rela_section.entry_size = sizeof(Elf64Rela);
        rela_section.info = image.section_index;
        for (const PendingRelocation &relocation : image.relocations) {
            const std::uint32_t reloc_type =
                relocation_type_for_record(relocation.record.kind);
            if (reloc_type == 0) {
                diagnostic_engine.add_error(
                    DiagnosticStage::Compiler,
                    std::string(error_prefix) +
                        "encountered an unsupported relocation kind");
                return false;
            }
            const auto symbol_index_it =
                symbol_indices.find(relocation.record.target.get_name());
            if (symbol_index_it == symbol_indices.end()) {
                diagnostic_engine.add_error(
                    DiagnosticStage::Compiler,
                    std::string(error_prefix) + "missing relocation symbol '" +
                        relocation.record.target.get_name() + "'");
                return false;
            }
            Elf64Rela rela;
            rela.offset = relocation.offset;
            rela.info = (static_cast<std::uint64_t>(symbol_index_it->second) << 32) |
                        reloc_type;
            rela.addend = relocation.record.target.addend;
            append_pod(rela_section.bytes, rela);
        }
        rela_section.logical_size = rela_section.bytes.size();
        relocation_sections.push_back(std::move(rela_section));
    }

    const std::uint32_t symtab_section_index = static_cast<std::uint32_t>(
        sections.size() + relocation_sections.size() + 1);
    const std::uint32_t strtab_section_index = symtab_section_index + 1;
    for (FinalSection &rela_section : relocation_sections) {
        rela_section.link = symtab_section_index;
        sections.push_back(std::move(rela_section));
    }

    FinalSection symtab_section;
    symtab_section.name = ".symtab";
    symtab_section.type = kElfSectionSymTab;
    symtab_section.align = 8;
    symtab_section.entry_size = sizeof(Elf64Symbol);
    symtab_section.bytes = std::move(symtab_bytes);
    symtab_section.logical_size = symtab_section.bytes.size();
    symtab_section.link = strtab_section_index;
    symtab_section.info = saw_global_symbol ? first_global_symbol_index
                                            : static_cast<std::uint32_t>(symbols.size());
    sections.push_back(std::move(symtab_section));

    FinalSection strtab_section;
    strtab_section.name = ".strtab";
    strtab_section.type = kElfSectionStrTab;
    strtab_section.align = 1;
    strtab_section.bytes = std::move(symbol_string_table);
    strtab_section.logical_size = strtab_section.bytes.size();
    sections.push_back(std::move(strtab_section));

    std::vector<std::uint8_t> section_name_table(1, '\0');
    std::vector<std::uint32_t> section_name_offsets;
    section_name_offsets.reserve(sections.size() + 1);
    section_name_offsets.push_back(0);
    for (const FinalSection &section : sections) {
        section_name_offsets.push_back(append_string(section_name_table, section.name));
    }
    const std::uint32_t shstrtab_name_offset =
        append_string(section_name_table, ".shstrtab");
    section_name_offsets.push_back(shstrtab_name_offset);

    FinalSection shstrtab_section;
    shstrtab_section.name = ".shstrtab";
    shstrtab_section.type = kElfSectionStrTab;
    shstrtab_section.align = 1;
    shstrtab_section.bytes = std::move(section_name_table);
    shstrtab_section.logical_size = shstrtab_section.bytes.size();
    sections.push_back(std::move(shstrtab_section));

    const std::uint16_t shstrtab_section_index =
        static_cast<std::uint16_t>(sections.size());

    std::vector<std::uint8_t> object_bytes(sizeof(Elf64Header), 0);
    for (FinalSection &section : sections) {
        if (section.type == kElfSectionNoBits) {
            section.file_offset = align_to(object_bytes.size(), section.align);
            continue;
        }
        object_bytes.resize(align_to(object_bytes.size(), section.align), 0);
        section.file_offset = object_bytes.size();
        object_bytes.insert(object_bytes.end(), section.bytes.begin(),
                            section.bytes.end());
    }
    const std::uint64_t section_header_offset = align_to(object_bytes.size(), 8);
    object_bytes.resize(section_header_offset, 0);

    std::vector<Elf64SectionHeader> section_headers(sections.size() + 1);
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const FinalSection &section = sections[index];
        Elf64SectionHeader &header = section_headers[index + 1];
        header.name = section_name_offsets[index + 1];
        header.type = section.type;
        header.flags = section.flags;
        header.offset = section.file_offset;
        header.size = section.logical_size;
        header.link = section.link;
        header.info = section.info;
        header.address_align = std::max<std::uint64_t>(1, section.align);
        header.entry_size = section.entry_size;
    }
    section_headers.back().name = shstrtab_name_offset;

    for (const Elf64SectionHeader &header : section_headers) {
        append_pod(object_bytes, header);
    }

    Elf64Header header;
    header.ident[0] = 0x7f;
    header.ident[1] = 'E';
    header.ident[2] = 'L';
    header.ident[3] = 'F';
    header.ident[4] = kElfClass64;
    header.ident[5] = kElfDataLittleEndian;
    header.ident[6] = kElfVersionCurrent;
    header.type = kElfTypeRelocatable;
    header.machine = kElfMachineAArch64;
    header.version = kElfVersionCurrent;
    header.section_header_offset = section_header_offset;
    header.header_size = sizeof(Elf64Header);
    header.section_header_entry_size = sizeof(Elf64SectionHeader);
    header.section_header_count = static_cast<std::uint16_t>(section_headers.size());
    header.section_name_string_table_index = shstrtab_section_index;
    std::memcpy(object_bytes.data(), &header, sizeof(header));

    if (!write_output_file(object_file, object_bytes)) {
        diagnostic_engine.add_error(DiagnosticStage::Compiler,
                                    std::string(error_prefix) +
                                        "failed to write the native AArch64 object file");
        return false;
    }
    return true;
}

} // namespace

bool write_aarch64_elf_object(
    const AArch64MachineModule &machine_module,
    const AArch64ObjectModule &object_module,
    const std::filesystem::path &object_file,
    const AArch64ElfObjectWriterOptions &options,
    DiagnosticEngine &diagnostic_engine) {
    std::vector<SectionImage> sections;
    std::unordered_map<std::string, DefinedSymbol> defined_symbols;
    std::unordered_map<std::string, FunctionScanInfo> scanned_functions;

    if (!build_data_object_section_images(object_module, sections, defined_symbols,
                                          diagnostic_engine) ||
        !build_text_section_image(machine_module, sections, defined_symbols,
                                  scanned_functions, diagnostic_engine) ||
        !build_eh_frame_section_image(machine_module, scanned_functions, sections) ||
        !build_debug_line_section_image(machine_module, object_module,
                                        scanned_functions, sections)) {
        return false;
    }

    if (sections.empty()) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 direct object writer received an empty module");
        return false;
    }

    std::uint32_t next_section_index = 1;
    for (SectionImage &section : sections) {
        section.section_index = next_section_index++;
    }

    std::vector<SymbolEntry> symbols;
    std::unordered_map<std::string, std::uint32_t> symbol_indices;
    if (!build_full_symbol_entries(object_module, defined_symbols, sections,
                                   options.force_defined_symbols_global, symbols,
                                   symbol_indices)) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "failed to build the native AArch64 object symbol table");
        return false;
    }

    return write_sectioned_object(object_file, sections, std::move(symbols),
                                  symbol_indices, diagnostic_engine,
                                  "AArch64 direct object writer: ");
}

bool write_aarch64_data_only_object(
    const AArch64ObjectModule &object_module,
    const std::filesystem::path &object_file,
    const AArch64DataOnlyObjectWriterOptions &options,
    DiagnosticEngine &diagnostic_engine) {
    if (object_module.get_data_objects().empty()) {
        diagnostic_engine.add_error(
            DiagnosticStage::Compiler,
            "AArch64 native data-only object writer received a module without data sections");
        return false;
    }
    return write_aarch64_elf_object(
        AArch64MachineModule{}, object_module, object_file,
        AArch64ElfObjectWriterOptions{
            .force_defined_symbols_global = options.force_defined_symbols_global},
        diagnostic_engine);
}

} // namespace sysycc
