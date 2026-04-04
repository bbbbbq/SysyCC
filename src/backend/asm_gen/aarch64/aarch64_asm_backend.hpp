#pragma once

#include <cstddef>
#include <memory>
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

class AArch64MachineInstr {
  private:
    std::string text_;

  public:
    explicit AArch64MachineInstr(std::string text) : text_(std::move(text)) {}

    const std::string &get_text() const noexcept { return text_; }
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

    void append_instruction(std::string text) {
        instructions_.emplace_back(std::move(text));
    }
};

class AArch64FunctionFrameInfo {
  private:
    std::unordered_map<const CoreIrStackSlot *, std::size_t> stack_slot_offsets_;
    std::unordered_map<const CoreIrValue *, std::size_t> value_offsets_;
    std::size_t local_size_ = 0;
    std::size_t frame_size_ = 0;

  public:
    void set_stack_slot_offset(const CoreIrStackSlot *stack_slot,
                               std::size_t offset) {
        stack_slot_offsets_[stack_slot] = offset;
    }

    void set_value_offset(const CoreIrValue *value, std::size_t offset) {
        value_offsets_[value] = offset;
    }

    std::size_t get_stack_slot_offset(const CoreIrStackSlot *stack_slot) const;
    std::size_t get_value_offset(const CoreIrValue *value) const;
    bool has_value_offset(const CoreIrValue *value) const;

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
