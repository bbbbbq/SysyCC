#pragma once

#include <string>

#include "frontend/dialects/registries/preprocess_feature_registry.hpp"
#include "frontend/preprocess/detail/macro_table.hpp"

namespace sysycc::preprocess::detail {

void initialize_predefined_macros(
    MacroTable &macro_table,
    const PreprocessFeatureRegistry &preprocess_feature_registry,
    const std::string &primary_input_file);

} // namespace sysycc::preprocess::detail
