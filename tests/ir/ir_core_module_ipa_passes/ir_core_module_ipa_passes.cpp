#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include "backend/ir/argument_promotion/core_ir_argument_promotion_pass.hpp"
#include "backend/ir/global_dce/core_ir_global_dce_pass.hpp"
#include "backend/ir/inliner/core_ir_inliner_pass.hpp"
#include "backend/ir/ipsccp/core_ir_ipsccp_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_global.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
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
        auto *i32_ptr_type = context->create_type<CoreIrPointerType>(i32_type);
        auto *noarg_i32 = context->create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{}, false);
        auto *module = context->create_module<CoreIrModule>("global_dce");

        auto *live_global = module->create_global<CoreIrGlobal>(
            "live", i32_type, context->create_constant<CoreIrConstantInt>(i32_type, 1),
            true, false);
        module->create_global<CoreIrGlobal>(
            "dead", i32_type, context->create_constant<CoreIrConstantInt>(i32_type, 2),
            true, false);

        auto *live_fn =
            module->create_function<CoreIrFunction>("live_helper", noarg_i32, true);
        auto *live_entry = live_fn->create_basic_block<CoreIrBasicBlock>("entry");
        live_entry->create_instruction<CoreIrReturnInst>(
            void_type, context->create_constant<CoreIrConstantInt>(i32_type, 3));

        auto *dead_fn =
            module->create_function<CoreIrFunction>("dead_helper", noarg_i32, true);
        auto *dead_entry = dead_fn->create_basic_block<CoreIrBasicBlock>("entry");
        dead_entry->create_instruction<CoreIrReturnInst>(
            void_type, context->create_constant<CoreIrConstantInt>(i32_type, 4));

        auto *main_fn = module->create_function<CoreIrFunction>("main", noarg_i32, false);
        auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
        auto *live_addr = main_entry->create_instruction<CoreIrAddressOfGlobalInst>(
            i32_ptr_type, "live.addr", live_global);
        auto *live_load =
            main_entry->create_instruction<CoreIrLoadInst>(i32_type, "live.load", live_addr);
        auto *live_call = main_entry->create_instruction<CoreIrCallInst>(
            i32_type, "live.call", "live_helper", noarg_i32, std::vector<CoreIrValue *>{});
        auto *sum = main_entry->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type, "sum", live_load, live_call);
        main_entry->create_instruction<CoreIrReturnInst>(void_type, sum);

        CompilerContext compiler_context = make_context(std::move(context), module);
        CoreIrGlobalDcePass pass;
        assert(pass.Run(compiler_context).ok);
        assert(module->find_function("dead_helper") == nullptr);
        assert(module->find_global("dead") == nullptr);
        assert(module->find_function("live_helper") != nullptr);
        assert(module->find_global("live") != nullptr);
    }

    {
        auto context = std::make_unique<CoreIrContext>();
        auto *void_type = context->create_type<CoreIrVoidType>();
        auto *i32_type = context->create_type<CoreIrIntegerType>(32);
        auto *noarg_i32 = context->create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{}, false);
        auto *module = context->create_module<CoreIrModule>("inliner");
        auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
        auto *two = context->create_constant<CoreIrConstantInt>(i32_type, 2);

        auto *inline_fn =
            module->create_function<CoreIrFunction>("inline_me", noarg_i32, true);
        auto *inline_entry = inline_fn->create_basic_block<CoreIrBasicBlock>("entry");
        auto *inline_sum = inline_entry->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type, "inline.sum", one, two);
        inline_entry->create_instruction<CoreIrReturnInst>(void_type, inline_sum);

        auto *skip_fn =
            module->create_function<CoreIrFunction>("skip_me", noarg_i32, true);
        auto *skip_entry = skip_fn->create_basic_block<CoreIrBasicBlock>("entry");
        auto *skip_exit = skip_fn->create_basic_block<CoreIrBasicBlock>("exit");
        skip_entry->create_instruction<CoreIrJumpInst>(void_type, skip_exit);
        skip_exit->create_instruction<CoreIrReturnInst>(void_type, one);

        auto *main_fn = module->create_function<CoreIrFunction>("main", noarg_i32, false);
        auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
        auto *inline_call = main_entry->create_instruction<CoreIrCallInst>(
            i32_type, "inline.call", "inline_me", noarg_i32, std::vector<CoreIrValue *>{});
        auto *skip_call = main_entry->create_instruction<CoreIrCallInst>(
            i32_type, "skip.call", "skip_me", noarg_i32, std::vector<CoreIrValue *>{});
        auto *main_sum = main_entry->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type, "main.sum", inline_call, skip_call);
        main_entry->create_instruction<CoreIrReturnInst>(void_type, main_sum);

        CompilerContext compiler_context = make_context(std::move(context), module);
        CoreIrInlinerPass pass;
        assert(pass.Run(compiler_context).ok);

        CoreIrRawPrinter printer;
        const std::string text = printer.print_module(*module);
        assert(text.find("call i32 @inline_me") == std::string::npos);
        assert(text.find("call i32 @skip_me") != std::string::npos);
        assert(text.find("%inline.sum = add i32 1, 2") != std::string::npos);
    }

    {
        auto context = std::make_unique<CoreIrContext>();
        auto *void_type = context->create_type<CoreIrVoidType>();
        auto *i32_type = context->create_type<CoreIrIntegerType>(32);
        auto *noarg_i32 = context->create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{}, false);
        auto *module = context->create_module<CoreIrModule>("ipsccp");

        auto *const_fn =
            module->create_function<CoreIrFunction>("const_fn", noarg_i32, true);
        auto *const_entry = const_fn->create_basic_block<CoreIrBasicBlock>("entry");
        auto *forty_two =
            context->create_constant<CoreIrConstantInt>(i32_type, 42);
        auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
        const_entry->create_instruction<CoreIrReturnInst>(void_type, forty_two);

        auto *main_fn = module->create_function<CoreIrFunction>("main", noarg_i32, false);
        auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
        auto *call = main_entry->create_instruction<CoreIrCallInst>(
            i32_type, "const.call", "const_fn", noarg_i32, std::vector<CoreIrValue *>{});
        auto *sum = main_entry->create_instruction<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Add, i32_type, "sum", call, one);
        main_entry->create_instruction<CoreIrReturnInst>(void_type, sum);

        CompilerContext compiler_context = make_context(std::move(context), module);
        CoreIrIpsccpPass pass;
        assert(pass.Run(compiler_context).ok);

        CoreIrRawPrinter printer;
        const std::string text = printer.print_module(*module);
        assert(text.find("call i32 @const_fn") == std::string::npos);
        assert(text.find("%sum = add i32 42, 1") != std::string::npos);
    }

    {
        auto context = std::make_unique<CoreIrContext>();
        auto *void_type = context->create_type<CoreIrVoidType>();
        auto *i32_type = context->create_type<CoreIrIntegerType>(32);
        auto *i32_ptr_type = context->create_type<CoreIrPointerType>(i32_type);
        auto *callee_type = context->create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{i32_ptr_type}, false);
        auto *module = context->create_module<CoreIrModule>("argprom");

        auto *callee =
            module->create_function<CoreIrFunction>("id_load", callee_type, true);
        auto *param = callee->create_parameter<CoreIrParameter>(i32_ptr_type, "p");
        auto *callee_entry = callee->create_basic_block<CoreIrBasicBlock>("entry");
        auto *loaded =
            callee_entry->create_instruction<CoreIrLoadInst>(i32_type, "loaded", param);
        callee_entry->create_instruction<CoreIrReturnInst>(void_type, loaded);

        auto *main_type = context->create_type<CoreIrFunctionType>(
            i32_type, std::vector<const CoreIrType *>{}, false);
        auto *main_fn = module->create_function<CoreIrFunction>("main", main_type, false);
        auto *main_entry = main_fn->create_basic_block<CoreIrBasicBlock>("entry");
        auto *slot = main_fn->create_stack_slot<CoreIrStackSlot>("value", i32_type, 4);
        auto *seven = context->create_constant<CoreIrConstantInt>(i32_type, 7);
        main_entry->create_instruction<CoreIrStoreInst>(void_type, seven, slot);
        auto *addr = main_entry->create_instruction<CoreIrAddressOfStackSlotInst>(
            i32_ptr_type, "value.addr", slot);
        auto *call = main_entry->create_instruction<CoreIrCallInst>(
            i32_type, "call", "id_load", callee_type, std::vector<CoreIrValue *>{addr});
        main_entry->create_instruction<CoreIrReturnInst>(void_type, call);

        CompilerContext compiler_context = make_context(std::move(context), module);
        CoreIrArgumentPromotionPass pass;
        assert(pass.Run(compiler_context).ok);

        assert(callee->get_parameters().size() == 1);
        assert(callee->get_parameters()[0]->get_type() == i32_ptr_type);

        CoreIrFunction *promoted = nullptr;
        for (const auto &function_ptr : module->get_functions()) {
            if (function_ptr != nullptr &&
                function_ptr->get_name().find("id_load.argprom.") == 0) {
                promoted = function_ptr.get();
                break;
            }
        }
        assert(promoted != nullptr);
        assert(promoted->get_parameters().size() == 1);
        assert(promoted->get_parameters()[0]->get_type() == i32_type);

        bool saw_promoted_load = false;
        bool saw_promoted_call = false;
        for (const auto &instruction_ptr : main_entry->get_instructions()) {
            auto *instruction = instruction_ptr.get();
            if (auto *load_inst = dynamic_cast<CoreIrLoadInst *>(instruction);
                load_inst != nullptr && load_inst->get_name() == "call.argprom.load") {
                saw_promoted_load = true;
                assert(load_inst->get_type() == i32_type);
                assert(load_inst->get_address() == addr);
            }
            if (auto *call_inst = dynamic_cast<CoreIrCallInst *>(instruction);
                call_inst != nullptr &&
                call_inst->get_callee_name() == promoted->get_name()) {
                saw_promoted_call = true;
                assert(call_inst->get_argument_count() == 1);
                assert(call_inst->get_argument(0) != nullptr);
                assert(call_inst->get_argument(0)->get_type() == i32_type);
            }
        }
        assert(saw_promoted_load);
        assert(saw_promoted_call);

        for (const auto &instruction_ptr : promoted->get_basic_blocks().front()->get_instructions()) {
            auto *load = dynamic_cast<CoreIrLoadInst *>(instruction_ptr.get());
            assert(load == nullptr ||
                   load->get_address() != promoted->get_parameters()[0].get());
        }
    }

    return 0;
}
