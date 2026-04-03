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
    const std::string expected =
        "module ir_core_builder_struct_member\n"
        "\n"
        "func @main() -> i32 {\n"
        "  stackslot %pair.addr : { i32, i32 }, align 4\n"
        "\n"
        "entry:\n"
        "  %t0 = addr_of_stackslot { i32, i32 }* %pair.addr\n"
        "  %t1 = gep i32* %t0, 0, 0\n"
        "  store i32 3, %t1\n"
        "  %t2 = addr_of_stackslot { i32, i32 }* %pair.addr\n"
        "  %t3 = gep i32* %t2, 0, 1\n"
        "  store i32 5, %t3\n"
        "  %t4 = addr_of_stackslot { i32, i32 }* %pair.addr\n"
        "  %t5 = gep i32* %t4, 0, 0\n"
        "  %t6 = load i32, %t5\n"
        "  %t7 = addr_of_stackslot { i32, i32 }* %pair.addr\n"
        "  %t8 = gep i32* %t7, 0, 1\n"
        "  %t9 = load i32, %t8\n"
        "  %t10 = add i32 %t6, %t9\n"
        "  ret i32 %t10\n"
        "}\n";
    assert(actual == expected);
    return 0;
}
