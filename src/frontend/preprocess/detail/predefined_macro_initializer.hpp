#pragma once

#include "frontend/dialects/preprocess_feature_registry.hpp"
#include "frontend/preprocess/detail/macro_table.hpp"

namespace sysycc::preprocess::detail {

void initialize_predefined_macros(
    MacroTable &macro_table,
    const PreprocessFeatureRegistry &preprocess_feature_registry);

} // namespace sysycc::preprocess::detail
