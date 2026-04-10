#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/lower/lower_ir_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "backend/ir/sroa/core_ir_sroa_pass.hpp"
#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"
#include "compiler/complier.hpp"
#include "compiler/complier_option.hpp"
#include "compiler/pass/pass.hpp"

using namespace sysycc;

namespace {

CoreIrFunction *find_function(CoreIrModule &module, std::string_view name) {
    for (const auto &function : module.get_functions()) {
        if (function != nullptr && function->get_name() == name) {
            return function.get();
        }
    }
    return nullptr;
}

bool function_has_stack_slot(const CoreIrFunction &function,
                             std::string_view slot_name) {
    for (const auto &stack_slot : function.get_stack_slots()) {
        if (stack_slot != nullptr && stack_slot->get_name() == slot_name) {
            return true;
        }
    }
    return false;
}

void verify_address_taken_store_only_slot_cleanup() {
    auto context = std::make_unique<CoreIrContext>();
    auto *void_type = context->create_type<CoreIrVoidType>();
    auto *i32_type = context->create_type<CoreIrIntegerType>(32);
    auto *ptr_type = context->create_type<CoreIrPointerType>(i32_type);
    auto *function_type = context->create_type<CoreIrFunctionType>(
        i32_type, std::vector<const CoreIrType *>{}, false);
    auto *module = context->create_module<CoreIrModule>(
        "ir_core_pre_ssa_dead_shadow_cleanup_manual");
    auto *function =
        module->create_function<CoreIrFunction>("manual", function_type, false);
    auto *slot =
        function->create_stack_slot<CoreIrStackSlot>("dead.addr", i32_type, 4);
    auto *entry = function->create_basic_block<CoreIrBasicBlock>("entry");
    auto *zero = context->create_constant<CoreIrConstantInt>(i32_type, 0);
    auto *one = context->create_constant<CoreIrConstantInt>(i32_type, 1);
    auto *address =
        entry->create_instruction<CoreIrAddressOfStackSlotInst>(ptr_type, "dead.ptr", slot);
    auto *indexed = entry->create_instruction<CoreIrGetElementPtrInst>(
        ptr_type, "dead.gep", address, std::vector<CoreIrValue *>{zero});
    entry->create_instruction<CoreIrStoreInst>(void_type, one, indexed);
    entry->create_instruction<CoreIrReturnInst>(void_type, zero);

    CompilerContext compiler_context;
    compiler_context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(context), module));

    CoreIrDeadStoreEliminationPass dse_pass;
    assert(dse_pass.Run(compiler_context).ok);

    assert(function->get_stack_slots().empty());
    for (const auto &instruction : entry->get_instructions()) {
        assert(dynamic_cast<CoreIrAddressOfStackSlotInst *>(instruction.get()) ==
               nullptr);
        assert(dynamic_cast<CoreIrGetElementPtrInst *>(instruction.get()) == nullptr);
        assert(dynamic_cast<CoreIrStoreInst *>(instruction.get()) == nullptr);
    }
}

std::string extract_llvm_function(std::string_view text, std::string_view header) {
    const std::size_t begin = text.find(header);
    assert(begin != std::string::npos);
    const std::size_t end = text.find("\n}\n", begin);
    assert(end != std::string::npos);
    return std::string(text.substr(begin, end - begin + 3));
}

} // namespace

int main(int argc, char **argv) {
    assert(argc == 2);

    verify_address_taken_store_only_slot_cleanup();

    ComplierOption option(argv[1]);
    option.set_stop_after_stage(StopAfterStage::Semantic);
    option.set_optimization_level(OptimizationLevel::O1);

    Complier complier(option);
    PassResult frontend_result = complier.Run();
    assert(frontend_result.ok);

    CompilerContext &compiler_context = complier.get_context();
    PassManager pass_manager;
    pass_manager.AddPass(std::make_unique<BuildCoreIrPass>());
    pass_manager.AddPass(std::make_unique<CoreIrCanonicalizePass>());
    pass_manager.AddPass(std::make_unique<CoreIrSimplifyCfgPass>());
    pass_manager.AddPass(std::make_unique<CoreIrLoopSimplifyPass>());
    pass_manager.AddPass(std::make_unique<CoreIrSroaPass>());
    pass_manager.AddPass(std::make_unique<CoreIrInstCombinePass>());
    pass_manager.AddPass(std::make_unique<CoreIrStackSlotForwardPass>());
    pass_manager.AddPass(std::make_unique<CoreIrDeadStoreEliminationPass>());
    pass_manager.AddPass(std::make_unique<CoreIrInstCombinePass>());
    assert(pass_manager.Run(compiler_context).ok);

    CoreIrBuildResult *build_result = compiler_context.get_core_ir_build_result();
    assert(build_result != nullptr);
    CoreIrModule *module = build_result->get_module();
    assert(module != nullptr);

    CoreIrMem2RegPass mem2reg_pass;
    assert(mem2reg_pass.Run(compiler_context).ok);

    CoreIrVerifier verifier;
    assert(verifier.verify_module(*module).ok);

    CoreIrFunction *function = find_function(*module, "dead_shadow");
    assert(function != nullptr);

    const std::vector<std::string> dead_shadow_slots = {
        "n.addr", "A.addr", "B.addr", "C.addr", "X.addr", "y.addr"};
    for (const std::string &slot_name : dead_shadow_slots) {
        assert(!function_has_stack_slot(*function, slot_name));
    }

    CoreIrRawPrinter printer;
    const std::string core_ir_text = printer.print_module(*module);
    for (const std::string &slot_name : dead_shadow_slots) {
        assert(core_ir_text.find("stackslot %" + slot_name) == std::string::npos);
    }

    LowerIrPass lower_ir_pass;
    assert(lower_ir_pass.Run(compiler_context).ok);

    const IRResult *ir_result = compiler_context.get_ir_result();
    assert(ir_result != nullptr);
    assert(ir_result->get_kind() == IrKind::LLVM);

    const std::string llvm_function =
        extract_llvm_function(ir_result->get_text(), "define i32 @dead_shadow(");
    assert(llvm_function.find("alloca") == std::string::npos);
    for (const std::string &slot_name : dead_shadow_slots) {
        assert(llvm_function.find("%" + slot_name) == std::string::npos);
    }
    assert(llvm_function.find("ret i32 %n") != std::string::npos);

    return 0;
}
