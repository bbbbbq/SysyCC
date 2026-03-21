#include "frontend/dialects/registries/ir_feature_registry.hpp"

namespace sysycc {

void IrFeatureRegistry::add_feature(IrFeature feature) {
    features_.insert(feature);
}

bool IrFeatureRegistry::has_feature(IrFeature feature) const noexcept {
    return features_.find(feature) != features_.end();
}

const std::set<IrFeature> &IrFeatureRegistry::get_features() const noexcept {
    return features_;
}

} // namespace sysycc
