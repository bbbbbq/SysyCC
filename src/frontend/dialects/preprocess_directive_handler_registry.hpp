#pragma once

#include <map>
#include <string>
#include <vector>

namespace sysycc {

enum class PreprocessDirectiveHandlerKind : unsigned char {
    GnuWarningDirective,
    ClangWarningDirective,
    GnuPragmaOnceDirective,
    ClangPragmaOnceDirective,
};

class PreprocessDirectiveHandlerRegistry {
  private:
    std::map<PreprocessDirectiveHandlerKind, std::string> owner_names_;
    std::vector<std::string> registration_errors_;

  public:
    void add_handler(PreprocessDirectiveHandlerKind handler_kind,
                     std::string owner_name);

    bool has_handler(
        PreprocessDirectiveHandlerKind handler_kind) const noexcept;

    const std::string &
    get_owner_name(PreprocessDirectiveHandlerKind handler_kind) const noexcept;

    const std::vector<std::string> &get_registration_errors() const noexcept;
};

} // namespace sysycc
