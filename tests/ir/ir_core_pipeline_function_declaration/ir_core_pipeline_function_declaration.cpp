#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/ir_result.hpp"
#include "backend/ir/pipeline/core_ir_pipeline.hpp"
#include "compiler/complier.hpp"
#include "compiler/complier_option.hpp"
#include "compiler/pass/pass.hpp"

using namespace sysycc;

namespace {

std::string get_expected_target_triple() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "arm64-apple-macosx15.0.0";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "x86_64-apple-macosx10.15.0";
#elif defined(__aarch64__)
    return "aarch64-unknown-linux-gnu";
#elif defined(__x86_64__)
    return "x86_64-unknown-linux-gnu";
#else
    return "";
#endif
}

std::string get_expected_target_datalayout() {
#if defined(__APPLE__) && defined(__aarch64__)
    return "e-m:o-i64:64-i128:128-n32:64-S128-Fn32";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "e-m:o-i64:64-f80:128-n8:16:32:64-S128";
#elif defined(__aarch64__)
    return "e-m:e-i64:64-i128:128-n32:64-S128";
#elif defined(__x86_64__)
    return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128";
#else
    return "";
#endif
}

} // namespace

int main(int argc, char **argv) {
    assert(argc == 2);

    ComplierOption option(argv[1]);
    option.set_stop_after_stage(StopAfterStage::Semantic);

    Complier complier(option);
    PassResult frontend_result = complier.Run();
    assert(frontend_result.ok);

    CompilerContext &context = complier.get_context();
    CoreIrPipeline pipeline(IrKind::LLVM);
    std::unique_ptr<IRResult> ir_result = pipeline.BuildOptimizeAndLower(context);
    assert(ir_result != nullptr);

    std::string expected;
    const std::string target_datalayout = get_expected_target_datalayout();
    const std::string target_triple = get_expected_target_triple();
    if (!target_datalayout.empty()) {
        expected += "target datalayout = \"" + target_datalayout + "\"\n";
    }
    if (!target_triple.empty()) {
        expected += "target triple = \"" + target_triple + "\"\n";
    }
    if (!target_datalayout.empty() || !target_triple.empty()) {
        expected += "\n";
    }
    expected +=
        "@.str0 = private unnamed_addr constant [3 x i8] c\"hi\\00\"\n"
        "\n"
        "declare i32 @puts(ptr)\n"
        "\n"
        "define i32 @main() {\n"
        "entry:\n"
        "  %t0 = getelementptr inbounds [3 x i8], ptr @.str0, i32 0, i32 0\n"
        "  %t1 = call i32 @puts(ptr %t0)\n"
        "  ret i32 %t1\n"
        "}\n";

    assert(ir_result->get_text() == expected);
    return 0;
}
