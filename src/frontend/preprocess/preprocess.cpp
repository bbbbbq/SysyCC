#include "frontend/preprocess/preprocess.hpp"

#include "frontend/preprocess/detail/preprocess_session.hpp"

namespace sysycc {

PassKind PreprocessPass::Kind() const { return PassKind::Preprocess; }

const char *PreprocessPass::Name() const { return "PreprocessPass"; }

PassResult PreprocessPass::Run(CompilerContext &context) {
    preprocess::detail::PreprocessSession session(context);
    return session.Run();
}

} // namespace sysycc
