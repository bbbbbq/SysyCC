#include "common/source_location_service.hpp"

namespace sysycc {

SourceMappingView SourceLocationService::build_source_mapping_view(
    const std::string &physical_file_path) const {
    return SourceMappingView(source_manager_.get_source_file(physical_file_path),
                             &preprocessed_line_map_);
}

} // namespace sysycc
