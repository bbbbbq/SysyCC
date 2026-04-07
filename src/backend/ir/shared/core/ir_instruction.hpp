#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/core/ir_value.hpp"

namespace sysycc {

class CoreIrBasicBlock;

enum class CoreIrOpcode : unsigned char {
    Phi,
    Binary,
    Unary,
    Compare,
    Cast,
    AddressOfFunction,
    AddressOfGlobal,
    AddressOfStackSlot,
    GetElementPtr,
    Load,
    Store,
    Call,
    Jump,
    CondJump,
    Return,
};

enum class CoreIrBinaryOpcode : unsigned char {
    Add,
    Sub,
    Mul,
    SDiv,
    UDiv,
    SRem,
    URem,
    And,
    Or,
    Xor,
    Shl,
    LShr,
    AShr,
};

enum class CoreIrUnaryOpcode : unsigned char {
    Negate,
    BitwiseNot,
    LogicalNot,
};

enum class CoreIrComparePredicate : unsigned char {
    Equal,
    NotEqual,
    SignedLess,
    SignedLessEqual,
    SignedGreater,
    SignedGreaterEqual,
    UnsignedLess,
    UnsignedLessEqual,
    UnsignedGreater,
    UnsignedGreaterEqual,
};

enum class CoreIrCastKind : unsigned char {
    SignExtend,
    ZeroExtend,
    Truncate,
    SignedIntToFloat,
    UnsignedIntToFloat,
    FloatToSignedInt,
    FloatToUnsignedInt,
    FloatExtend,
    FloatTruncate,
    PtrToInt,
    IntToPtr,
};

class CoreIrStackSlot;
class CoreIrGlobal;
class CoreIrFunction;

class CoreIrInstruction : public CoreIrValue {
  private:
    CoreIrOpcode opcode_;
    CoreIrBasicBlock *parent_ = nullptr;
    std::vector<CoreIrValue *> operands_;

  protected:
    void append_operand(CoreIrValue *operand) {
        if (operand != nullptr) {
            operand->add_use(this, operands_.size());
        }
        operands_.push_back(operand);
    }

    void erase_operand(std::size_t index) {
        if (index >= operands_.size()) {
            return;
        }
        if (operands_[index] != nullptr) {
            operands_[index]->remove_use(this, index);
        }
        for (std::size_t shifted = index + 1; shifted < operands_.size();
             ++shifted) {
            if (operands_[shifted] != nullptr) {
                operands_[shifted]->remove_use(this, shifted);
                operands_[shifted]->add_use(this, shifted - 1);
            }
        }
        operands_.erase(operands_.begin() + static_cast<std::ptrdiff_t>(index));
    }

  public:
    CoreIrInstruction(CoreIrOpcode opcode, const CoreIrType *type,
                      std::string name = {})
        : CoreIrValue(type, std::move(name)), opcode_(opcode) {}

    CoreIrOpcode get_opcode() const noexcept { return opcode_; }

    CoreIrBasicBlock *get_parent() const noexcept { return parent_; }

    void set_parent(CoreIrBasicBlock *parent) noexcept { parent_ = parent; }

    const std::vector<CoreIrValue *> &get_operands() const noexcept {
        return operands_;
    }

    void set_operand(std::size_t index, CoreIrValue *operand) {
        if (index >= operands_.size()) {
            return;
        }
        if (operands_[index] != nullptr) {
            operands_[index]->remove_use(this, index);
        }
        operands_[index] = operand;
        if (operand != nullptr) {
            operand->add_use(this, index);
        }
    }

    void detach_operands() {
        for (std::size_t index = 0; index < operands_.size(); ++index) {
            if (operands_[index] != nullptr) {
                operands_[index]->remove_use(this, index);
            }
        }
        operands_.clear();
    }

