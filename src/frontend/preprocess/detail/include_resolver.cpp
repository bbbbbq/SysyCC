#include "frontend/preprocess/detail/include_resolver.hpp"

#include <cctype>
#include <filesystem>
#include <string>

namespace sysycc::preprocess::detail {

namespace {

std::string trim_left(const std::string &text) {
    std::size_t index = 0;
    while (index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
    return text.substr(index);
}

} // namespace

PassResult IncludeResolver::resolve_local_include(
    const std::string &directive_line, const std::string &including_file_path,
    const std::vector<std::string> &include_directories,
    const std::string &include_token, std::string &resolved_file_path) const {
    if (include_token.size() < 2 || include_token.front() != '"' ||
        include_token.back() != '"') {
        return PassResult::Failure("unsupported #include form: " +
                                   trim_left(directive_line));
    }

    const std::string include_name =
        include_token.substr(1, include_token.size() - 2);
    if (include_name.empty()) {
        return PassResult::Failure("invalid #include directive: empty path");
    }

    const std::filesystem::path current_path(including_file_path);
    const std::filesystem::path local_path =
        current_path.parent_path() / include_name;
    if (std::filesystem::exists(local_path)) {
        resolved_file_path = local_path.lexically_normal().string();
        return PassResult::Success();
    }

    for (const std::string &include_directory : include_directories) {
        const std::filesystem::path include_path =
            std::filesystem::path(include_directory) / include_name;
        if (std::filesystem::exists(include_path)) {
            resolved_file_path = include_path.lexically_normal().string();
            return PassResult::Success();
        }
    }

    return PassResult::Failure("failed to resolve included file: " +
                               include_name);
}

} // namespace sysycc::preprocess::detail
