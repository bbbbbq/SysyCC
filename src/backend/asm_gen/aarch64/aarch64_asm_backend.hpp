#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "backend/asm_gen/asm_result.hpp"
#include "backend/asm_gen/backend_options.hpp"

namespace sysycc {

class CoreIrModule;
class CoreIrStackSlot;
class CoreIrValue;
class DiagnosticEngine;

enum class AArch64PhysicalReg : unsigned {
    X0 = 0,
    X1 = 1,
    X2 = 2,
    X3 = 3,
    X4 = 4,
    X5 = 5,
    X6 = 6,
    X7 = 7,
    X8 = 8,
    X9 = 9,
    X10 = 10,
    X11 = 11,
    X12 = 12,
    X13 = 13,
    X14 = 14,
    X15 = 15,
    X16 = 16,
    X17 = 17,
    X18 = 18,
    X19 = 19,
    X20 = 20,
    X21 = 21,
    X22 = 22,
    X23 = 23,
    X24 = 24,
    X25 = 25,
    X26 = 26,
    X27 = 27,
    X28 = 28,
    X29 = 29,
    X30 = 30,
};

enum class AArch64RegClass {
    CallerSavedGeneral,
    CalleeSavedGeneral,
    SpillScratchGeneral,
};

class AArch64CallClobberMask {
  private:
    std::set<unsigned> regs_;

  public:
    AArch64CallClobberMask() = default;
    explicit AArch64CallClobberMask(std::set<unsigned> regs)
        : regs_(std::move(regs)) {}

    const std::set<unsigned> &get_regs() const noexcept { return regs_; }
    bool clobbers(unsigned reg) const noexcept {
        return regs_.find(reg) != regs_.end();
    }
};

struct AArch64InstructionFlags {
    bool is_call = false;
};

class AArch64VirtualReg {
  private:
    std::size_t id_ = 0;
    bool use_64bit_ = false;

  public:
    AArch64VirtualReg() = default;
    AArch64VirtualReg(std::size_t id, bool use_64bit)
        : id_(id), use_64bit_(use_64bit) {}

    std::size_t get_id() const noexcept { return id_; }
    bool get_use_64bit() const noexcept { return use_64bit_; }
    bool is_valid() const noexcept { return id_ != 0; }
};

class AArch64MachineOperand {
  private:
    std::string text_;

  public:
    explicit AArch64MachineOperand(std::string text) : text_(std::move(text)) {}

    const std::string &get_text() const noexcept { return text_; }
};

class AArch64MachineInstr {
  private:
    std::string mnemonic_;
    std::vector<AArch64MachineOperand> operands_;
    AArch64InstructionFlags flags_;
    std::vector<std::size_t> explicit_defs_;
    std::vector<std::size_t> explicit_uses_;
    std::vector<std::size_t> implicit_defs_;
    std::vector<std::size_t> implicit_uses_;
    std::optional<AArch64CallClobberMask> call_clobber_mask_;

  public:
    explicit AArch64MachineInstr(std::string text);
    AArch64MachineInstr(std::string mnemonic,
                        std::vector<AArch64MachineOperand> operands,
                        AArch64InstructionFlags flags = {},
                        std::vector<std::size_t> implicit_defs = {},
                        std::vector<std::size_t> implicit_uses = {},
                        std::optional<AArch64CallClobberMask> call_clobber_mask =
                            std::nullopt);

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
};

class AArch64MachineBlock {
  private:
    std::string label_;
    std::vector<AArch64MachineInstr> instructions_;

  public:
    explicit AArch64MachineBlock(std::string label) : label_(std::move(label)) {}

    const std::string &get_label() const noexcept { return label_; }

    const std::vector<AArch64MachineInstr> &get_instructions() const noexcept {
        return instructions_;
    }
    std::vector<AArch64MachineInstr> &get_instructions() noexcept {
        return instructions_;
    }

    void append_instruction(std::string text) {
        instructions_.emplace_back(std::move(text));
    }

