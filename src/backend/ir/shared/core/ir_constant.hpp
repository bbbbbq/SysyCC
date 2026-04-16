#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "backend/ir/shared/core/ir_value.hpp"

namespace sysycc {

class CoreIrGlobal;
class CoreIrFunction;
class CoreIrContext;
enum class CoreIrCastKind : unsigned char;

class CoreIrConstant : public CoreIrValue {
  private:
    CoreIrContext *parent_context_ = nullptr;

  public:
    using CoreIrValue::CoreIrValue;

    CoreIrContext *get_parent_context() const noexcept { return parent_context_; }

    void set_parent_context(CoreIrContext *parent_context) noexcept {
        parent_context_ = parent_context;
    }
};

class CoreIrConstantInt final : public CoreIrConstant {
  private:
    std::uint64_t value_ = 0;

  public:
    CoreIrConstantInt(const CoreIrType *type, std::uint64_t value)
        : CoreIrConstant(type), value_(value) {}

    std::uint64_t get_value() const noexcept { return value_; }
};

class CoreIrConstantFloat final : public CoreIrConstant {
  private:
    std::string literal_text_;

  public:
    CoreIrConstantFloat(const CoreIrType *type, std::string literal_text)
        : CoreIrConstant(type), literal_text_(std::move(literal_text)) {}

    const std::string &get_literal_text() const noexcept {
        return literal_text_;
    }
};

class CoreIrConstantNull final : public CoreIrConstant {
  public:
    explicit CoreIrConstantNull(const CoreIrType *type) : CoreIrConstant(type) {}
};

class CoreIrConstantZeroInitializer final : public CoreIrConstant {
  public:
    explicit CoreIrConstantZeroInitializer(const CoreIrType *type)
        : CoreIrConstant(type) {}
};

class CoreIrConstantByteString final : public CoreIrConstant {
  private:
    std::vector<std::uint8_t> bytes_;

  public:
    CoreIrConstantByteString(const CoreIrType *type,
                             std::vector<std::uint8_t> bytes)
        : CoreIrConstant(type), bytes_(std::move(bytes)) {}

    const std::vector<std::uint8_t> &get_bytes() const noexcept {
        return bytes_;
    }
};

class CoreIrConstantAggregate final : public CoreIrConstant {
  private:
    std::vector<const CoreIrConstant *> elements_;

  public:
    CoreIrConstantAggregate(const CoreIrType *type,
                            std::vector<const CoreIrConstant *> elements)
        : CoreIrConstant(type), elements_(std::move(elements)) {}

    const std::vector<const CoreIrConstant *> &get_elements() const noexcept {
        return elements_;
    }
};

class CoreIrConstantGlobalAddress final : public CoreIrConstant {
  private:
    CoreIrGlobal *global_ = nullptr;
    CoreIrFunction *function_ = nullptr;

  public:
    CoreIrConstantGlobalAddress(const CoreIrType *type, CoreIrGlobal *global)
        : CoreIrConstant(type), global_(global) {}

    CoreIrConstantGlobalAddress(const CoreIrType *type, CoreIrFunction *function)
        : CoreIrConstant(type), function_(function) {}

    CoreIrGlobal *get_global() const noexcept { return global_; }
    CoreIrFunction *get_function() const noexcept { return function_; }
};

class CoreIrConstantGetElementPtr final : public CoreIrConstant {
  private:
    const CoreIrConstant *base_ = nullptr;
    std::vector<const CoreIrConstant *> indices_;

  public:
    CoreIrConstantGetElementPtr(const CoreIrType *type,
                                const CoreIrConstant *base,
                                std::vector<const CoreIrConstant *> indices)
        : CoreIrConstant(type), base_(base), indices_(std::move(indices)) {}

    const CoreIrConstant *get_base() const noexcept { return base_; }

    const std::vector<const CoreIrConstant *> &get_indices() const noexcept {
        return indices_;
    }
};

class CoreIrConstantCast final : public CoreIrConstant {
  private:
    CoreIrCastKind cast_kind_;
    const CoreIrConstant *operand_ = nullptr;

  public:
    CoreIrConstantCast(const CoreIrType *type, CoreIrCastKind cast_kind,
                       const CoreIrConstant *operand)
        : CoreIrConstant(type), cast_kind_(cast_kind), operand_(operand) {}

    CoreIrCastKind get_cast_kind() const noexcept { return cast_kind_; }
    const CoreIrConstant *get_operand() const noexcept { return operand_; }
};

class CoreIrConstantBlockAddress final : public CoreIrConstant {
  private:
    std::string function_name_;
    std::string block_name_;

  public:
    CoreIrConstantBlockAddress(const CoreIrType *type, std::string function_name,
                               std::string block_name)
        : CoreIrConstant(type),
          function_name_(std::move(function_name)),
          block_name_(std::move(block_name)) {}

    const std::string &get_function_name() const noexcept { return function_name_; }
    const std::string &get_block_name() const noexcept { return block_name_; }
};

} // namespace sysycc
