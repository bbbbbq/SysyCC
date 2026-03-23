#include "pass.hpp"

#include <stdexcept>

namespace sysycc {

namespace {

bool should_stop_after_pass(const CompilerContext &context, PassKind pass_kind) {
    switch (context.get_stop_after_stage()) {
    case StopAfterStage::None:
        return false;
    case StopAfterStage::Preprocess:
        return pass_kind == PassKind::Preprocess;
    case StopAfterStage::Lex:
        return pass_kind == PassKind::Lex;
    case StopAfterStage::Parse:
        return pass_kind == PassKind::Parse;
    case StopAfterStage::Ast:
        return pass_kind == PassKind::Ast;
    case StopAfterStage::Semantic:
        return pass_kind == PassKind::Semantic;
    case StopAfterStage::IR:
        return pass_kind == PassKind::IRGen;
    }

    return false;
}

} // namespace

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

        PassResult result = pass->Run(context);
        if (!result.ok) {
            return result;
        }
        if (should_stop_after_pass(context, pass->Kind())) {
            return PassResult::Success();
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
