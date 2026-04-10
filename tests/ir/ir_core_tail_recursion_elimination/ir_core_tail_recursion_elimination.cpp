#include <cassert>
#include <memory>
#include <string>
#include <string_view>

#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
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
#include "backend/ir/tail_recursion_elimination/core_ir_tail_recursion_elimination_pass.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"
#include "compiler/complier.hpp"
#include "compiler/complier_option.hpp"
#include "compiler/pass/pass.hpp"

using namespace sysycc;

namespace {

std::string extract_core_ir_function(std::string_view text, std::string_view header) {
    const std::size_t begin = text.find(header);
    assert(begin != std::string::npos);
    const std::size_t end = text.find("\n}\n", begin);
    assert(end != std::string::npos);
    return std::string(text.substr(begin, end - begin + 3));
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

    ComplierOption option(argv[1]);
    option.set_stop_after_stage(StopAfterStage::Semantic);
    option.set_optimization_level(OptimizationLevel::O1);

    Complier complier(option);
    assert(complier.Run().ok);

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
    pass_manager.AddPass(std::make_unique<CoreIrMem2RegPass>());
    pass_manager.AddPass(
        std::make_unique<CoreIrTailRecursionEliminationPass>());
    assert(pass_manager.Run(compiler_context).ok);

    CoreIrBuildResult *build_result = compiler_context.get_core_ir_build_result();
    assert(build_result != nullptr);
    CoreIrModule *module = build_result->get_module();
    assert(module != nullptr);

    CoreIrVerifier verifier;
    assert(verifier.verify_module(*module).ok);

    CoreIrRawPrinter printer;
    const std::string core_ir_text = printer.print_module(*module);
    const std::string core_ir_fun =
        extract_core_ir_function(core_ir_text, "func @fun(");
    const std::string core_ir_non_tail =
        extract_core_ir_function(core_ir_text, "func @non_tail(");
    assert(core_ir_fun.find("call i32 @fun") == std::string::npos);
    assert(core_ir_fun.find("%n.tr = phi i32") != std::string::npos);
    assert(core_ir_fun.find("%dep.tr = phi i32") != std::string::npos);
    assert(core_ir_non_tail.find("call i32 @non_tail") != std::string::npos);

    LowerIrPass lower_ir_pass;
    assert(lower_ir_pass.Run(compiler_context).ok);
    const IRResult *ir_result = compiler_context.get_ir_result();
    assert(ir_result != nullptr);

    const std::string llvm_fun =
        extract_llvm_function(ir_result->get_text(), "define i32 @fun(");
    const std::string llvm_non_tail =
        extract_llvm_function(ir_result->get_text(), "define i32 @non_tail(");
    assert(llvm_fun.find("call i32 @fun") == std::string::npos);
    assert(llvm_fun.find("phi i32") != std::string::npos);
    assert(llvm_non_tail.find("call i32 @non_tail") != std::string::npos);

    return 0;
}
