#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "backend/ir/lower/lowering/core_ir_target_backend.hpp"
#include "backend/ir/lower/lowering/core_ir_target_backend_factory.hpp"
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
    BuildCoreIrPass build_pass;
    assert(build_pass.Run(context).ok);

    const CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    assert(build_result != nullptr);
    assert(build_result->get_module() != nullptr);

    std::unique_ptr<CoreIrTargetBackend> target_backend =
        create_core_ir_target_backend(IrKind::AArch64);
    assert(target_backend != nullptr);

    std::unique_ptr<IRResult> ir_result =
        target_backend->Lower(*build_result->get_module(),
                              context.get_diagnostic_engine());
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
