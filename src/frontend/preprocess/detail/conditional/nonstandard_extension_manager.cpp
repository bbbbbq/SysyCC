#include "frontend/preprocess/detail/conditional/nonstandard_extension_manager.hpp"

namespace sysycc::preprocess::detail {

PassResult NonStandardExtensionManager::try_evaluate(
    const std::string &expression, std::size_t &index,
    const PreprocessProbeHandlerRegistry &registry, long long &value,
    bool &handled) const {
    if (registry.has_handler(PreprocessProbeHandlerKind::ClangBuiltinProbes)) {
        PassResult clang_result = clang_extension_provider_.try_evaluate(
            expression, index, value, handled);
        if (!clang_result.ok || handled) {
            return clang_result;
        }
    }

    if (registry.has_handler(PreprocessProbeHandlerKind::GnuBuiltinProbes)) {
        return gnu_extension_provider_.try_evaluate(expression, index, value,
                                                    handled);
    }

    handled = false;
    return PassResult::Success();
}

} // namespace sysycc::preprocess::detail
