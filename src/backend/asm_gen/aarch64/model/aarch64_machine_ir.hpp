#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_register_info.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_symbol_reference_model.hpp"

namespace sysycc {

class CoreIrStackSlot;

enum class AArch64MachineOperandKind : unsigned char {
    VirtualReg,
    PhysicalReg,
    Immediate,
    Symbol,
    Label,
    ConditionCode,
    ZeroRegister,
    Shift,
    StackPointer,
    MemoryAddress,
};

struct AArch64MachineVirtualRegOperand {
    AArch64VirtualReg reg;
    bool is_def = false;
};

struct AArch64MachinePhysicalRegOperand {
    unsigned reg_number = 0;
    AArch64VirtualRegKind kind = AArch64VirtualRegKind::General32;
};

struct AArch64MachineConditionCodeOperand {
    std::string code;
};

struct AArch64MachineImmediateOperand {
    std::string asm_text;
};

struct AArch64MachineSymbolReference {
    enum class Modifier : unsigned char {
        None,
        Lo12,
        Got,
        GotLo12,
    };

    AArch64SymbolReference target;
    Modifier modifier = Modifier::None;

    static AArch64MachineSymbolReference plain(AArch64SymbolReference target) {
        return AArch64MachineSymbolReference{std::move(target), Modifier::None};
    }

    static AArch64MachineSymbolReference plain(
        std::string symbol_name,
        AArch64SymbolKind kind = AArch64SymbolKind::Object,
        AArch64SymbolBinding binding = AArch64SymbolBinding::Unknown,
        std::optional<AArch64SectionKind> section_kind = std::nullopt,
        long long addend = 0, bool is_defined = false) {
        return plain(AArch64SymbolReference::direct(
            std::move(symbol_name), kind, binding, section_kind, addend,
            is_defined));
    }

    static AArch64MachineSymbolReference lo12(AArch64SymbolReference target) {
        return AArch64MachineSymbolReference{std::move(target), Modifier::Lo12};
    }

    static AArch64MachineSymbolReference lo12(
        std::string symbol_name,
        AArch64SymbolKind kind = AArch64SymbolKind::Object,
        AArch64SymbolBinding binding = AArch64SymbolBinding::Unknown,
        std::optional<AArch64SectionKind> section_kind = std::nullopt,
        long long addend = 0, bool is_defined = false) {
        return lo12(AArch64SymbolReference::direct(
            std::move(symbol_name), kind, binding, section_kind, addend,
            is_defined));
    }

    static AArch64MachineSymbolReference got(AArch64SymbolReference target) {
        return AArch64MachineSymbolReference{std::move(target), Modifier::Got};
    }

    static AArch64MachineSymbolReference got(
        std::string symbol_name,
        AArch64SymbolKind kind = AArch64SymbolKind::Object,
        AArch64SymbolBinding binding = AArch64SymbolBinding::Unknown,
        std::optional<AArch64SectionKind> section_kind = std::nullopt,
        long long addend = 0, bool is_defined = false) {
        return got(AArch64SymbolReference::direct(
            std::move(symbol_name), kind, binding, section_kind, addend,
            is_defined));
    }

    static AArch64MachineSymbolReference got_lo12(AArch64SymbolReference target) {
        return AArch64MachineSymbolReference{std::move(target),
                                             Modifier::GotLo12};
    }

    static AArch64MachineSymbolReference got_lo12(
        std::string symbol_name,
        AArch64SymbolKind kind = AArch64SymbolKind::Object,
        AArch64SymbolBinding binding = AArch64SymbolBinding::Unknown,
        std::optional<AArch64SectionKind> section_kind = std::nullopt,
        long long addend = 0, bool is_defined = false) {
        return got_lo12(AArch64SymbolReference::direct(
            std::move(symbol_name), kind, binding, section_kind, addend,
            is_defined));
    }
};

struct AArch64MachineSymbolOperand {
    AArch64MachineSymbolReference reference;
};

struct AArch64MachineLabelOperand {
    std::string label_text;
};

struct AArch64MachineZeroRegisterOperand {
    bool use_64bit = false;
};

struct AArch64MachineShiftOperand {
    std::string mnemonic;
    unsigned amount = 0;
};

struct AArch64MachineStackPointerOperand {
    bool use_64bit = true;
};

struct AArch64MachineMemoryImmediateOffset {
    long long value = 0;
};

struct AArch64MachineMemoryAddressOperand {
    using OffsetPayload =
        std::variant<std::monostate, AArch64MachineMemoryImmediateOffset,
                     AArch64MachineSymbolReference>;

