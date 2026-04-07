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
    auto *ptr_i32_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *ptr_fn_type = context->create_type<CoreIrPointerType>(fn_type);
    auto *module = context->create_module<CoreIrModule>("ir_core_global_dce");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);

    auto *used_global = module->create_global<CoreIrGlobal>(
        "used_global", i32_type, zero, true, false);
    auto *global_via_initializer = module->create_global<CoreIrGlobal>(
        "global_via_initializer", i32_type, zero, true, false);
    auto *exported_global = module->create_global<CoreIrGlobal>(
        "exported_global", i32_type, zero, false, false);
    module->create_global<CoreIrGlobal>("dead_global", i32_type, zero, true, false);

    auto *main_fn = module->create_function<CoreIrFunction>("main", fn_type, false);
    auto *used_helper =
        module->create_function<CoreIrFunction>("used_helper", fn_type, true);
    auto *dead_helper =
        module->create_function<CoreIrFunction>("dead_helper", fn_type, true);
    auto *callback =
        module->create_function<CoreIrFunction>("callback", fn_type, true);
    auto *callback_via_initializer =
        module->create_function<CoreIrFunction>("callback_via_initializer", fn_type, true);
    auto *exported_api =
        module->create_function<CoreIrFunction>("exported_api", fn_type, false);

    module->create_global<CoreIrGlobal>(
        "exported_global_ref", ptr_i32_type,
        context->create_constant<CoreIrConstantGlobalAddress>(ptr_i32_type,
                                                              global_via_initializer),
        false, true);
    module->create_global<CoreIrGlobal>(
        "exported_callback_ref", ptr_fn_type,
        context->create_constant<CoreIrConstantGlobalAddress>(ptr_fn_type,
                                                              callback_via_initializer),
        false, true);

    auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
    auto *used_global_addr = main_entry->create_instruction<CoreIrAddressOfGlobalInst>(
        ptr_i32_type, "g.addr", used_global);
    main_entry->create_instruction<CoreIrLoadInst>(i32_type, "g.load", used_global_addr);
    auto *callback_addr = main_entry->create_instruction<CoreIrAddressOfFunctionInst>(
        ptr_fn_type, "cb.addr", callback);
    main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "cb.call", callback_addr, fn_type, std::vector<CoreIrValue *>{});
    main_entry->create_instruction<CoreIrCallInst>(
        i32_type, "used.call", "used_helper", fn_type, std::vector<CoreIrValue *>{});
    main_entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    auto *used_entry = used_helper->create_basic_block<CoreIrBasicBlock>("entry");
    used_entry->create_instruction<CoreIrReturnInst>(void_type, zero);
    auto *dead_entry = dead_helper->create_basic_block<CoreIrBasicBlock>("entry");
    dead_entry->create_instruction<CoreIrReturnInst>(void_type, zero);
    auto *callback_entry = callback->create_basic_block<CoreIrBasicBlock>("entry");
    callback_entry->create_instruction<CoreIrReturnInst>(void_type, zero);
    auto *callback_init_entry =
        callback_via_initializer->create_basic_block<CoreIrBasicBlock>("entry");
    callback_init_entry->create_instruction<CoreIrReturnInst>(void_type, zero);
    auto *exported_api_entry =
        exported_api->create_basic_block<CoreIrBasicBlock>("entry");
    exported_api_entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrGlobalDcePass pass;
    assert(pass.Run(compiler_context).ok);

    assert(module->find_function("main") != nullptr);
    assert(module->find_function("used_helper") != nullptr);
    assert(module->find_function("callback") != nullptr);
    assert(module->find_function("callback_via_initializer") != nullptr);
    assert(module->find_function("exported_api") != nullptr);
    assert(module->find_function("dead_helper") == nullptr);
    assert(module->find_global("used_global") != nullptr);
    assert(module->find_global("global_via_initializer") != nullptr);
    assert(module->find_global("exported_global") != nullptr);
    assert(module->find_global("exported_global_ref") != nullptr);
    assert(module->find_global("exported_callback_ref") != nullptr);
    assert(module->find_global("dead_global") == nullptr);
    return 0;
}
