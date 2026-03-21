#pragma once

#include <cstddef>
#include <string>

#include "compiler/pass/pass.hpp"
#include "frontend/dialects/preprocess_probe_handler_registry.hpp"
#include "frontend/preprocess/detail/conditional/clang_extension_provider.hpp"
#include "frontend/preprocess/detail/conditional/gnu_extension_provider.hpp"

namespace sysycc::preprocess::detail {

// Routes non-standard preprocessor builtin probes to provider-specific
// handlers.
class NonStandardExtensionManager {
  private:
    ClangExtensionProvider clang_extension_provider_;
    GnuExtensionProvider gnu_extension_provider_;

  public:
    PassResult try_evaluate(const std::string &expression, std::size_t &index,
                            const PreprocessProbeHandlerRegistry &registry,
                            long long &value, bool &handled) const;
};

} // namespace sysycc::preprocess::detail
