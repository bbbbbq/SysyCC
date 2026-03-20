#include "common/source_span.hpp"

#include <memory>
#include <unordered_map>

namespace sysycc {

const SourceFile *get_source_file(const std::string &path) {
    if (path.empty()) {
        return nullptr;
    }

    static std::unordered_map<std::string, std::unique_ptr<SourceFile>>
        source_files;
    auto found = source_files.find(path);
    if (found != source_files.end()) {
        return found->second.get();
    }

    auto inserted =
        source_files
            .emplace(path, std::make_unique<SourceFile>(path))
            .first;
    return inserted->second.get();
}

} // namespace sysycc
