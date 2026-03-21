#pragma once

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

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
    const SemanticType *base_type_;

  public:
    QualifiedSemanticType(bool is_const, const SemanticType *base_type);

    bool get_is_const() const noexcept;
    const SemanticType *get_base_type() const noexcept;
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

class SemanticFieldInfo {
  private:
    std::string name_;
    const SemanticType *type_;

  public:
    SemanticFieldInfo(std::string name, const SemanticType *type)
        : name_(std::move(name)), type_(type) {}

    const std::string &get_name() const noexcept { return name_; }
    const SemanticType *get_type() const noexcept { return type_; }
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
