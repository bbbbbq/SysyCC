#include "backend/ir/ir_pass.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "backend/ir/ir_result.hpp"
#include "backend/ir/pipeline/core_ir_pipeline.hpp"

namespace sysycc {

PassKind IRGenPass::Kind() const { return PassKind::IRGen; }

const char *IRGenPass::Name() const { return "IRGenPass"; }

PassResult IRGenPass::Run(CompilerContext &context) {
    context.clear_ir_result();
    context.set_ir_dump_file_path("");

    CoreIrPipeline pipeline(IrKind::LLVM);
    std::unique_ptr<IRResult> ir_result = pipeline.BuildOptimizeAndLower(context);
    if (ir_result == nullptr) {
        return PassResult::Failure("failed to build ir result");
    }

    context.set_ir_result(std::move(ir_result));
    if (context.get_dump_ir() && context.get_ir_result() != nullptr) {
        const std::filesystem::path output_dir("build/intermediate_results");
        std::filesystem::create_directories(output_dir);

        const std::filesystem::path input_path(context.get_input_file());
        std::string extension = ".ir";
        switch (context.get_ir_result()->get_kind()) {
        case IrKind::LLVM:
            extension = ".ll";
            break;
        case IrKind::AArch64:
            extension = ".s";
            break;
        case IrKind::None:
            break;
        }
        const std::filesystem::path output_file =
            output_dir / (input_path.stem().string() + extension);
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