    enum class AddressMode : unsigned char {
        Offset,
        PreIndex,
        PostIndex,
    };

    enum class BaseKind : unsigned char {
        VirtualReg,
        PhysicalReg,
        StackPointer,
    };

    BaseKind base_kind = BaseKind::VirtualReg;
    AArch64VirtualReg virtual_reg;
    unsigned physical_reg = 0;
    bool stack_pointer_use_64bit = true;
    OffsetPayload offset;
    AddressMode address_mode = AddressMode::Offset;

    std::optional<long long> get_immediate_offset() const noexcept {
        const auto *immediate =
            std::get_if<AArch64MachineMemoryImmediateOffset>(&offset);
        if (immediate == nullptr) {
            return std::nullopt;
        }
        return immediate->value;
    }

    const AArch64MachineSymbolReference *get_symbolic_offset() const noexcept {
        return std::get_if<AArch64MachineSymbolReference>(&offset);
    }

    bool has_offset() const noexcept {
        return !std::holds_alternative<std::monostate>(offset);
    }
};

class AArch64MachineOperand {
  private:
    using Payload = std::variant<std::monostate, AArch64MachineVirtualRegOperand,
                                 AArch64MachinePhysicalRegOperand,
                                 AArch64MachineConditionCodeOperand,
                                 AArch64MachineImmediateOperand,
                                 AArch64MachineSymbolOperand,
                                 AArch64MachineLabelOperand,
                                 AArch64MachineZeroRegisterOperand,
                                 AArch64MachineShiftOperand,
                                 AArch64MachineStackPointerOperand,
                                 AArch64MachineMemoryAddressOperand>;

    AArch64MachineOperandKind kind_ = AArch64MachineOperandKind::Immediate;
    Payload payload_;

    AArch64MachineOperand(AArch64MachineOperandKind kind, Payload payload);

  public:
    static AArch64MachineOperand use_virtual_reg(const AArch64VirtualReg &reg);
    static AArch64MachineOperand def_virtual_reg(const AArch64VirtualReg &reg);
    static AArch64MachineOperand physical_reg(unsigned reg_number,
                                              AArch64VirtualRegKind kind);
    static AArch64MachineOperand immediate(std::string text);
    static AArch64MachineOperand symbol(std::string text);
    static AArch64MachineOperand symbol(AArch64SymbolReference reference);
    static AArch64MachineOperand symbol(AArch64MachineSymbolReference reference);
    static AArch64MachineOperand label(std::string text);
    static AArch64MachineOperand condition_code(std::string code);
    static AArch64MachineOperand zero_register(bool use_64bit);
    static AArch64MachineOperand shift(std::string mnemonic, unsigned amount);
    static AArch64MachineOperand stack_pointer(bool use_64bit = true);
    static AArch64MachineOperand memory_address_virtual_reg(
        const AArch64VirtualReg &reg,
        std::optional<long long> immediate_offset = std::nullopt,
        AArch64MachineMemoryAddressOperand::AddressMode address_mode =
            AArch64MachineMemoryAddressOperand::AddressMode::Offset);
    static AArch64MachineOperand memory_address_virtual_reg(
        const AArch64VirtualReg &reg, AArch64MachineSymbolReference symbolic_offset,
        AArch64MachineMemoryAddressOperand::AddressMode address_mode =
            AArch64MachineMemoryAddressOperand::AddressMode::Offset);
    static AArch64MachineOperand memory_address_physical_reg(
        unsigned reg_number,
        std::optional<long long> immediate_offset = std::nullopt,
        AArch64MachineMemoryAddressOperand::AddressMode address_mode =
            AArch64MachineMemoryAddressOperand::AddressMode::Offset);
    static AArch64MachineOperand memory_address_physical_reg(
        unsigned reg_number, AArch64MachineSymbolReference symbolic_offset,
        AArch64MachineMemoryAddressOperand::AddressMode address_mode =
            AArch64MachineMemoryAddressOperand::AddressMode::Offset);
    static AArch64MachineOperand memory_address_stack_pointer(
        std::optional<long long> immediate_offset = std::nullopt,
        bool use_64bit = true,
        AArch64MachineMemoryAddressOperand::AddressMode address_mode =
            AArch64MachineMemoryAddressOperand::AddressMode::Offset);
    static AArch64MachineOperand memory_address_stack_pointer(
        AArch64MachineSymbolReference symbolic_offset, bool use_64bit = true,
        AArch64MachineMemoryAddressOperand::AddressMode address_mode =
            AArch64MachineMemoryAddressOperand::AddressMode::Offset);

