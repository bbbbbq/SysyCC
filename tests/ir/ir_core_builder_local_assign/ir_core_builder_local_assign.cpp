#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "compiler/compiler.hpp"
#include "compiler/compiler_option.hpp"
#include "compiler/pass/pass.hpp"

using namespace sysycc;

int main(int argc, char **argv) {
    assert(argc == 2);

    CompilerOption option(argv[1]);
    option.set_stop_after_stage(StopAfterStage::Semantic);

    Compiler compiler(option);
    PassResult frontend_result = compiler.Run();
    assert(frontend_result.ok);

    CompilerContext &context = compiler.get_context();
    CoreIrBuilder builder;
    std::unique_ptr<CoreIrBuildResult> build_result = builder.Build(context);
    assert(build_result != nullptr);

    CoreIrRawPrinter printer;
    const std::string actual = printer.print_module(*build_result->get_module());
    const auto contains = [&](const std::string &snippet) {
        return actual.find(snippet) != std::string::npos;
    };

    assert(contains("module ir_core_builder_local_assign\n"));
    assert(contains("stackslot %sum.addr : i32, align 4\n"));
    assert(contains("addr_of_stackslot i32* %sum.addr"));
    assert(contains("load i32"));
    assert(contains("add i32"));
    assert(contains("store i32"));
    assert(contains("ret i32 %"));
    return 0;
}
