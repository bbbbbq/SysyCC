#include <cassert>
#include <memory>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_analysis_invalidation");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *middle = function->create_basic_block<CoreIrBasicBlock>("middle");
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);

    entry->create_instruction<CoreIrJumpInst>(void_type, middle);
    middle->create_instruction<CoreIrReturnInst>(void_type, seven);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrBuildResult *build_result = compiler_context.get_core_ir_build_result();
    assert(build_result != nullptr);
    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    assert(analysis_manager != nullptr);

    const CoreIrCfgAnalysisResult &before =
        analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    assert(before.get_successors(entry).size() == 1);
    assert(before.get_successors(entry).front() == middle);

    CoreIrSimplifyCfgPass simplify_cfg_pass;
    assert(simplify_cfg_pass.Run(compiler_context).ok);
    assert(function->get_basic_blocks().size() == 1);

    const CoreIrCfgAnalysisResult &after =
        analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
    assert(after.get_entry_block() == entry);
    assert(after.get_successors(entry).empty());
    assert(after.get_predecessor_count(entry) == 0);
    return 0;
}
