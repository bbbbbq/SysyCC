#pragma once

#include <set>

namespace sysycc {

enum class AstFeature : unsigned char {
    GnuAttributeLists,
    GnuAsmLabels,
    BitFieldWidths,
    QualifiedTypeNodes,
    UnionTypeNodes,
    ExtendedBuiltinTypes,
};

class AstFeatureRegistry {
  private:
    std::set<AstFeature> features_;

  public:
    void add_feature(AstFeature feature);

    bool has_feature(AstFeature feature) const noexcept;

    const std::set<AstFeature> &get_features() const noexcept;
};

} // namespace sysycc
