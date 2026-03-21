#pragma once

#include <set>

namespace sysycc {

enum class ParserFeature : unsigned char {
    FunctionPrototypeDeclarations,
    ExternVariableDeclarations,
    GnuAttributeSpecifiers,
    ExtendedBuiltinTypeSpecifiers,
    QualifiedPrototypeParameters,
    UnionDeclarations,
    TypedefNameTypeSpecifiers,
};

class ParserFeatureRegistry {
  private:
    std::set<ParserFeature> features_;

  public:
    void add_feature(ParserFeature feature);

    bool has_feature(ParserFeature feature) const noexcept;

    const std::set<ParserFeature> &get_features() const noexcept;
};

} // namespace sysycc