    AArch64MachineOperandKind get_kind() const noexcept { return kind_; }
    bool is_virtual_reg() const noexcept {
        return kind_ == AArch64MachineOperandKind::VirtualReg;
    }
    bool is_physical_reg() const noexcept {
        return kind_ == AArch64MachineOperandKind::PhysicalReg;
    }
    bool is_immediate() const noexcept {
        return kind_ == AArch64MachineOperandKind::Immediate;
    }
    bool is_symbol() const noexcept {
        return kind_ == AArch64MachineOperandKind::Symbol;
    }
    bool is_label() const noexcept { return kind_ == AArch64MachineOperandKind::Label; }
    bool is_condition_code() const noexcept {
        return kind_ == AArch64MachineOperandKind::ConditionCode;
    }
    bool is_zero_register() const noexcept {
        return kind_ == AArch64MachineOperandKind::ZeroRegister;
    }
    bool is_shift() const noexcept { return kind_ == AArch64MachineOperandKind::Shift; }
    bool is_stack_pointer() const noexcept {
        return kind_ == AArch64MachineOperandKind::StackPointer;
    }
    bool is_memory_address() const noexcept {
        return kind_ == AArch64MachineOperandKind::MemoryAddress;
    }

    const AArch64MachineVirtualRegOperand *
    get_virtual_reg_operand() const noexcept;
    const AArch64MachinePhysicalRegOperand *
    get_physical_reg_operand() const noexcept;
    const AArch64MachineConditionCodeOperand *
    get_condition_code_operand() const noexcept;
    const AArch64MachineImmediateOperand *get_immediate_operand() const noexcept;
    const AArch64MachineSymbolOperand *get_symbol_operand() const noexcept;
    const AArch64MachineLabelOperand *get_label_operand() const noexcept;
    const AArch64MachineZeroRegisterOperand *
    get_zero_register_operand() const noexcept;
    const AArch64MachineShiftOperand *get_shift_operand() const noexcept;
    const AArch64MachineStackPointerOperand *
    get_stack_pointer_operand() const noexcept;
    const AArch64MachineMemoryAddressOperand *
    get_memory_address_operand() const noexcept;
};

class AArch64MachineInstr {
  private:
    std::string mnemonic_;
    std::vector<AArch64MachineOperand> operands_;
    AArch64InstructionFlags flags_;
    std::optional<AArch64DebugLocation> debug_location_;
    std::vector<std::size_t> explicit_defs_;
    std::vector<std::size_t> explicit_uses_;
    std::vector<std::size_t> implicit_defs_;
    std::vector<std::size_t> implicit_uses_;
    std::optional<AArch64CallClobberMask> call_clobber_mask_;

  public:
    AArch64MachineInstr(std::string mnemonic,
                        std::vector<AArch64MachineOperand> operands,
                        AArch64InstructionFlags flags = {},
                        std::optional<AArch64DebugLocation> debug_location =
                            std::nullopt,
                        std::vector<std::size_t> implicit_defs = {},
                        std::vector<std::size_t> implicit_uses = {},
                        std::optional<AArch64CallClobberMask> call_clobber_mask =
                            std::nullopt);
    AArch64MachineInstr(std::string mnemonic,
                        std::vector<AArch64MachineOperand> operands,
                        AArch64InstructionFlags flags,
                        std::vector<std::size_t> implicit_defs,
                        std::vector<std::size_t> implicit_uses,
                        std::optional<AArch64CallClobberMask> call_clobber_mask);

