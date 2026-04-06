#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "compiler/pass/pass.hpp"

using namespace sysycc;

namespace {

class CountingTransformPass : public Pass {
  public:
    CountingTransformPass(CoreIrFunction *function, int *run_count,
                          int remaining_changes)
        : function_(function), run_count_(run_count),
          remaining_changes_(remaining_changes) {}

    PassKind Kind() const override { return PassKind::CoreIrInstCombine; }

    const char *Name() const override { return "CountingTransformPass"; }

    CoreIrPassMetadata Metadata() const noexcept override {
        return CoreIrPassMetadata::core_ir_transform();
    }

    PassResult Run(CompilerContext &) override {
        ++(*run_count_);
        CoreIrPassEffects effects;
        if (remaining_changes_ > 0 && function_ != nullptr) {
            effects.changed_functions.insert(function_);
            --remaining_changes_;
        } else {
            effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        }
        return PassResult::Success(std::move(effects));
    }

  private:
    CoreIrFunction *function_ = nullptr;
    int *run_count_ = nullptr;
    int remaining_changes_ = 0;
};

CompilerContext make_context(std::unique_ptr<CoreIrContext> context,
                             CoreIrModule *module) {
    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    return compiler_context;
}

void test_fixed_point_stops_on_convergence() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("fixed_point_converge");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context = make_context(std::move(context), module);
    int run_count = 0;
    PassManager manager;
    std::vector<std::unique_ptr<Pass>> group;
    group.push_back(
        std::make_unique<CountingTransformPass>(function, &run_count, 1));
    manager.AddCoreIrFixedPointGroup(std::move(group), 4);
    assert(manager.Run(compiler_context).ok);
    assert(run_count == 2);
}

void test_fixed_point_respects_iteration_cap() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("fixed_point_cap");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context = make_context(std::move(context), module);
    int run_count = 0;
    PassManager manager;
    std::vector<std::unique_ptr<Pass>> group;
    group.push_back(
        std::make_unique<CountingTransformPass>(function, &run_count, 10));
    manager.AddCoreIrFixedPointGroup(std::move(group), 4);
    assert(manager.Run(compiler_context).ok);
    assert(run_count == 4);
}

void test_fixed_point_real_pipeline_contracts_after_instcombine() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("fixed_point_real");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *left = function->create_basic_block<CoreIrBasicBlock>("left");
    auto *right = function->create_basic_block<CoreIrBasicBlock>("right");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
    auto *nine = context->create_constant<CoreIrConstantInt>(i32_type, 9);
    auto *cmp = entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cmp", one, one);
    entry->create_instruction<CoreIrCondJumpInst>(void_type, cmp, left, right);
    left->create_instruction<CoreIrReturnInst>(void_type, seven);
    right->create_instruction<CoreIrReturnInst>(void_type, nine);

    CompilerContext compiler_context = make_context(std::move(context), module);
    PassManager manager;
    std::vector<std::unique_ptr<Pass>> group;
    group.push_back(std::make_unique<CoreIrInstCombinePass>());
    group.push_back(std::make_unique<CoreIrConstFoldPass>());
    group.push_back(std::make_unique<CoreIrDcePass>());
    group.push_back(std::make_unique<CoreIrSimplifyCfgPass>());
    manager.AddCoreIrFixedPointGroup(std::move(group), 4);
    assert(manager.Run(compiler_context).ok);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("br i1") == std::string::npos);
    assert(text.find("right:") == std::string::npos);
    assert(text.find("ret i32 7") != std::string::npos);
}

void test_instcombine_uses_incremental_worklist_stats() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module =
        context->create_module<CoreIrModule>("instcombine_incremental_stats");
    auto *function =
        module->create_function<CoreIrFunction>("main", function_type, false);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *hot = function->create_basic_block<CoreIrBasicBlock>("hot");
    auto *cold = function->create_basic_block<CoreIrBasicBlock>("cold");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    entry->create_instruction<CoreIrJumpInst>(void_type, hot);
    auto *phi = hot->create_instruction<CoreIrPhiInst>(i32_type, "merged");
    phi->add_incoming(entry, one);
    auto *identity_cast = hot->create_instruction<CoreIrCastInst>(
        CoreIrCastKind::ZeroExtend, i32_type, "identity", phi);
    auto *add_zero = hot->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "add_zero", identity_cast, zero);
    auto *cmp_same = hot->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cmp_same", add_zero, add_zero);
    hot->create_instruction<CoreIrReturnInst>(void_type, cmp_same);

    CoreIrValue *cold_value = one;
    for (int index = 0; index < 24; ++index) {
        cold_value = cold->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type,
            "cold_" + std::to_string(index), cold_value, zero);
    }
    cold->create_instruction<CoreIrReturnInst>(void_type, cold_value);

    CompilerContext compiler_context = make_context(std::move(context), module);
    CoreIrInstCombinePass pass;
    assert(pass.Run(compiler_context).ok);

    const CoreIrInstCombineStats &stats =
        get_instcombine_stats_for_testing(pass);
    assert(stats.rewrites >= 4);
    assert(stats.visited_instructions < 90);

    CoreIrRawPrinter printer;
    const std::string text = printer.print_module(*module);
    assert(text.find("%merged =") == std::string::npos);
    assert(text.find("%identity =") == std::string::npos);
    assert(text.find("%add_zero =") == std::string::npos);
    assert(text.find("%cmp_same =") == std::string::npos);
    assert(text.find("ret i1 1") != std::string::npos);
}

} // namespace

int main() {
    test_fixed_point_stops_on_convergence();
    test_fixed_point_respects_iteration_cap();
    test_fixed_point_real_pipeline_contracts_after_instcombine();
    test_instcombine_uses_incremental_worklist_stats();
    return 0;
}
