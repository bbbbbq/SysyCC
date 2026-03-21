#include "frontend/dialects/registries/parser_feature_registry.hpp"

namespace sysycc {

void ParserFeatureRegistry::add_feature(ParserFeature feature) {
    features_.insert(feature);
}

bool ParserFeatureRegistry::has_feature(ParserFeature feature) const noexcept {
    return features_.find(feature) != features_.end();
}

const std::set<ParserFeature> &ParserFeatureRegistry::get_features() const
    noexcept {
    return features_;
}

} // namespace sysycc
