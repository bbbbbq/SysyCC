#include "frontend/semantic/model/semantic_type.hpp"

#include <utility>

namespace sysycc {

SemanticType::SemanticType(SemanticTypeKind kind) : kind_(kind) {}

SemanticTypeKind SemanticType::get_kind() const noexcept { return kind_; }

BuiltinSemanticType::BuiltinSemanticType(std::string name)
    : SemanticType(SemanticTypeKind::Builtin), name_(std::move(name)) {}

const std::string &BuiltinSemanticType::get_name() const noexcept {
    return name_;
}

QualifiedSemanticType::QualifiedSemanticType(bool is_const, bool is_volatile,
                                             const SemanticType *base_type)
    : QualifiedSemanticType(is_const, is_volatile, false, base_type) {}

QualifiedSemanticType::QualifiedSemanticType(bool is_const, bool is_volatile,
                                             bool is_restrict,
                                             const SemanticType *base_type)
    : SemanticType(SemanticTypeKind::Qualified), is_const_(is_const),
      is_volatile_(is_volatile),
      is_restrict_(is_restrict),
      base_type_(base_type) {}

bool QualifiedSemanticType::get_is_const() const noexcept { return is_const_; }

bool QualifiedSemanticType::get_is_volatile() const noexcept {
    return is_volatile_;
}

bool QualifiedSemanticType::get_is_restrict() const noexcept {
    return is_restrict_;
}

const SemanticType *QualifiedSemanticType::get_base_type() const noexcept {
    return base_type_;
}

PointerSemanticType::PointerSemanticType(
    const SemanticType *pointee_type, PointerNullabilityKind nullability_kind)
    : SemanticType(SemanticTypeKind::Pointer), pointee_type_(pointee_type),
      nullability_kind_(nullability_kind) {}

const SemanticType *PointerSemanticType::get_pointee_type() const noexcept {
    return pointee_type_;
}

PointerNullabilityKind PointerSemanticType::get_nullability_kind() const noexcept {
    return nullability_kind_;
}

ArraySemanticType::ArraySemanticType(const SemanticType *element_type,
                                     std::vector<int> dimensions)
    : SemanticType(SemanticTypeKind::Array), element_type_(element_type),
      dimensions_(std::move(dimensions)) {}

const SemanticType *ArraySemanticType::get_element_type() const noexcept {
    return element_type_;
}

const std::vector<int> &ArraySemanticType::get_dimensions() const noexcept {
    return dimensions_;
}

FunctionSemanticType::FunctionSemanticType(
    const SemanticType *return_type,
    std::vector<const SemanticType *> parameter_types, bool is_variadic)
    : SemanticType(SemanticTypeKind::Function), return_type_(return_type),
      parameter_types_(std::move(parameter_types)), is_variadic_(is_variadic) {}

const SemanticType *FunctionSemanticType::get_return_type() const noexcept {
    return return_type_;
}

const std::vector<const SemanticType *> &
FunctionSemanticType::get_parameter_types() const noexcept {
    return parameter_types_;
}

bool FunctionSemanticType::get_is_variadic() const noexcept {
    return is_variadic_;
}

StructSemanticType::StructSemanticType(std::string name,
                                       std::vector<SemanticFieldInfo> fields)
    : SemanticType(SemanticTypeKind::Struct), name_(std::move(name)),
      fields_(std::move(fields)) {}

const std::string &StructSemanticType::get_name() const noexcept {
    return name_;
}

const std::vector<SemanticFieldInfo> &StructSemanticType::get_fields() const noexcept {
    return fields_;
}

UnionSemanticType::UnionSemanticType(std::string name,
                                     std::vector<SemanticFieldInfo> fields)
    : SemanticType(SemanticTypeKind::Union), name_(std::move(name)),
      fields_(std::move(fields)) {}

const std::string &UnionSemanticType::get_name() const noexcept {
    return name_;
}

const std::vector<SemanticFieldInfo> &UnionSemanticType::get_fields() const noexcept {
    return fields_;
}

EnumSemanticType::EnumSemanticType(std::string name)
    : SemanticType(SemanticTypeKind::Enum), name_(std::move(name)) {}

const std::string &EnumSemanticType::get_name() const noexcept {
    return name_;
}

} // namespace sysycc
