#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"

#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

} // namespace

PassKind CoreIrCanonicalizePass::Kind() const {
    return PassKind::CoreIrCanonicalize;
}

const char *CoreIrCanonicalizePass::Name() const {
    return "CoreIrCanonicalizePass";
}

PassResult CoreIrCanonicalizePass::Run(CompilerContext &context) {
    if (context.get_core_ir_build_result() == nullptr) {
        return fail_missing_core_ir(context, Name());
    }
    return PassResult::Success();
}

} // namespace sysycc