    void replace_all_uses_with(CoreIrValue *replacement) {
        const std::vector<CoreIrUse> uses = get_uses();
        for (const CoreIrUse &use : uses) {
            if (use.get_user() != nullptr) {
                use.get_user()->set_operand(use.get_operand_index(), replacement);
            }
        }
    }

    virtual bool get_has_side_effect() const noexcept = 0;
    virtual bool get_is_terminator() const noexcept = 0;
};

class CoreIrPhiInst final : public CoreIrInstruction {
  private:
    std::vector<CoreIrBasicBlock *> incoming_blocks_;

  public:
    CoreIrPhiInst(const CoreIrType *type, std::string name = {})
        : CoreIrInstruction(CoreIrOpcode::Phi, type, std::move(name)) {}

    std::size_t get_incoming_count() const noexcept {
        return incoming_blocks_.size();
    }

    CoreIrBasicBlock *get_incoming_block(std::size_t index) const noexcept {
        if (index >= incoming_blocks_.size()) {
            return nullptr;
        }
        return incoming_blocks_[index];
    }

    CoreIrValue *get_incoming_value(std::size_t index) const noexcept {
        if (index >= get_operands().size()) {
            return nullptr;
        }
        return get_operands()[index];
    }

    void add_incoming(CoreIrBasicBlock *block, CoreIrValue *value) {
        incoming_blocks_.push_back(block);
        append_operand(value);
    }

    void set_incoming_value(std::size_t index, CoreIrValue *value) {
        set_operand(index, value);
    }

    void set_incoming_block(std::size_t index, CoreIrBasicBlock *block) {
        if (index >= incoming_blocks_.size()) {
            return;
        }
        incoming_blocks_[index] = block;
    }

