#pragma once

#include <stdint.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "common/pointer_nullability_kind.hpp"

namespace sysycc {

enum class SemanticTypeKind : uint8_t {
    Builtin,
    Qualified,
    Pointer,
    Array,
    Function,
    Struct,
    Union,
    Enum,
};

// Base class for semantic types.
class SemanticType {
  private:
    SemanticTypeKind kind_;

  public:
    explicit SemanticType(SemanticTypeKind kind);
    virtual ~SemanticType() = default;

    SemanticTypeKind get_kind() const noexcept;
};

// Represents builtin scalar types such as int, float, and void.
class BuiltinSemanticType : public SemanticType {
  private:
    std::string name_;

  public:
    explicit BuiltinSemanticType(std::string name);

    const std::string &get_name() const noexcept;
};

class QualifiedSemanticType : public SemanticType {
  private:
    bool is_const_;
    bool is_volatile_;
    bool is_restrict_;
    const SemanticType *base_type_;

  public:
    QualifiedSemanticType(bool is_const, bool is_volatile,
                          const SemanticType *base_type);
    QualifiedSemanticType(bool is_const, bool is_volatile, bool is_restrict,
                          const SemanticType *base_type);

    bool get_is_const() const noexcept;
    bool get_is_volatile() const noexcept;
    bool get_is_restrict() const noexcept;
    const SemanticType *get_base_type() const noexcept;
};

// Represents a pointer type.
class PointerSemanticType : public SemanticType {
  private:
    const SemanticType *pointee_type_;
    PointerNullabilityKind nullability_kind_;

  public:
    explicit PointerSemanticType(
        const SemanticType *pointee_type,
        PointerNullabilityKind nullability_kind = PointerNullabilityKind::None);

    const SemanticType *get_pointee_type() const noexcept;
    PointerNullabilityKind get_nullability_kind() const noexcept;
};

// Represents an array type.
class ArraySemanticType : public SemanticType {
  private:
    const SemanticType *element_type_;
    std::vector<int> dimensions_;

  public:
    ArraySemanticType(const SemanticType *element_type,
                      std::vector<int> dimensions);

    const SemanticType *get_element_type() const noexcept;
    const std::vector<int> &get_dimensions() const noexcept;
};

// Represents a function type.
class FunctionSemanticType : public SemanticType {
  private:
    const SemanticType *return_type_;
    std::vector<const SemanticType *> parameter_types_;
    bool is_variadic_;

  public:
    FunctionSemanticType(const SemanticType *return_type,
                         std::vector<const SemanticType *> parameter_types,
                         bool is_variadic);

    const SemanticType *get_return_type() const noexcept;
    const std::vector<const SemanticType *> &get_parameter_types() const
        noexcept;
    bool get_is_variadic() const noexcept;
};

class SemanticFieldInfo {
  private:
    std::string name_;
    const SemanticType *type_;
    std::optional<int> bit_width_;

  public:
    SemanticFieldInfo(std::string name, const SemanticType *type,
                      std::optional<int> bit_width = std::nullopt)
        : name_(std::move(name)), type_(type), bit_width_(bit_width) {}

    const std::string &get_name() const noexcept { return name_; }
    const SemanticType *get_type() const noexcept { return type_; }
    bool get_is_bit_field() const noexcept { return bit_width_.has_value(); }
    const std::optional<int> &get_bit_width() const noexcept { return bit_width_; }
};

// Represents a named struct type.
class StructSemanticType : public SemanticType {
  private:
    std::string name_;
    std::vector<SemanticFieldInfo> fields_;

  public:
    StructSemanticType(std::string name, std::vector<SemanticFieldInfo> fields = {});

    const std::string &get_name() const noexcept;
    const std::vector<SemanticFieldInfo> &get_fields() const noexcept;
};

// Represents a union type.
class UnionSemanticType : public SemanticType {
  private:
    std::string name_;
    std::vector<SemanticFieldInfo> fields_;

  public:
    UnionSemanticType(std::string name, std::vector<SemanticFieldInfo> fields);

    const std::string &get_name() const noexcept;
    const std::vector<SemanticFieldInfo> &get_fields() const noexcept;
};

// Represents a named enum type.
class EnumSemanticType : public SemanticType {
  private:
    std::string name_;

  public:
    explicit EnumSemanticType(std::string name);

    const std::string &get_name() const noexcept;
};

} // namespace sysycc
