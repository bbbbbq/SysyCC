#include "frontend/dialects/semantic_feature_registry.hpp"

namespace sysycc {

void SemanticFeatureRegistry::add_feature(SemanticFeature feature) {
    features_.insert(feature);
}

bool SemanticFeatureRegistry::has_feature(
    SemanticFeature feature) const noexcept {
    return features_.find(feature) != features_.end();
}

const std::set<SemanticFeature> &SemanticFeatureRegistry::get_features() const
    noexcept {
    return features_;
}

} // namespace sysycc