    void remove_incoming_block(CoreIrBasicBlock *block) {
        for (std::size_t index = 0; index < incoming_blocks_.size();) {
            if (incoming_blocks_[index] != block) {
                ++index;
                continue;
            }
            incoming_blocks_.erase(
                incoming_blocks_.begin() + static_cast<std::ptrdiff_t>(index));
            erase_operand(index);
        }
    }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrBinaryInst final : public CoreIrInstruction {
  private:
    CoreIrBinaryOpcode binary_opcode_;

  public:
    CoreIrBinaryInst(CoreIrBinaryOpcode binary_opcode, const CoreIrType *type,
                     std::string name, CoreIrValue *lhs, CoreIrValue *rhs)
        : CoreIrInstruction(CoreIrOpcode::Binary, type, std::move(name)),
          binary_opcode_(binary_opcode) {
        append_operand(lhs);
        append_operand(rhs);
    }

    CoreIrBinaryOpcode get_binary_opcode() const noexcept {
        return binary_opcode_;
    }

    CoreIrValue *get_lhs() const noexcept {
        return get_operands().empty() ? nullptr : get_operands()[0];
    }

    CoreIrValue *get_rhs() const noexcept {
        return get_operands().size() < 2 ? nullptr : get_operands()[1];
    }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrUnaryInst final : public CoreIrInstruction {
  private:
    CoreIrUnaryOpcode unary_opcode_;

  public:
    CoreIrUnaryInst(CoreIrUnaryOpcode unary_opcode, const CoreIrType *type,
                    std::string name, CoreIrValue *operand)
        : CoreIrInstruction(CoreIrOpcode::Unary, type, std::move(name)),
          unary_opcode_(unary_opcode) {
        append_operand(operand);
    }

    CoreIrUnaryOpcode get_unary_opcode() const noexcept { return unary_opcode_; }

    CoreIrValue *get_operand() const noexcept {
        return get_operands().empty() ? nullptr : get_operands()[0];
    }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrCompareInst final : public CoreIrInstruction {
  private:
    CoreIrComparePredicate predicate_;

  public:
    CoreIrCompareInst(CoreIrComparePredicate predicate, const CoreIrType *type,
                      std::string name, CoreIrValue *lhs, CoreIrValue *rhs)
        : CoreIrInstruction(CoreIrOpcode::Compare, type, std::move(name)),
          predicate_(predicate) {
        append_operand(lhs);
        append_operand(rhs);
    }

    CoreIrComparePredicate get_predicate() const noexcept { return predicate_; }

    void set_predicate(CoreIrComparePredicate predicate) noexcept {
        predicate_ = predicate;
    }

    CoreIrValue *get_lhs() const noexcept {
        return get_operands().empty() ? nullptr : get_operands()[0];
    }

    CoreIrValue *get_rhs() const noexcept {
        return get_operands().size() < 2 ? nullptr : get_operands()[1];
    }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrCastInst final : public CoreIrInstruction {
  private:
    CoreIrCastKind cast_kind_;

  public:
    CoreIrCastInst(CoreIrCastKind cast_kind, const CoreIrType *type,
                   std::string name, CoreIrValue *operand)
        : CoreIrInstruction(CoreIrOpcode::Cast, type, std::move(name)),
          cast_kind_(cast_kind) {
        append_operand(operand);
    }

    CoreIrCastKind get_cast_kind() const noexcept { return cast_kind_; }

    CoreIrValue *get_operand() const noexcept {
        return get_operands().empty() ? nullptr : get_operands()[0];
    }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrAddressOfFunctionInst final : public CoreIrInstruction {
  private:
    CoreIrFunction *function_ = nullptr;

  public:
    CoreIrAddressOfFunctionInst(const CoreIrType *type, std::string name,
                                CoreIrFunction *function)
        : CoreIrInstruction(CoreIrOpcode::AddressOfFunction, type,
                            std::move(name)),
          function_(function) {}

    CoreIrFunction *get_function() const noexcept { return function_; }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrAddressOfGlobalInst final : public CoreIrInstruction {
  private:
    CoreIrGlobal *global_ = nullptr;

  public:
    CoreIrAddressOfGlobalInst(const CoreIrType *type, std::string name,
                              CoreIrGlobal *global)
        : CoreIrInstruction(CoreIrOpcode::AddressOfGlobal, type,
                            std::move(name)),
          global_(global) {}

    CoreIrGlobal *get_global() const noexcept { return global_; }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrAddressOfStackSlotInst final : public CoreIrInstruction {
  private:
    CoreIrStackSlot *stack_slot_ = nullptr;

  public:
    CoreIrAddressOfStackSlotInst(const CoreIrType *type, std::string name,
                                 CoreIrStackSlot *stack_slot)
        : CoreIrInstruction(CoreIrOpcode::AddressOfStackSlot, type,
                            std::move(name)),
          stack_slot_(stack_slot) {}

    CoreIrStackSlot *get_stack_slot() const noexcept { return stack_slot_; }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrGetElementPtrInst final : public CoreIrInstruction {
  public:
    CoreIrGetElementPtrInst(const CoreIrType *type, std::string name,
                            CoreIrValue *base,
                            std::vector<CoreIrValue *> indices)
        : CoreIrInstruction(CoreIrOpcode::GetElementPtr, type,
                            std::move(name)) {
        append_operand(base);
        for (CoreIrValue *index : indices) {
            append_operand(index);
        }
    }

    CoreIrValue *get_base() const noexcept {
        return get_operands().empty() ? nullptr : get_operands()[0];
    }

    std::size_t get_index_count() const noexcept {
        return get_operands().size() > 0 ? get_operands().size() - 1 : 0;
    }

    CoreIrValue *get_index(std::size_t index) const noexcept {
        if (index >= get_index_count()) {
            return nullptr;
        }
        return get_operands()[index + 1];
    }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrLoadInst final : public CoreIrInstruction {
  private:
    CoreIrStackSlot *stack_slot_ = nullptr;

  public:
    CoreIrLoadInst(const CoreIrType *type, std::string name,
                   CoreIrStackSlot *stack_slot)
        : CoreIrInstruction(CoreIrOpcode::Load, type, std::move(name)),
          stack_slot_(stack_slot) {}

    CoreIrLoadInst(const CoreIrType *type, std::string name,
                   CoreIrValue *address)
        : CoreIrInstruction(CoreIrOpcode::Load, type, std::move(name)) {
        append_operand(address);
    }

    CoreIrStackSlot *get_stack_slot() const noexcept { return stack_slot_; }

    CoreIrValue *get_address() const noexcept {
        if (stack_slot_ != nullptr || get_operands().empty()) {
            return nullptr;
        }
        return get_operands()[0];
    }

    bool get_has_side_effect() const noexcept override { return false; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrStoreInst final : public CoreIrInstruction {
  private:
    CoreIrStackSlot *stack_slot_ = nullptr;

  public:
    CoreIrStoreInst(const CoreIrType *void_type, CoreIrValue *value,
                    CoreIrStackSlot *stack_slot)
        : CoreIrInstruction(CoreIrOpcode::Store, void_type),
          stack_slot_(stack_slot) {
        append_operand(value);
    }

    CoreIrStoreInst(const CoreIrType *void_type, CoreIrValue *value,
                    CoreIrValue *address)
        : CoreIrInstruction(CoreIrOpcode::Store, void_type) {
        append_operand(value);
        append_operand(address);
    }

    CoreIrValue *get_value() const noexcept {
        return get_operands().empty() ? nullptr : get_operands()[0];
    }

    CoreIrStackSlot *get_stack_slot() const noexcept { return stack_slot_; }

    CoreIrValue *get_address() const noexcept {
        if (stack_slot_ != nullptr || get_operands().size() < 2) {
            return nullptr;
        }
        return get_operands()[1];
    }

    bool get_has_side_effect() const noexcept override { return true; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrCallInst final : public CoreIrInstruction {
  private:
    std::string callee_name_;
    CoreIrValue *callee_value_ = nullptr;
    const CoreIrFunctionType *callee_type_ = nullptr;
    std::size_t argument_begin_index_ = 0;

  public:
    CoreIrCallInst(const CoreIrType *type, std::string name,
                   std::string callee_name,
                   const CoreIrFunctionType *callee_type,
                   std::vector<CoreIrValue *> arguments)
        : CoreIrInstruction(CoreIrOpcode::Call, type, std::move(name)),
          callee_name_(std::move(callee_name)),
          callee_type_(callee_type),
          argument_begin_index_(0) {
        for (CoreIrValue *argument : arguments) {
            append_operand(argument);
        }
    }

    CoreIrCallInst(const CoreIrType *type, std::string name,
                   CoreIrValue *callee_value,
                   const CoreIrFunctionType *callee_type,
                   std::vector<CoreIrValue *> arguments)
        : CoreIrInstruction(CoreIrOpcode::Call, type, std::move(name)),
          callee_value_(callee_value),
          callee_type_(callee_type),
          argument_begin_index_(1) {
        append_operand(callee_value);
        for (CoreIrValue *argument : arguments) {
            append_operand(argument);
        }
    }

    bool get_is_direct_call() const noexcept { return callee_value_ == nullptr; }

    const std::string &get_callee_name() const noexcept { return callee_name_; }

    CoreIrValue *get_callee_value() const noexcept {
        if (callee_value_ == nullptr || get_operands().empty()) {
            return callee_value_;
        }
        return get_operands()[0];
    }

    const CoreIrFunctionType *get_callee_type() const noexcept {
        return callee_type_;
    }

    void set_callee_name(std::string callee_name) {
        callee_name_ = std::move(callee_name);
        callee_value_ = nullptr;
        argument_begin_index_ = 0;
    }

    void set_callee_value(CoreIrValue *callee_value) {
        if (argument_begin_index_ == 1) {
            set_operand(0, callee_value);
        } else {
            std::vector<CoreIrValue *> arguments;
            for (std::size_t index = argument_begin_index_; index < get_operands().size();
                 ++index) {
                arguments.push_back(get_operands()[index]);
            }
            detach_operands();
            append_operand(callee_value);
            for (CoreIrValue *argument : arguments) {
                append_operand(argument);
            }
            argument_begin_index_ = 1;
        }
        callee_value_ = callee_value;
        callee_name_.clear();
    }

    void set_callee_type(const CoreIrFunctionType *callee_type) noexcept {
        callee_type_ = callee_type;
    }

    std::size_t get_argument_begin_index() const noexcept {
        return argument_begin_index_;
    }

    std::size_t get_argument_count() const noexcept {
        return get_operands().size() < argument_begin_index_
                   ? 0
                   : get_operands().size() - argument_begin_index_;
    }

    CoreIrValue *get_argument(std::size_t index) const noexcept {
        const std::size_t operand_index = argument_begin_index_ + index;
        return operand_index < get_operands().size() ? get_operands()[operand_index]
                                                     : nullptr;
    }

    void set_argument(std::size_t index, CoreIrValue *value) {
        set_operand(argument_begin_index_ + index, value);
    }

    bool get_has_side_effect() const noexcept override { return true; }

    bool get_is_terminator() const noexcept override { return false; }
};

class CoreIrJumpInst final : public CoreIrInstruction {
  private:
    CoreIrBasicBlock *target_block_ = nullptr;

  public:
    explicit CoreIrJumpInst(const CoreIrType *void_type,
                            CoreIrBasicBlock *target_block)
        : CoreIrInstruction(CoreIrOpcode::Jump, void_type),
          target_block_(target_block) {}

    CoreIrBasicBlock *get_target_block() const noexcept { return target_block_; }

    void set_target_block(CoreIrBasicBlock *target_block) noexcept {
        target_block_ = target_block;
    }

    bool get_has_side_effect() const noexcept override { return true; }

    bool get_is_terminator() const noexcept override { return true; }
};

class CoreIrCondJumpInst final : public CoreIrInstruction {
  private:
    CoreIrBasicBlock *true_block_ = nullptr;
    CoreIrBasicBlock *false_block_ = nullptr;

  public:
    CoreIrCondJumpInst(const CoreIrType *void_type, CoreIrValue *condition,
                       CoreIrBasicBlock *true_block,
                       CoreIrBasicBlock *false_block)
        : CoreIrInstruction(CoreIrOpcode::CondJump, void_type),
          true_block_(true_block), false_block_(false_block) {
        append_operand(condition);
    }

    CoreIrValue *get_condition() const noexcept {
        return get_operands().empty() ? nullptr : get_operands()[0];
    }

    CoreIrBasicBlock *get_true_block() const noexcept { return true_block_; }

    CoreIrBasicBlock *get_false_block() const noexcept { return false_block_; }

    void set_true_block(CoreIrBasicBlock *true_block) noexcept {
        true_block_ = true_block;
    }

    void set_false_block(CoreIrBasicBlock *false_block) noexcept {
        false_block_ = false_block;
    }

    bool get_has_side_effect() const noexcept override { return true; }

    bool get_is_terminator() const noexcept override { return true; }
};

class CoreIrReturnInst final : public CoreIrInstruction {
  public:
    explicit CoreIrReturnInst(const CoreIrType *void_type)
        : CoreIrInstruction(CoreIrOpcode::Return, void_type) {}

    CoreIrReturnInst(const CoreIrType *void_type, CoreIrValue *return_value)
        : CoreIrInstruction(CoreIrOpcode::Return, void_type) {
        append_operand(return_value);
    }

    CoreIrValue *get_return_value() const noexcept {
        return get_operands().empty() ? nullptr : get_operands()[0];
    }

    bool get_has_side_effect() const noexcept override { return true; }

    bool get_is_terminator() const noexcept override { return true; }
};

} // namespace sysycc
