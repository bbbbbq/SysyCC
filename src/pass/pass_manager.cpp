#include "pass/pass_manager.hpp"

#include <memory>
#include <utility>

namespace sysycc {

void PassManager::AddPass(std::unique_ptr<Pass> pass) {
    passes_.push_back(std::move(pass));
}

PassStatus PassManager::Run(CompilerContext& context) {
    for (const std::unique_ptr<Pass>& pass : passes_) {
        const PassStatus status = pass->Run(context);
        if (!status.ok) {
            return status;
        }
    }

    return PassStatus::Success();
}

} // namespace sysycc
