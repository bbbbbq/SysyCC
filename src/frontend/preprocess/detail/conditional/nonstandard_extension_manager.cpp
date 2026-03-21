#include "frontend/preprocess/detail/conditional/nonstandard_extension_manager.hpp"

namespace sysycc::preprocess::detail {

PassResult NonStandardExtensionManager::try_evaluate(
    const std::string &expression, std::size_t &index, long long &value,
    bool &handled) const {
    PassResult clang_result = clang_extension_provider_.try_evaluate(
        expression, index, value, handled);
    if (!clang_result.ok || handled) {
        return clang_result;
    }

    return gnu_extension_provider_.try_evaluate(expression, index, value,
                                                handled);
}

} // namespace sysycc::preprocess::detail
