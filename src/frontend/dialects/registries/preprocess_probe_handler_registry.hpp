#pragma once

#include <map>
#include <string>
#include <vector>

namespace sysycc {

enum class PreprocessProbeHandlerKind : unsigned char {
    ClangBuiltinProbes,
    GnuBuiltinProbes,
};

class PreprocessProbeHandlerRegistry {
  private:
    std::map<PreprocessProbeHandlerKind, std::string> owner_names_;
    std::vector<std::string> registration_errors_;

  public:
    void add_handler(PreprocessProbeHandlerKind handler_kind,
                     std::string owner_name);

    bool has_handler(PreprocessProbeHandlerKind handler_kind) const noexcept;

    const std::string &
    get_owner_name(PreprocessProbeHandlerKind handler_kind) const noexcept;

    const std::vector<std::string> &get_registration_errors() const noexcept;
};

} // namespace sysycc
