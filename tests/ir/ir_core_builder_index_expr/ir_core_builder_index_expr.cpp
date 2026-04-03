#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/core/core_ir_builder.hpp"
#include "backend/ir/printer/core_ir_raw_printer.hpp"
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
    const auto contains = [&](const std::string &snippet) {
        return actual.find(snippet) != std::string::npos;
    };

    assert(contains("module ir_core_builder_index_expr\n"));
    assert(contains("stackslot %values.addr : [3 x i32], align 4\n"));
    assert(contains("stackslot %index.addr : i32, align 4\n"));
    assert(contains("addr_of_stackslot [3 x i32]* %values.addr"));
    assert(contains("addr_of_stackslot i32* %index.addr"));
    assert(contains("gep i32*"));
    assert(contains("load i32"));
    assert(contains("store i32 4"));
    assert(contains("ret i32 %"));
    return 0;
}
