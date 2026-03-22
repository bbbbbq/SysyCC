#pragma once

#include <set>

namespace sysycc {

enum class SemanticFeature : unsigned char {
    FunctionAttributes,
    BitFieldMembers,
    QualifiedPointerConversions,
    ExtendedBuiltinTypes,
    UnionSemanticTypes,
};

class SemanticFeatureRegistry {
  private:
    std::set<SemanticFeature> features_;

  public:
    void add_feature(SemanticFeature feature);

    bool has_feature(SemanticFeature feature) const noexcept;

    const std::set<SemanticFeature> &get_features() const noexcept;
};

} // namespace sysycc
