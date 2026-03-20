#include "frontend/preprocess/preprocess.hpp"

#include "common/diagnostic/diagnostic_engine.hpp"
#include "frontend/preprocess/detail/preprocess_session.hpp"

namespace sysycc {

PassKind PreprocessPass::Kind() const { return PassKind::Preprocess; }

const char *PreprocessPass::Name() const { return "PreprocessPass"; }

PassResult PreprocessPass::Run(CompilerContext &context) {
    preprocess::detail::PreprocessSession session(context);
    PassResult result = session.Run();
    if (!result.ok) {
        context.get_diagnostic_engine().add_error(DiagnosticStage::Preprocess,
                                                  result.message);
    }
    return result;
}

} // namespace sysycc
