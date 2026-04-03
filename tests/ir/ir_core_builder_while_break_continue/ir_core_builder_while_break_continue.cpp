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
    const auto contains = [&](const std::string &snippet) {
        return actual.find(snippet) != std::string::npos;
    };

    assert(contains("module ir_core_builder_while_break_continue\n"));
    assert(contains("func @main() -> i32 {\n"));
    assert(contains("stackslot %value.addr : i32, align 4\n"));
    assert(contains("while.cond0:\n"));
    assert(contains("while.body0:\n"));
    assert(contains("while.end0:\n"));
    assert(contains("if.then0:\n"));
    assert(contains("if.end0:\n"));
    assert(contains("if.then1:\n"));
    assert(contains("if.end1:\n"));
    assert(contains("icmp slt i32"));
    assert(contains("icmp eq i32"));
    assert(contains("jmp %while.cond0"));
    assert(contains("jmp %while.end0"));
    assert(contains("ret i32 %"));
    return 0;
}
