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
    const std::string expected =
        "module ir_core_builder_if_stmt\n"
        "\n"
        "func @choose(i32 %cond, i32 %lhs, i32 %rhs) -> i32 {\n"
        "  stackslot %cond.addr : i32, align 4\n"
        "  stackslot %lhs.addr : i32, align 4\n"
        "  stackslot %rhs.addr : i32, align 4\n"
        "\n"
        "entry:\n"
        "  store i32 %cond, %cond.addr\n"
        "  store i32 %lhs, %lhs.addr\n"
        "  store i32 %rhs, %rhs.addr\n"
        "  %t0 = addr_of_stackslot i32* %cond.addr\n"
        "  %t1 = load i32, %t0\n"
        "  br i32 %t1, label %if.then0, label %if.else0\n"
        "if.then0:\n"
        "  %t2 = addr_of_stackslot i32* %lhs.addr\n"
        "  %t3 = load i32, %t2\n"
        "  ret i32 %t3\n"
        "if.else0:\n"
        "  %t4 = addr_of_stackslot i32* %rhs.addr\n"
        "  %t5 = load i32, %t4\n"
        "  ret i32 %t5\n"
        "}\n";
    assert(actual == expected);
    return 0;
}
