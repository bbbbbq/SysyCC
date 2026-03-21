#pragma once

#include <set>

namespace sysycc {

enum class PreprocessFeature : unsigned char {
    ClangBuiltinProbes,
    HasIncludeFamily,
    GnuPredefinedMacros,
    NonStandardDirectivePayloads,
};

class PreprocessFeatureRegistry {
  private:
    std::set<PreprocessFeature> features_;

  public:
    void add_feature(PreprocessFeature feature);

    bool has_feature(PreprocessFeature feature) const noexcept;

    const std::set<PreprocessFeature> &get_features() const noexcept;
};

} // namespace sysycc
