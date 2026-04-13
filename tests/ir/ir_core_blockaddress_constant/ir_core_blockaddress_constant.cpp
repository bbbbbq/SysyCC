#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "backend/ir/global_dce/core_ir_global_dce_pass.hpp"
#include "backend/ir/lower/lowering/llvm/core_ir_llvm_target_backend.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"
#include "compiler/compiler_context/compiler_context.hpp"

using namespace sysycc;

namespace {

CompilerContext make_context(std::unique_ptr<CoreIrContext> context,
                             CoreIrModule *module) {
    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));
    return compiler_context;
}

} // namespace

int main() {
    {
        auto context = std::make_unique<CoreIrContext>();
        auto *void_type = context->create_type<CoreIrVoidType>();
        auto *i32_type = context->create_type<CoreIrIntegerType>(32);
        auto *i64_type = context->create_type<CoreIrIntegerType>(64);
        auto *ptr_void_type = context->create_type<CoreIrPointerType>(void_type);
        auto *fn_type = context->create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{}, false);
        auto *module = context->create_module<CoreIrModule>("ir_core_blockaddress");

        auto *foo = module->create_function<CoreIrFunction>("foo", fn_type, false);
        auto *entry = foo->create_basic_block<CoreIrBasicBlock>("entry");
        auto *target = foo->create_basic_block<CoreIrBasicBlock>("target");
        auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
        entry->create_instruction<CoreIrJumpInst>(void_type, target);
        target->create_instruction<CoreIrReturnInst>(void_type, one);

        auto *blockaddr = context->create_constant<CoreIrConstantBlockAddress>(
            ptr_void_type, "foo", "target");
        module->create_global<CoreIrGlobal>("jump_target", ptr_void_type, blockaddr,
                                            false, true);
        module->create_global<CoreIrGlobal>(
            "jump_target_i64", i64_type,
            context->create_constant<CoreIrConstantCast>(i64_type,
                                                         CoreIrCastKind::PtrToInt,
                                                         blockaddr),
            false, true);

        CoreIrRawPrinter printer;
        const std::string raw = printer.print_module(*module);
        assert(raw.find("blockaddress(@foo, %target)") != std::string::npos);
        assert(raw.find("const @jump_target") != std::string::npos);
        assert(raw.find("const @jump_target_i64") != std::string::npos);

        DiagnosticEngine diagnostics;
        CoreIrLlvmTargetBackend backend;
        std::unique_ptr<IRResult> lowered = backend.Lower(*module, diagnostics);
        assert(lowered != nullptr);
        const std::string &llvm = lowered->get_text();
        assert(llvm.find("blockaddress(@foo, %target)") != std::string::npos);
        assert(llvm.find("@jump_target") != std::string::npos);
        assert(llvm.find("@jump_target_i64") != std::string::npos);
    }

    {
        auto context = std::make_unique<CoreIrContext>();
        auto *void_type = context->create_type<CoreIrVoidType>();
        auto *i32_type = context->create_type<CoreIrIntegerType>(32);
        auto *ptr_void_type = context->create_type<CoreIrPointerType>(void_type);
        auto *fn_type = context->create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{}, false);
        auto *module =
            context->create_module<CoreIrModule>("ir_core_blockaddress_dce");

        auto *main_fn = module->create_function<CoreIrFunction>("main", fn_type, false);
        auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
        auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
        main_entry->create_instruction<CoreIrReturnInst>(void_type, zero);

        auto *kept = module->create_function<CoreIrFunction>("kept", fn_type, true);
        auto *kept_entry = kept->create_basic_block<CoreIrBasicBlock>("entry");
        auto *kept_target = kept->create_basic_block<CoreIrBasicBlock>("target");
        kept_entry->create_instruction<CoreIrJumpInst>(void_type, kept_target);
        kept_target->create_instruction<CoreIrReturnInst>(void_type, zero);

        auto *dead = module->create_function<CoreIrFunction>("dead", fn_type, true);
        auto *dead_entry = dead->create_basic_block<CoreIrBasicBlock>("entry");
        dead_entry->create_instruction<CoreIrReturnInst>(void_type, zero);

        auto *blockaddr = context->create_constant<CoreIrConstantBlockAddress>(
            ptr_void_type, "kept", "target");
        module->create_global<CoreIrGlobal>("keep_blockaddr", ptr_void_type, blockaddr,
                                            false, true);

        CompilerContext compiler_context = make_context(std::move(context), module);
        CoreIrGlobalDcePass pass;
        assert(pass.Run(compiler_context).ok);

        assert(module->find_function("main") != nullptr);
        assert(module->find_function("kept") != nullptr);
        assert(module->find_function("dead") == nullptr);
        assert(module->find_global("keep_blockaddr") != nullptr);
    }

    return 0;
}