    const std::string &get_mnemonic() const noexcept { return mnemonic_; }
    const std::vector<AArch64MachineOperand> &get_operands() const noexcept {
        return operands_;
    }
    std::vector<AArch64MachineOperand> &get_operands() noexcept { return operands_; }
    const AArch64InstructionFlags &get_flags() const noexcept { return flags_; }
    const std::vector<std::size_t> &get_explicit_defs() const noexcept {
        return explicit_defs_;
    }
    const std::vector<std::size_t> &get_explicit_uses() const noexcept {
        return explicit_uses_;
    }
    const std::vector<std::size_t> &get_implicit_defs() const noexcept {
        return implicit_defs_;
    }
    const std::vector<std::size_t> &get_implicit_uses() const noexcept {
        return implicit_uses_;
    }
    const std::optional<AArch64CallClobberMask> &
    get_call_clobber_mask() const noexcept {
        return call_clobber_mask_;
    }
    const std::optional<AArch64DebugLocation> &get_debug_location() const noexcept {
        return debug_location_;
    }
    void set_debug_location(AArch64DebugLocation debug_location) {
        debug_location_ = std::move(debug_location);
    }
};

class AArch64MachineBlock {
  private:
    std::string label_;
    std::vector<AArch64MachineInstr> instructions_;
    std::optional<AArch64DebugLocation> pending_debug_location_;

  public:
    explicit AArch64MachineBlock(std::string label) : label_(std::move(label)) {}

    const std::string &get_label() const noexcept { return label_; }
    const std::vector<AArch64MachineInstr> &get_instructions() const noexcept {
        return instructions_;
    }
    std::vector<AArch64MachineInstr> &get_instructions() noexcept {
        return instructions_;
    }
    void set_pending_debug_location(AArch64DebugLocation debug_location) {
        pending_debug_location_ = std::move(debug_location);
    }

    void append_instruction(AArch64MachineInstr instruction) {
        if (pending_debug_location_.has_value() &&
            (instruction.get_mnemonic().empty() ||
             instruction.get_mnemonic().front() != '.')) {
            if (!instruction.get_debug_location().has_value()) {
                instruction.set_debug_location(*pending_debug_location_);
            }
            pending_debug_location_.reset();
        }
        instructions_.push_back(std::move(instruction));
    }
};

class AArch64FunctionFrameInfo {
  private:
    std::unordered_map<const CoreIrStackSlot *, std::size_t> stack_slot_offsets_;
    std::unordered_map<std::size_t, std::size_t> virtual_reg_spill_offsets_;
    std::unordered_map<unsigned, std::size_t> saved_physical_reg_offsets_;
    std::unordered_map<unsigned, std::size_t> borrowed_scratch_reg_offsets_;
    std::set<unsigned> saved_physical_regs_;
    std::size_t local_size_ = 0;
    std::size_t frame_size_ = 0;

  public:
    void set_stack_slot_offset(const CoreIrStackSlot *stack_slot,
                               std::size_t offset) {
        stack_slot_offsets_[stack_slot] = offset;
    }

    std::size_t get_stack_slot_offset(const CoreIrStackSlot *stack_slot) const {
        return stack_slot_offsets_.at(stack_slot);
    }
    void set_virtual_reg_spill_offset(std::size_t id, std::size_t offset) {
        virtual_reg_spill_offsets_[id] = offset;
    }
    std::optional<std::size_t> get_virtual_reg_spill_offset(std::size_t id) const {
        const auto it = virtual_reg_spill_offsets_.find(id);
        if (it == virtual_reg_spill_offsets_.end()) {
            return std::nullopt;
        }
        return it->second;
    }
    void mark_saved_physical_reg(unsigned reg) { saved_physical_regs_.insert(reg); }
    const std::set<unsigned> &get_saved_physical_regs() const noexcept {
        return saved_physical_regs_;
    }
    bool has_saved_physical_reg_offset(unsigned reg) const noexcept {
        return saved_physical_reg_offsets_.find(reg) != saved_physical_reg_offsets_.end();
    }
    void set_saved_physical_reg_offset(unsigned reg, std::size_t offset) {
        saved_physical_reg_offsets_[reg] = offset;
    }
    std::size_t get_saved_physical_reg_offset(unsigned reg) const {
        return saved_physical_reg_offsets_.at(reg);
    }
    bool has_borrowed_scratch_reg_offset(unsigned reg) const noexcept {
        return borrowed_scratch_reg_offsets_.find(reg) !=
               borrowed_scratch_reg_offsets_.end();
    }
    void set_borrowed_scratch_reg_offset(unsigned reg, std::size_t offset) {
        borrowed_scratch_reg_offsets_[reg] = offset;
    }
    std::size_t get_borrowed_scratch_reg_offset(unsigned reg) const {
        return borrowed_scratch_reg_offsets_.at(reg);
    }
    void set_local_size(std::size_t local_size) noexcept { local_size_ = local_size; }
    std::size_t get_local_size() const noexcept { return local_size_; }
    void set_frame_size(std::size_t frame_size) noexcept { frame_size_ = frame_size; }
    std::size_t get_frame_size() const noexcept { return frame_size_; }
};

class AArch64MachineFunction {
  private:
    std::string name_;
    bool is_global_symbol_ = false;
    AArch64SectionKind section_kind_ = AArch64SectionKind::Text;
    std::string epilogue_label_;
    AArch64FunctionFrameInfo frame_info_;
    AArch64FrameRecord frame_record_;
    std::vector<AArch64MachineBlock> blocks_;
    std::size_t next_virtual_reg_id_ = 1;
    std::unordered_map<std::size_t, AArch64VirtualRegKind> virtual_reg_kinds_;
    std::unordered_map<std::size_t, unsigned> virtual_reg_allocation_;

