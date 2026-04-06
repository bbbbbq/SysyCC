#pragma once

#include <cstddef>
#include <vector>

namespace sysycc {

class CoreIrContext;

enum class CoreIrTypeKind : unsigned char {
    Void,
    Integer,
    Float,
    Pointer,
    Array,
    Struct,
    Function,
};

enum class CoreIrFloatKind : unsigned char {
    Float16,
    Float32,
    Float64,
    Float128,
};

class CoreIrType {
  private:
    CoreIrContext *parent_context_ = nullptr;

  public:
    virtual ~CoreIrType() = default;
    virtual CoreIrTypeKind get_kind() const noexcept = 0;

    CoreIrContext *get_parent_context() const noexcept { return parent_context_; }

    void set_parent_context(CoreIrContext *parent_context) noexcept {
        parent_context_ = parent_context;
    }
};

class CoreIrVoidType final : public CoreIrType {
  public:
    CoreIrTypeKind get_kind() const noexcept override {
        return CoreIrTypeKind::Void;
    }
};

class CoreIrIntegerType final : public CoreIrType {
  private:
    std::size_t bit_width_ = 0;
    bool is_signed_ = true;

  public:
    explicit CoreIrIntegerType(std::size_t bit_width, bool is_signed = true)
        : bit_width_(bit_width), is_signed_(is_signed) {}

    CoreIrTypeKind get_kind() const noexcept override {
        return CoreIrTypeKind::Integer;
    }

    std::size_t get_bit_width() const noexcept { return bit_width_; }
    bool get_is_signed() const noexcept { return is_signed_; }
};

class CoreIrFloatType final : public CoreIrType {
  private:
    CoreIrFloatKind float_kind_;

  public:
    explicit CoreIrFloatType(CoreIrFloatKind float_kind)
        : float_kind_(float_kind) {}

    CoreIrTypeKind get_kind() const noexcept override {
        return CoreIrTypeKind::Float;
    }

    CoreIrFloatKind get_float_kind() const noexcept { return float_kind_; }
};

class CoreIrPointerType final : public CoreIrType {
  private:
    const CoreIrType *pointee_type_ = nullptr;

  public:
    explicit CoreIrPointerType(const CoreIrType *pointee_type)
        : pointee_type_(pointee_type) {}

    CoreIrTypeKind get_kind() const noexcept override {
        return CoreIrTypeKind::Pointer;
    }

    const CoreIrType *get_pointee_type() const noexcept {
        return pointee_type_;
    }
};

class CoreIrArrayType final : public CoreIrType {
  private:
    const CoreIrType *element_type_ = nullptr;
    std::size_t element_count_ = 0;

  public:
    CoreIrArrayType(const CoreIrType *element_type, std::size_t element_count)
        : element_type_(element_type), element_count_(element_count) {}

    CoreIrTypeKind get_kind() const noexcept override {
        return CoreIrTypeKind::Array;
    }

    const CoreIrType *get_element_type() const noexcept {
        return element_type_;
    }

    std::size_t get_element_count() const noexcept { return element_count_; }
};

class CoreIrStructType final : public CoreIrType {
  private:
    std::vector<const CoreIrType *> element_types_;

  public:
    CoreIrStructType() = default;

    explicit CoreIrStructType(std::vector<const CoreIrType *> element_types)
        : element_types_(std::move(element_types)) {}

    CoreIrTypeKind get_kind() const noexcept override {
        return CoreIrTypeKind::Struct;
    }

    const std::vector<const CoreIrType *> &get_element_types() const noexcept {
        return element_types_;
    }

    void set_element_types(std::vector<const CoreIrType *> element_types) {
        element_types_ = std::move(element_types);
    }
};

class CoreIrFunctionType final : public CoreIrType {
  private:
    const CoreIrType *return_type_ = nullptr;
    std::vector<const CoreIrType *> parameter_types_;
    bool is_variadic_ = false;

  public:
    CoreIrFunctionType(const CoreIrType *return_type,
                       std::vector<const CoreIrType *> parameter_types,
                       bool is_variadic)
        : return_type_(return_type),
          parameter_types_(std::move(parameter_types)),
          is_variadic_(is_variadic) {}

    CoreIrTypeKind get_kind() const noexcept override {
        return CoreIrTypeKind::Function;
    }

    const CoreIrType *get_return_type() const noexcept { return return_type_; }

    const std::vector<const CoreIrType *> &get_parameter_types() const noexcept {
        return parameter_types_;
    }

    bool get_is_variadic() const noexcept { return is_variadic_; }
};

} // namespace sysycc
