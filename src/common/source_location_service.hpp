#pragma once

#include <string>

#include "common/source_line_map.hpp"
#include "common/source_manager.hpp"
#include "common/source_mapping_view.hpp"

namespace sysycc {

// Provides one shared front-end service over source-file identity and
// preprocess-exported logical line remapping.
class SourceLocationService {
  private:
    SourceManager &source_manager_;
    SourceLineMap &preprocessed_line_map_;

  public:
    SourceLocationService(
        SourceManager &source_manager,
        SourceLineMap
            &preprocessed_line_map) noexcept // NOLINT(bugprone-easily-swappable-parameters)
        : source_manager_(source_manager),
          preprocessed_line_map_(preprocessed_line_map) {}

    SourceManager &get_source_manager() noexcept { return source_manager_; }

    const SourceManager &get_source_manager() const noexcept {
        return source_manager_;
    }

    SourceLineMap &get_preprocessed_line_map() noexcept {
        return preprocessed_line_map_;
    }

    const SourceLineMap &get_preprocessed_line_map() const noexcept {
        return preprocessed_line_map_;
    }

    SourceMappingView build_source_mapping_view(
        const std::string &physical_file_path) const;
};

} // namespace sysycc
