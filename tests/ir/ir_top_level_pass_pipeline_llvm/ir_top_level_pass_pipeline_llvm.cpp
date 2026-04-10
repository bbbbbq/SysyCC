#include <cassert>
#include <string>
#include <vector>

#include "backend/ir/lower/lower_ir_pass.hpp"
#include "backend/ir/pipeline/core_ir_pass_pipeline.hpp"
#include "backend/ir/shared/ir_result.hpp"
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
    return "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:"
           "16:32:64-S128";
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
    PassManager pass_manager;
    append_default_core_ir_pipeline(pass_manager);

    assert(pass_manager.get_pass_by_kind(PassKind::CoreIrFunctionAttrs) !=
           nullptr);
    assert(pass_manager.get_pass_by_kind(PassKind::CoreIrIpsccp) != nullptr);
    assert(pass_manager.get_pass_by_kind(PassKind::CoreIrArgumentPromotion) !=
           nullptr);
    assert(pass_manager.get_pass_by_kind(PassKind::CoreIrInliner) != nullptr);
    assert(pass_manager.get_pass_by_kind(PassKind::CoreIrGlobalDce) != nullptr);
    assert(pass_manager.get_pass_by_kind(PassKind::CoreIrIfConversion) !=
           nullptr);
    assert(pass_manager.get_pass_by_kind(PassKind::CoreIrLoopVectorize) !=
           nullptr);

    const std::vector<PassKind> kinds = pass_manager.get_pipeline_kinds();
    auto index_of = [&kinds](PassKind kind) -> std::size_t {
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            if (kinds[index] == kind) {
                return index;
            }
        }
        return kinds.size();
    };
    assert(index_of(PassKind::CoreIrIfConversion) <
           index_of(PassKind::CoreIrSimpleLoopUnswitch));
    assert(index_of(PassKind::CoreIrSimpleLoopUnswitch) <
           index_of(PassKind::CoreIrLoopIdiom));
    assert(index_of(PassKind::CoreIrLoopIdiom) <
           index_of(PassKind::CoreIrLoopUnroll));
    assert(index_of(PassKind::CoreIrLoopUnroll) <
           index_of(PassKind::CoreIrLoopVectorize));

    assert(pass_manager.Run(context).ok);

    const IRResult *ir_result = context.get_ir_result();
    assert(ir_result != nullptr);
    assert(ir_result->get_kind() == IrKind::LLVM);

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
    expected += "@g = global i32 1\n"
                "\n"
                "define i32 @main() {\n"
                "entry:\n"
                "  %t0 = load i32, ptr @g\n"
                "  %t1.raw = icmp slt i32 %t0, 2\n"
                "  br i1 %t1.raw, label %if.then0, label %if.end0\n"
                "if.then0:\n"
                "  %t2 = add i32 %t0, 3\n"
                "  ret i32 %t2\n"
                "if.end0:\n"
                "  ret i32 0\n"
                "}\n";

    assert(ir_result->get_text() == expected);
    return 0;
}
