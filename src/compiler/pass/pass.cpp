#include "pass.hpp"

#include <stdexcept>

namespace sysycc {

void PassManager::AddPass(std::unique_ptr<Pass> pass) {
    if (pass == nullptr) {
        return;
    }

    if (get_pass_by_kind(pass->Kind()) != nullptr) {
        throw std::runtime_error(std::string("duplicate pass kind: ") +
                                 pass->Name());
    }

    passes_.push_back(std::move(pass));
}

Pass *PassManager::get_pass_by_kind(PassKind kind) const {
    for (const std::unique_ptr<Pass> &pass : passes_) {
        if (pass != nullptr && pass->Kind() == kind) {
            return pass.get();
        }
    }

    return nullptr;
}

PassResult PassManager::Run(CompilerContext &context) {
    for (const std::unique_ptr<Pass> &pass : passes_) {
        if (pass == nullptr) {
            return PassResult::Failure("encountered null pass");
        }

        const PassResult result = pass->Run(context);
        if (!result.ok) {
            return result;
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
