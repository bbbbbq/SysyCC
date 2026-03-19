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

PointerSemanticType::PointerSemanticType(const SemanticType *pointee_type)
    : SemanticType(SemanticTypeKind::Pointer), pointee_type_(pointee_type) {}

const SemanticType *PointerSemanticType::get_pointee_type() const noexcept {
    return pointee_type_;
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
    std::vector<const SemanticType *> parameter_types)
    : SemanticType(SemanticTypeKind::Function), return_type_(return_type),
      parameter_types_(std::move(parameter_types)) {}

const SemanticType *FunctionSemanticType::get_return_type() const noexcept {
    return return_type_;
}

const std::vector<const SemanticType *> &
FunctionSemanticType::get_parameter_types() const noexcept {
    return parameter_types_;
}

StructSemanticType::StructSemanticType(std::string name)
    : SemanticType(SemanticTypeKind::Struct), name_(std::move(name)) {}

const std::string &StructSemanticType::get_name() const noexcept {
    return name_;
}

EnumSemanticType::EnumSemanticType(std::string name)
    : SemanticType(SemanticTypeKind::Enum), name_(std::move(name)) {}

const std::string &EnumSemanticType::get_name() const noexcept {
    return name_;
}

} // namespace sysycc
