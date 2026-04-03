#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backend/ir/core/ir_instruction.hpp"

namespace sysycc {

class CoreIrFunction;

class CoreIrBasicBlock {
  private:
    std::string name_;
    CoreIrFunction *parent_ = nullptr;
    std::vector<std::unique_ptr<CoreIrInstruction>> instructions_;

  public:
    explicit CoreIrBasicBlock(std::string name) : name_(std::move(name)) {}

    const std::string &get_name() const noexcept { return name_; }

    CoreIrFunction *get_parent() const noexcept { return parent_; }

    void set_parent(CoreIrFunction *parent) noexcept { parent_ = parent; }

    const std::vector<std::unique_ptr<CoreIrInstruction>> &
    get_instructions() const noexcept {
        return instructions_;
    }

    std::vector<std::unique_ptr<CoreIrInstruction>> &get_instructions() noexcept {
        return instructions_;
    }

    CoreIrInstruction *append_instruction(
        std::unique_ptr<CoreIrInstruction> instruction) {
        if (instruction == nullptr) {
            return nullptr;
        }
        instruction->set_parent(this);
        CoreIrInstruction *instruction_ptr = instruction.get();
        instructions_.push_back(std::move(instruction));
        return instruction_ptr;
    }

    template <typename T, typename... Args>
    T *create_instruction(Args &&...args) {
        auto instruction =
            std::make_unique<T>(std::forward<Args>(args)...);
        T *instruction_ptr = instruction.get();
        append_instruction(std::move(instruction));
        return instruction_ptr;
    }

    bool get_has_terminator() const noexcept {
        return !instructions_.empty() &&
               instructions_.back()->get_is_terminator();
    }
};

} // namespace sysycc
