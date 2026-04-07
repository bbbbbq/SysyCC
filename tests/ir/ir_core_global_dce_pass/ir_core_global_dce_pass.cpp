#include <cassert>
#include <memory>
#include <vector>

#include "backend/ir/global_dce/core_ir_global_dce_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
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
    auto *module = context->create_module<CoreIrModule>("ir_core_global_dce");
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

    auto *live_global =
        module->create_global<CoreIrGlobal>("live_g", i32_type, one, false, false);
    module->create_global<CoreIrGlobal>("dead_g", i32_type, two, true, false);

    auto *live_helper =
        module->create_function<CoreIrFunction>("live_helper", fn_type, true);
    auto *live_helper_entry =
        live_helper->create_basic_block<CoreIrBasicBlock>("entry");
    auto *live_addr = live_helper_entry->create_instruction<CoreIrAddressOfGlobalInst>(
        context->create_type<CoreIrPointerType>(i32_type), "live.addr", live_global);
    auto *live_load = live_helper_entry->create_instruction<CoreIrLoadInst>(
        i32_type, "live.load", live_addr);
    live_helper_entry->create_instruction<CoreIrReturnInst>(void_type, live_load);

    auto *dead_helper =
        module->create_function<CoreIrFunction>("dead_helper", fn_type, true);
    auto *dead_entry = dead_helper->create_basic_block<CoreIrBasicBlock>("entry");
    dead_entry->create_instruction<CoreIrReturnInst>(void_type, two);

    auto *main_fn = module->create_function<CoreIrFunction>("main", fn_type, false);
    auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
    auto *call = main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "call", "live_helper", fn_type, std::vector<CoreIrValue *>{});
    main_entry->create_instruction<CoreIrReturnInst>(void_type, call);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrGlobalDcePass pass;
    PassResult result = pass.Run(compiler_context);
    assert(result.ok);
    assert(module->find_function("live_helper") != nullptr);
    assert(module->find_function("dead_helper") == nullptr);
    assert(module->find_global("live_g") != nullptr);
    assert(module->find_global("dead_g") == nullptr);
    return 0;
}
