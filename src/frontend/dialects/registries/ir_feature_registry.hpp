#pragma once

#include <set>

namespace sysycc {

enum class IrFeature : unsigned char {
    FunctionAttributes,
    BitFieldMembers,
    ExtendedBuiltinTypes,
};

class IrFeatureRegistry {
  private:
    std::set<IrFeature> features_;

  public:
    void add_feature(IrFeature feature);

    bool has_feature(IrFeature feature) const noexcept;

    const std::set<IrFeature> &get_features() const noexcept;
};

} // namespace sysycc
