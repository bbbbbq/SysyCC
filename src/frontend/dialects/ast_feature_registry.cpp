#include "frontend/dialects/ast_feature_registry.hpp"

namespace sysycc {

void AstFeatureRegistry::add_feature(AstFeature feature) {
    features_.insert(feature);
}

bool AstFeatureRegistry::has_feature(AstFeature feature) const noexcept {
    return features_.find(feature) != features_.end();
}

const std::set<AstFeature> &AstFeatureRegistry::get_features() const noexcept {
    return features_;
}

} // namespace sysycc
