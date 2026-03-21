#include "frontend/preprocess/detail/conditional/gnu_extension_provider.hpp"

namespace sysycc::preprocess::detail {

PassResult GnuExtensionProvider::try_evaluate(const std::string &expression,
                                              std::size_t &index,
                                              long long &value,
                                              bool &handled) const {
    (void)expression;
    (void)index;
    (void)value;
    handled = false;
    return PassResult::Success();
}

} // namespace sysycc::preprocess::detail
