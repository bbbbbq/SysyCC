#include "common/source_manager.hpp"

namespace sysycc {

const SourceFile *SourceManager::get_source_file(const std::string &path) {
    if (path.empty()) {
        return nullptr;
    }

    auto found = source_files_.find(path);
    if (found != source_files_.end()) {
        return found->second.get();
    }

    auto inserted =
        source_files_.emplace(path, std::make_unique<SourceFile>(path)).first;
    return inserted->second.get();
}

} // namespace sysycc
