#include "backend/ir/pass/core_ir_pass.hpp"

#include <utility>

#include "backend/ir/core/ir_module.hpp"

namespace sysycc {

const char *CoreIrNoOpPass::get_name() const noexcept {
    return "CoreIrNoOpPass";
}

bool CoreIrNoOpPass::Run(CoreIrModule &module,
                         DiagnosticEngine &diagnostic_engine) {
    static_cast<void>(module);
    static_cast<void>(diagnostic_engine);
    return true;
}

void CoreIrPassManager::AddPass(std::unique_ptr<CoreIrPass> pass) {
    if (pass == nullptr) {
        return;
    }
    passes_.push_back(std::move(pass));
}

bool CoreIrPassManager::Run(CoreIrModule &module,
                            DiagnosticEngine &diagnostic_engine) {
    for (const auto &pass : passes_) {
        if (pass == nullptr) {
            return false;
        }
        if (!pass->Run(module, diagnostic_engine)) {
            return false;
        }
    }
    return true;
}

} // namespace sysycc
