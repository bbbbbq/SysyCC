#include <cassert>
#include <string>

#include "backend/ir/build/build_core_ir_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    CompilerContext context;
    BuildCoreIrPass pass;
    const PassResult result = pass.Run(context);
    assert(!result.ok);
    assert(context.get_core_ir_build_result() == nullptr);
    assert(context.get_diagnostic_engine().has_error());
    return 0;
}
