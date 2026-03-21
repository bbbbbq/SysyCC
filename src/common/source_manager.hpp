#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "common/source_span.hpp"

namespace sysycc {

// Owns stable SourceFile identities for one compiler run.
class SourceManager {
  private:
    std::unordered_map<std::string, std::unique_ptr<SourceFile>> source_files_;

  public:
    const SourceFile *get_source_file(const std::string &path);
};

} // namespace sysycc
