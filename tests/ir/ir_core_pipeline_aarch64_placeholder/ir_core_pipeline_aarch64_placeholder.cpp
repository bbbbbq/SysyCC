#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/ir_result.hpp"
#include "backend/ir/pipeline/core_ir_pipeline.hpp"
#include "compiler/complier.hpp"
#include "compiler/complier_option.hpp"
#include "compiler/pass/pass.hpp"

using namespace sysycc;

int main(int argc, char **argv) {
    assert(argc == 2);

    ComplierOption option(argv[1]);
    option.set_stop_after_stage(StopAfterStage::Semantic);

    Complier complier(option);
    PassResult frontend_result = complier.Run();
    assert(frontend_result.ok);

    CompilerContext &context = complier.get_context();
    CoreIrPipeline pipeline(IrKind::AArch64);
    std::unique_ptr<IRResult> ir_result = pipeline.BuildOptimizeAndLower(context);
    assert(ir_result == nullptr);
    assert(context.get_diagnostic_engine().has_error());

    bool found_message = false;
    for (const auto &diagnostic : context.get_diagnostic_engine().get_diagnostics()) {
        if (diagnostic.get_message() ==
            "core ir aarch64 target backend is not implemented yet") {
            found_message = true;
            break;
        }
    }
    assert(found_message);
    return 0;
}
