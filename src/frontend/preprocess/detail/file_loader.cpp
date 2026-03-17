#include "frontend/preprocess/detail/file_loader.hpp"

#include <fstream>
#include <string>

namespace sysycc::preprocess::detail {

PassResult FileLoader::read_lines(const std::string &file_path,
                                  std::vector<std::string> &lines) const {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        return PassResult::Failure(
            "failed to open input file for preprocessor");
    }

    lines.clear();
    std::string line;
    while (std::getline(ifs, line)) {
        lines.push_back(line);
    }

    return PassResult::Success();
}

} // namespace sysycc::preprocess::detail