    void append_instruction(AArch64MachineInstr instruction) {
        instructions_.push_back(std::move(instruction));
    }
};

class AArch64FunctionFrameInfo {
  private:
    std::unordered_map<const CoreIrStackSlot *, std::size_t> stack_slot_offsets_;
    std::unordered_map<std::size_t, std::size_t> virtual_reg_spill_offsets_;
    std::unordered_map<unsigned, std::size_t> saved_physical_reg_offsets_;
    std::set<unsigned> saved_physical_regs_;
    std::size_t local_size_ = 0;
    std::size_t frame_size_ = 0;

  public:
    void set_stack_slot_offset(const CoreIrStackSlot *stack_slot,
                               std::size_t offset) {
        stack_slot_offsets_[stack_slot] = offset;
    }

    std::size_t get_stack_slot_offset(const CoreIrStackSlot *stack_slot) const;
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

    void set_local_size(std::size_t local_size) noexcept { local_size_ = local_size; }
    std::size_t get_local_size() const noexcept { return local_size_; }

    void set_frame_size(std::size_t frame_size) noexcept {
        frame_size_ = frame_size;
    }

    std::size_t get_frame_size() const noexcept { return frame_size_; }
};

class AArch64MachineFunction {
  private:
    std::string name_;
    bool is_global_symbol_ = false;
    std::string epilogue_label_;
    AArch64FunctionFrameInfo frame_info_;
    std::vector<AArch64MachineBlock> blocks_;
    std::size_t next_virtual_reg_id_ = 1;
    std::unordered_map<std::size_t, bool> virtual_reg_widths_;
    std::unordered_map<std::size_t, unsigned> virtual_reg_allocation_;

  public:
    AArch64MachineFunction(std::string name, bool is_global_symbol,
                           std::string epilogue_label)
        : name_(std::move(name)),
          is_global_symbol_(is_global_symbol),
          epilogue_label_(std::move(epilogue_label)) {}

    const std::string &get_name() const noexcept { return name_; }
    bool get_is_global_symbol() const noexcept { return is_global_symbol_; }
    const std::string &get_epilogue_label() const noexcept { return epilogue_label_; }

    AArch64FunctionFrameInfo &get_frame_info() noexcept { return frame_info_; }
    const AArch64FunctionFrameInfo &get_frame_info() const noexcept {
        return frame_info_;
    }

    std::vector<AArch64MachineBlock> &get_blocks() noexcept { return blocks_; }
    const std::vector<AArch64MachineBlock> &get_blocks() const noexcept {
        return blocks_;
    }

    AArch64MachineBlock &append_block(std::string label) {
        blocks_.emplace_back(std::move(label));
        return blocks_.back();
    }

    AArch64VirtualReg create_virtual_reg(bool use_64bit) {
        const std::size_t id = next_virtual_reg_id_++;
        virtual_reg_widths_[id] = use_64bit;
        return AArch64VirtualReg(id, use_64bit);
    }

    bool get_virtual_reg_use_64bit(std::size_t id) const noexcept {
        const auto it = virtual_reg_widths_.find(id);
        return it != virtual_reg_widths_.end() ? it->second : false;
    }

    const std::unordered_map<std::size_t, bool> &
    get_virtual_reg_widths() const noexcept {
        return virtual_reg_widths_;
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

class AArch64MachineModule {
  private:
    std::vector<std::string> global_lines_;
    std::vector<AArch64MachineFunction> functions_;

  public:
    const std::vector<std::string> &get_global_lines() const noexcept {
        return global_lines_;
    }

    void append_global_line(std::string line) {
        global_lines_.push_back(std::move(line));
    }

    std::vector<AArch64MachineFunction> &get_functions() noexcept {
        return functions_;
    }

    const std::vector<AArch64MachineFunction> &get_functions() const noexcept {
        return functions_;
    }

    AArch64MachineFunction &append_function(std::string name, bool is_global_symbol,
                                            std::string epilogue_label) {
        functions_.emplace_back(std::move(name), is_global_symbol,
                                std::move(epilogue_label));
        return functions_.back();
    }
};

class AArch64AsmPrinter {
  public:
    std::string print_module(const AArch64MachineModule &module) const;
};

class AArch64AsmBackend {
  public:
    std::unique_ptr<AsmResult>
    Generate(const CoreIrModule &module, const BackendOptions &backend_options,
             DiagnosticEngine &diagnostic_engine) const;
};

} // namespace sysycc
