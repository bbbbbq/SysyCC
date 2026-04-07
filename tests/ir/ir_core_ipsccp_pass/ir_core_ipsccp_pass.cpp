#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/ipsccp/core_ir_ipsccp_pass.hpp"
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
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *fn_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>("ir_core_ipsccp_pass");
    auto *nine = context->create_constant<CoreIrConstantInt>(i32_type, 9);

    auto *helper =
        module->create_function<CoreIrFunction>("const_helper", fn_type, true);
    auto *helper_entry = helper->create_basic_block<CoreIrBasicBlock>("entry");
    helper_entry->create_instruction<CoreIrReturnInst>(void_type, nine);

    auto *main_fn = module->create_function<CoreIrFunction>("main", fn_type, false);
    auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
    auto *call = main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "call", "const_helper", fn_type, std::vector<CoreIrValue *>{});
    main_entry->create_instruction<CoreIrReturnInst>(void_type, call);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrIpsccpPass pass;
    PassResult result = pass.Run(compiler_context);
    assert(result.ok);

    bool saw_call = false;
    CoreIrReturnInst *ret = nullptr;
    for (const auto &instruction_ptr : main_entry->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (dynamic_cast<CoreIrCallInst *>(instruction) != nullptr) {
            saw_call = true;
        }
        ret = dynamic_cast<CoreIrReturnInst *>(instruction);
    }
    assert(!saw_call);
    assert(ret != nullptr);
    assert(ret->get_return_value() == nine);
    return 0;
}
