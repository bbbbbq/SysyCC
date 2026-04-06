#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"

namespace sysycc {

class CoreIrFunctionType;
class CoreIrFunction;
class CoreIrModule;
class CoreIrType;

class CoreIrParameter : public CoreIrValue {
  private:
    CoreIrFunction *parent_ = nullptr;

  public:
    CoreIrParameter(const CoreIrType *type, std::string name)
        : CoreIrValue(type, std::move(name)) {}

    CoreIrFunction *get_parent() const noexcept { return parent_; }

    void set_parent(CoreIrFunction *parent) noexcept { parent_ = parent; }
};

class CoreIrFunction {
  private:
    std::string name_;
    const CoreIrFunctionType *function_type_ = nullptr;
    bool is_internal_linkage_ = false;
    bool is_always_inline_ = false;
    CoreIrModule *parent_ = nullptr;
    std::vector<std::unique_ptr<CoreIrParameter>> parameters_;
    std::vector<std::unique_ptr<CoreIrStackSlot>> stack_slots_;
    std::vector<std::unique_ptr<CoreIrBasicBlock>> basic_blocks_;

  public:
    CoreIrFunction(std::string name, const CoreIrFunctionType *function_type,
                   bool is_internal_linkage, bool is_always_inline = false)
        : name_(std::move(name)),
          function_type_(function_type),
          is_internal_linkage_(is_internal_linkage),
          is_always_inline_(is_always_inline) {}

    const std::string &get_name() const noexcept { return name_; }

    CoreIrModule *get_parent() const noexcept { return parent_; }

    void set_parent(CoreIrModule *parent) noexcept { parent_ = parent; }

    const CoreIrFunctionType *get_function_type() const noexcept {
        return function_type_;
    }

    bool get_is_internal_linkage() const noexcept {
        return is_internal_linkage_;
    }

    bool get_is_always_inline() const noexcept { return is_always_inline_; }

    bool get_is_variadic() const noexcept {
        return function_type_ != nullptr && function_type_->get_is_variadic();
    }

    const std::vector<std::unique_ptr<CoreIrParameter>> &
    get_parameters() const noexcept {
        return parameters_;
    }

    std::vector<std::unique_ptr<CoreIrParameter>> &get_parameters() noexcept {
        return parameters_;
    }

    const std::vector<std::unique_ptr<CoreIrStackSlot>> &
    get_stack_slots() const noexcept {
        return stack_slots_;
    }

    std::vector<std::unique_ptr<CoreIrStackSlot>> &get_stack_slots() noexcept {
        return stack_slots_;
    }

    const std::vector<std::unique_ptr<CoreIrBasicBlock>> &
    get_basic_blocks() const noexcept {
        return basic_blocks_;
    }

    std::vector<std::unique_ptr<CoreIrBasicBlock>> &get_basic_blocks() noexcept {
        return basic_blocks_;
    }

    CoreIrParameter *append_parameter(std::unique_ptr<CoreIrParameter> parameter) {
        if (parameter == nullptr) {
            return nullptr;
        }
        parameter->set_parent(this);
        CoreIrParameter *parameter_ptr = parameter.get();
        parameters_.push_back(std::move(parameter));
        return parameter_ptr;
    }

    CoreIrStackSlot *append_stack_slot(
        std::unique_ptr<CoreIrStackSlot> stack_slot) {
        if (stack_slot == nullptr) {
            return nullptr;
        }
        stack_slot->set_parent(this);
        CoreIrStackSlot *stack_slot_ptr = stack_slot.get();
        stack_slots_.push_back(std::move(stack_slot));
        return stack_slot_ptr;
    }

    CoreIrBasicBlock *append_basic_block(
        std::unique_ptr<CoreIrBasicBlock> basic_block) {
        if (basic_block == nullptr) {
            return nullptr;
        }
        basic_block->set_parent(this);
        CoreIrBasicBlock *basic_block_ptr = basic_block.get();
        basic_blocks_.push_back(std::move(basic_block));
        return basic_block_ptr;
    }

    template <typename T, typename... Args>
    T *create_parameter(Args &&...args) {
        auto parameter = std::make_unique<T>(std::forward<Args>(args)...);
        T *parameter_ptr = parameter.get();
        append_parameter(std::move(parameter));
        return parameter_ptr;
    }

    template <typename T, typename... Args>
    T *create_stack_slot(Args &&...args) {
        auto stack_slot = std::make_unique<T>(std::forward<Args>(args)...);
        T *stack_slot_ptr = stack_slot.get();
        append_stack_slot(std::move(stack_slot));
        return stack_slot_ptr;
    }

    template <typename T, typename... Args>
    T *create_basic_block(Args &&...args) {
        auto basic_block = std::make_unique<T>(std::forward<Args>(args)...);
        T *basic_block_ptr = basic_block.get();
        append_basic_block(std::move(basic_block));
        return basic_block_ptr;
    }
};

} // namespace sysycc
