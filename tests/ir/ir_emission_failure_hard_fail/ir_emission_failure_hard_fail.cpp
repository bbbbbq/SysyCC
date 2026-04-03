#include <algorithm>
#include <cassert>
#include <memory>
#include <string>

#include "backend/ir/ir_backend.hpp"
#include "backend/ir/ir_builder.hpp"
#include "backend/ir/llvm/llvm_ir_backend.hpp"
#include "common/diagnostic/diagnostic.hpp"
#include "compiler/complier.hpp"
#include "compiler/complier_option.hpp"

using namespace sysycc;

namespace {

class FailingBinaryBackend : public LlvmIrBackend {
  public:
    IRValue emit_binary(const std::string &op, const IRValue &lhs,
                        const IRValue &rhs,
                        const SemanticType *result_type) override {
        if (op == "+") {
            return {};
        }
        return LlvmIrBackend::emit_binary(op, lhs, rhs, result_type);
    }
};

} // namespace

int main(int argc, char **argv) {
    assert(argc == 2);

    ComplierOption option(argv[1]);
    option.set_stop_after_stage(StopAfterStage::Semantic);

    Complier complier(option);
    PassResult frontend_result = complier.Run();
    assert(frontend_result.ok);

    CompilerContext &context = complier.get_context();
    FailingBinaryBackend backend;
    IRBuilder builder(backend);

    std::unique_ptr<IRResult> ir_result = builder.Build(context);
    assert(ir_result == nullptr);

    const auto &diagnostics = context.get_diagnostic_engine().get_diagnostics();
    const auto diagnostic_it = std::find_if(
        diagnostics.begin(), diagnostics.end(), [](const Diagnostic &diagnostic) {
            return diagnostic.get_level() == DiagnosticLevel::Error &&
                   diagnostic.get_stage() == DiagnosticStage::Compiler &&
                   diagnostic.get_message().find(
                       "ir generation failed while emitting function definition: "
                       "main") != std::string::npos;
        });
    assert(diagnostic_it != diagnostics.end());
    assert(diagnostic_it->get_message().find("backend emission returned failure") !=
           std::string::npos);
    return 0;
}
