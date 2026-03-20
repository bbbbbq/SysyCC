#pragma once

#include <stdint.h>
#include <string>
#include <vector>

namespace sysycc {

enum class SemanticTypeKind : uint8_t {
    Builtin,
    Pointer,
    Array,
    Function,
    Struct,
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

// Represents a pointer type.
class PointerSemanticType : public SemanticType {
  private:
    const SemanticType *pointee_type_;

  public:
    explicit PointerSemanticType(const SemanticType *pointee_type);

    const SemanticType *get_pointee_type() const noexcept;
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

  public:
    FunctionSemanticType(const SemanticType *return_type,
                         std::vector<const SemanticType *> parameter_types);

    const SemanticType *get_return_type() const noexcept;
    const std::vector<const SemanticType *> &get_parameter_types() const
        noexcept;
};

// Represents a named struct type.
class StructSemanticType : public SemanticType {
  private:
    std::string name_;

  public:
    explicit StructSemanticType(std::string name);

    const std::string &get_name() const noexcept;
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
