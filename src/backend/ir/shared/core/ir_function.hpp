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
    bool is_readnone_ = false;
    bool is_readonly_ = false;
    bool is_writeonly_ = false;
    bool is_norecurse_ = false;
    CoreIrModule *parent_ = nullptr;
    std::vector<std::unique_ptr<CoreIrParameter>> parameters_;
    std::vector<std::unique_ptr<CoreIrStackSlot>> stack_slots_;
    std::vector<std::unique_ptr<CoreIrBasicBlock>> basic_blocks_;
    std::vector<bool> parameter_nocapture_;
    std::vector<bool> parameter_readonly_;

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

    void set_function_type(const CoreIrFunctionType *function_type) noexcept {
        function_type_ = function_type;
    }

    bool get_is_internal_linkage() const noexcept {
        return is_internal_linkage_;
    }

    void set_is_internal_linkage(bool is_internal_linkage) noexcept {
        is_internal_linkage_ = is_internal_linkage;
    }

    bool get_is_always_inline() const noexcept { return is_always_inline_; }

    void set_is_always_inline(bool is_always_inline) noexcept {
        is_always_inline_ = is_always_inline;
    }

    bool get_is_readnone() const noexcept { return is_readnone_; }
    void set_is_readnone(bool is_readnone) noexcept { is_readnone_ = is_readnone; }

    bool get_is_readonly() const noexcept { return is_readonly_; }
    void set_is_readonly(bool is_readonly) noexcept { is_readonly_ = is_readonly; }

    bool get_is_writeonly() const noexcept { return is_writeonly_; }
    void set_is_writeonly(bool is_writeonly) noexcept {
        is_writeonly_ = is_writeonly;
    }

    bool get_is_norecurse() const noexcept { return is_norecurse_; }
    void set_is_norecurse(bool is_norecurse) noexcept {
        is_norecurse_ = is_norecurse;
    }

    const std::vector<bool> &get_parameter_nocapture() const noexcept {
        return parameter_nocapture_;
    }

    void set_parameter_nocapture(std::vector<bool> parameter_nocapture) {
        parameter_nocapture_ = std::move(parameter_nocapture);
    }

    const std::vector<bool> &get_parameter_readonly() const noexcept {
        return parameter_readonly_;
    }

    void set_parameter_readonly(std::vector<bool> parameter_readonly) {
        parameter_readonly_ = std::move(parameter_readonly);
    }

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
