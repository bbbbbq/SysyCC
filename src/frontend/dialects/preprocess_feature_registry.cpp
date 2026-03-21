#include "frontend/dialects/preprocess_feature_registry.hpp"

namespace sysycc {

void PreprocessFeatureRegistry::add_feature(PreprocessFeature feature) {
    features_.insert(feature);
}

bool PreprocessFeatureRegistry::has_feature(
    PreprocessFeature feature) const noexcept {
    return features_.find(feature) != features_.end();
}

const std::set<PreprocessFeature> &
PreprocessFeatureRegistry::get_features() const noexcept {
    return features_;
}

} // namespace sysycc
