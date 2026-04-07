#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/inliner/core_ir_inliner_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

int main() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i1_type = context->create_type<CoreIrIntegerType>(1);
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *callee_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{i32_type}, false);
    auto *main_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_inliner_pass");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *five = context->create_constant<CoreIrConstantInt>(i32_type, 5);

    auto *helper =
        module->create_function<CoreIrFunction>("helper", callee_type, true);
    helper->set_is_always_inline(true);
    auto *param = helper->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *helper_entry = helper->create_basic_block<CoreIrBasicBlock>("entry");
    auto *sum = helper_entry->create_instruction<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Add, i32_type, "sum", param, one);
    helper_entry->create_instruction<CoreIrReturnInst>(void_type, sum);

    auto *too_complex =
        module->create_function<CoreIrFunction>("too_complex", callee_type, true);
    auto *too_complex_param =
        too_complex->create_parameter<CoreIrParameter>(i32_type, "x");
    auto *too_complex_entry =
        too_complex->create_basic_block<CoreIrBasicBlock>("entry");
    auto *too_complex_exit =
        too_complex->create_basic_block<CoreIrBasicBlock>("exit");
    auto *cond = too_complex_entry->create_instruction<CoreIrCompareInst>(
        CoreIrComparePredicate::Equal, i1_type, "cond", too_complex_param, zero);
    too_complex_entry->create_instruction<CoreIrCondJumpInst>(
        void_type, cond, too_complex_exit, too_complex_exit);
    too_complex_exit->create_instruction<CoreIrReturnInst>(void_type, too_complex_param);

    auto *main_fn = module->create_function<CoreIrFunction>("main", main_type, false);
    auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
    auto *simple_call = main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "call", "helper", callee_type, std::vector<CoreIrValue *>{five});
    auto *complex_call = main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "complex", "too_complex", callee_type,
        std::vector<CoreIrValue *>{simple_call});
    main_entry->create_instruction<CoreIrReturnInst>(void_type, complex_call);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrInlinerPass pass;
    PassResult result = pass.Run(compiler_context);
    assert(result.ok);

    bool saw_helper_call = false;
    bool saw_complex_call = false;
    bool saw_add = false;
    for (const auto &instruction_ptr : main_entry->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        auto *call = dynamic_cast<CoreIrCallInst *>(instruction);
        if (call != nullptr && call->get_is_direct_call()) {
            if (call->get_callee_name() == "helper") {
                saw_helper_call = true;
            }
            if (call->get_callee_name() == "too_complex") {
                saw_complex_call = true;
            }
        }
        if (dynamic_cast<CoreIrBinaryInst *>(instruction) != nullptr) {
            saw_add = true;
        }
    }
    assert(!saw_helper_call);
    assert(saw_complex_call);
    assert(saw_add);
    return 0;
}
