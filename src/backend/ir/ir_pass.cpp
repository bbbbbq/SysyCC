#include "backend/ir/ir_pass.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "backend/ir/ir_backend.hpp"
#include "backend/ir/ir_backend_factory.hpp"
#include "backend/ir/ir_builder.hpp"
#include "backend/ir/ir_result.hpp"

namespace sysycc {

PassKind IRGenPass::Kind() const { return PassKind::IRGen; }

const char *IRGenPass::Name() const { return "IRGenPass"; }

PassResult IRGenPass::Run(CompilerContext &context) {
    context.clear_ir_result();
    context.set_ir_dump_file_path("");

    auto backend = create_ir_backend(IrKind::LLVM);
    if (backend == nullptr) {
        return PassResult::Failure("failed to create ir backend");
    }

    IRBuilder builder(*backend);
    std::unique_ptr<IRResult> ir_result = builder.Build(context);
    if (ir_result == nullptr) {
        return PassResult::Failure("failed to build ir result");
    }

    context.set_ir_result(std::move(ir_result));
    if (context.get_dump_ir() && context.get_ir_result() != nullptr) {
        const std::filesystem::path output_dir("build/intermediate_results");
        std::filesystem::create_directories(output_dir);

        const std::filesystem::path input_path(context.get_input_file());
        const std::filesystem::path output_file =
            output_dir / (input_path.stem().string() + ".ll");
        std::ofstream ofs(output_file);
        if (!ofs.is_open()) {
            return PassResult::Failure("failed to open ir dump file");
        }
        ofs << context.get_ir_result()->get_text();
        context.set_ir_dump_file_path(output_file.string());
    }
    return PassResult::Success();
}

} // namespace sysycc
