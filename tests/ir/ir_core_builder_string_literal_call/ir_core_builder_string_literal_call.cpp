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
        "module ir_core_builder_string_literal_call\n"
        "\n"
        "const @.str0 : [3 x i8] = c\"hi\\00\" internal\n"
        "\n"
        "func @sink(i8* %text) -> i32 {\n"
        "  stackslot %text.addr : i8*, align 8\n"
        "\n"
        "entry:\n"
        "  store i8* %text, %text.addr\n"
        "  ret i32 0\n"
        "}\n"
        "\n"
        "func @main() -> i32 {\n"
        "entry:\n"
        "  %t0 = addr_of_global [3 x i8]* @.str0\n"
        "  %t1 = gep i8* %t0, 0, 0\n"
        "  %t2 = call i32 @sink(i8* %t1)\n"
        "  ret i32 %t2\n"
        "}\n";
    assert(actual == expected);
    return 0;
}
