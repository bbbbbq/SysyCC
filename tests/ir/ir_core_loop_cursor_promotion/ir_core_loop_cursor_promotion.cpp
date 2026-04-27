#include <cassert>
#include <memory>
#include <string>
#include <string_view>

#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/loop_cursor_promotion/core_ir_loop_cursor_promotion_pass.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/lower/lower_ir_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "backend/ir/sroa/core_ir_sroa_pass.hpp"
#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"
#include "compiler/compiler.hpp"
#include "compiler/compiler_option.hpp"
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

    CompilerOption option(argv[1]);
    option.set_stop_after_stage(StopAfterStage::Semantic);
    option.set_optimization_level(OptimizationLevel::O1);

    Compiler compiler(option);
    assert(compiler.Run().ok);

    CompilerContext &compiler_context = compiler.get_context();
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
    pass_manager.AddPass(std::make_unique<CoreIrLoopCursorPromotionPass>());
    pass_manager.AddPass(std::make_unique<CoreIrDeadStoreEliminationPass>());
    pass_manager.AddPass(std::make_unique<CoreIrMem2RegPass>());
    assert(pass_manager.Run(compiler_context).ok);

    CoreIrBuildResult *build_result = compiler_context.get_core_ir_build_result();
    assert(build_result != nullptr);
    CoreIrModule *module = build_result->get_module();
    assert(module != nullptr);
    CoreIrFunction *pair_function = find_function(*module, "cursor_pair");
    CoreIrFunction *nested_function = find_function(*module, "cursor_nested_accum");
    CoreIrFunction *exit_function = find_function(*module, "cursor_exit_accum");
    assert(pair_function != nullptr);
    assert(nested_function != nullptr);
    assert(exit_function != nullptr);

    CoreIrVerifier verifier;
    assert(verifier.verify_module(*module).ok);

    CoreIrRawPrinter printer;
    const std::string core_ir_text = printer.print_module(*module);
    assert(core_ir_text.find("%j.addr.cursor.") != std::string::npos);
    assert(core_ir_text.find("%k.addr.cursor.") != std::string::npos);
    assert(core_ir_text.find("%w.addr.cursor.") != std::string::npos);
    assert(core_ir_text.find("load i32, %j.addr") == std::string::npos);
    assert(core_ir_text.find("load i32, %k.addr") == std::string::npos);
    assert(core_ir_text.find("load i32, %w.addr") == std::string::npos);
    assert(core_ir_text.find("store i32 %j.addr.cursor.") == std::string::npos);
    assert(core_ir_text.find("store i32 %k.addr.cursor.") == std::string::npos);
    assert(core_ir_text.find("store i32 %w.addr.cursor.") == std::string::npos);

    LowerIrPass lower_ir_pass;
    assert(lower_ir_pass.Run(compiler_context).ok);
    const IRResult *ir_result = compiler_context.get_ir_result();
    assert(ir_result != nullptr);
    const std::string llvm_pair_function =
        extract_llvm_function(ir_result->get_text(), "define i32 @cursor_pair(");
    const std::string llvm_nested_function =
        extract_llvm_function(ir_result->get_text(),
                              "define i32 @cursor_nested_accum(");
    const std::string llvm_exit_function =
        extract_llvm_function(ir_result->get_text(), "define i32 @cursor_exit_accum(");
    assert(llvm_pair_function.find("%j.addr") == std::string::npos);
    assert(llvm_nested_function.find("%j.addr") == std::string::npos);
    assert(llvm_nested_function.find("%k.addr") == std::string::npos);
    assert(llvm_exit_function.find("%w.addr") == std::string::npos);

    return 0;
}
