#include <cassert>

#include "backend/ir/lower/lower_ir_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    CompilerContext context;
    LowerIrPass pass;
    const PassResult result = pass.Run(context);
    assert(!result.ok);
    assert(context.get_ir_result() == nullptr);
    assert(context.get_diagnostic_engine().has_error());
    return 0;
}
