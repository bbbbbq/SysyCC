#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
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
    CoreIrBuilder builder;
    std::unique_ptr<CoreIrBuildResult> build_result = builder.Build(context);
    assert(build_result != nullptr);

    CoreIrRawPrinter printer;
    const std::string actual = printer.print_module(*build_result->get_module());

    assert(actual.find("phi i32") != std::string::npos);
    assert(actual.find("%addr.addr") == std::string::npos);
    return 0;
}