  public:
    AArch64MachineFunction(std::string name, bool is_global_symbol,
                           std::string epilogue_label)
        : name_(std::move(name)),
          is_global_symbol_(is_global_symbol),
          epilogue_label_(std::move(epilogue_label)) {}

    const std::string &get_name() const noexcept { return name_; }
    bool get_is_global_symbol() const noexcept { return is_global_symbol_; }
    AArch64SectionKind get_section_kind() const noexcept { return section_kind_; }
    void set_section_kind(AArch64SectionKind section_kind) noexcept {
        section_kind_ = section_kind;
    }
    const std::string &get_epilogue_label() const noexcept { return epilogue_label_; }
    AArch64FunctionFrameInfo &get_frame_info() noexcept { return frame_info_; }
    const AArch64FunctionFrameInfo &get_frame_info() const noexcept {
        return frame_info_;
    }
    AArch64FrameRecord &get_frame_record() noexcept { return frame_record_; }
    const AArch64FrameRecord &get_frame_record() const noexcept {
        return frame_record_;
    }
    std::vector<AArch64MachineBlock> &get_blocks() noexcept { return blocks_; }
    const std::vector<AArch64MachineBlock> &get_blocks() const noexcept {
        return blocks_;
    }
    AArch64MachineBlock &append_block(std::string label) {
        blocks_.emplace_back(std::move(label));
        return blocks_.back();
    }
    AArch64VirtualReg create_virtual_reg(AArch64VirtualRegKind kind) {
        const std::size_t id = next_virtual_reg_id_++;
        virtual_reg_kinds_[id] = kind;
        return AArch64VirtualReg(id, kind);
    }
    AArch64VirtualReg create_virtual_reg(bool use_64bit) {
        return create_virtual_reg(use_64bit ? AArch64VirtualRegKind::General64
                                            : AArch64VirtualRegKind::General32);
    }
    AArch64VirtualRegKind get_virtual_reg_kind(std::size_t id) const noexcept {
        const auto it = virtual_reg_kinds_.find(id);
        return it != virtual_reg_kinds_.end() ? it->second
                                              : AArch64VirtualRegKind::General32;
    }
    bool get_virtual_reg_use_64bit(std::size_t id) const noexcept {
        return get_virtual_reg_kind(id) == AArch64VirtualRegKind::General64;
    }
    const std::unordered_map<std::size_t, AArch64VirtualRegKind> &
    get_virtual_reg_kinds() const noexcept {
        return virtual_reg_kinds_;
    }
    void set_virtual_reg_allocation(std::size_t id, unsigned physical_reg) {
        virtual_reg_allocation_[id] = physical_reg;
    }
    const std::unordered_map<std::size_t, unsigned> &
    get_virtual_reg_allocation() const noexcept {
        return virtual_reg_allocation_;
    }
    std::optional<unsigned> get_physical_reg_for_virtual(std::size_t id) const {
        const auto it = virtual_reg_allocation_.find(id);
        if (it == virtual_reg_allocation_.end()) {
            return std::nullopt;
        }
        return it->second;
    }
};

} // namespace sysycc
