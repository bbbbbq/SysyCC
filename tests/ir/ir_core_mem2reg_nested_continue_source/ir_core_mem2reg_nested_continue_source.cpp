#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/promotable_stack_slot_analysis.hpp"
#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/lower/lower_ir_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/printer/core_ir_raw_printer.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "backend/ir/sroa/core_ir_sroa_pass.hpp"
#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"
#include "backend/ir/shared/ir_result.hpp"
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

bool has_promotable_unit(const CoreIrPromotableStackSlotAnalysisResult &result,
                         std::string_view slot_name) {
    for (const CoreIrPromotionUnitInfo &unit_info : result.get_unit_infos()) {
        if (unit_info.unit.stack_slot != nullptr &&
            unit_info.unit.stack_slot->get_name() == slot_name) {
            return true;
        }
    }
    return false;
}

bool has_rejected_slot(const CoreIrPromotableStackSlotAnalysisResult &result,
                       std::string_view slot_name) {
    for (const CoreIrRejectedStackSlot &rejected_slot : result.get_rejected_slots()) {
        if (rejected_slot.stack_slot != nullptr &&
            rejected_slot.stack_slot->get_name() == slot_name) {
            return true;
        }
    }
    return false;
}

bool function_has_stack_slot(const CoreIrFunction &function, std::string_view slot_name) {
    for (const auto &stack_slot : function.get_stack_slots()) {
        if (stack_slot != nullptr && stack_slot->get_name() == slot_name) {
            return true;
        }
    }
    return false;
}

std::size_t count_loop_phi_blocks(const CoreIrFunction &function) {
    std::size_t count = 0;
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr) {
            continue;
        }
        const std::string &name = block->get_name();
        if (name.find("while.cond") == std::string::npos &&
            name.find(".latch") == std::string::npos) {
            continue;
        }
        for (const auto &instruction : block->get_instructions()) {
            if (instruction != nullptr &&
                dynamic_cast<CoreIrPhiInst *>(instruction.get()) != nullptr) {
                ++count;
            }
        }
    }
    return count;
}

void assert_function_lacks_stack_slots(const CoreIrFunction &function,
                                       const std::vector<std::string> &slot_names) {
    for (const std::string &slot_name : slot_names) {
        assert(!function_has_stack_slot(function, slot_name));
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
    CoreIrFunction *mm_function = find_function(*module, "mm");
    assert(mm_function != nullptr);
    CoreIrFunction *dead_shadow_function = find_function(*module, "dead_shadow");
    assert(dead_shadow_function != nullptr);

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    assert(analysis_manager != nullptr);
    const CoreIrPromotableStackSlotAnalysisResult &promotable_units =
        analysis_manager->get_or_compute<CoreIrPromotableStackSlotAnalysis>(
            *mm_function);

    const std::vector<std::string> expected_promoted_slots = {
        "n.addr", "A.addr", "B.addr", "C.addr", "i.addr", "j.addr", "k.addr"};
    for (const std::string &slot_name : expected_promoted_slots) {
        assert(has_promotable_unit(promotable_units, slot_name));
        assert(!has_rejected_slot(promotable_units, slot_name));
    }

    CoreIrMem2RegPass mem2reg_pass;
    assert(mem2reg_pass.Run(compiler_context).ok);

    CoreIrVerifier verifier;
    assert(verifier.verify_module(*module).ok);

    mm_function = find_function(*module, "mm");
    assert(mm_function != nullptr);
    dead_shadow_function = find_function(*module, "dead_shadow");
    assert(dead_shadow_function != nullptr);
    assert_function_lacks_stack_slots(*mm_function, expected_promoted_slots);
    const std::vector<std::string> dead_shadow_slots = {
        "n.addr", "A.addr", "B.addr", "C.addr", "X.addr", "y.addr"};
    assert_function_lacks_stack_slots(*dead_shadow_function, dead_shadow_slots);
    assert(count_loop_phi_blocks(*mm_function) >= 3);

    CoreIrRawPrinter printer;
    const std::string core_ir_text = printer.print_module(*module);
    for (const std::string &slot_name : expected_promoted_slots) {
        assert(core_ir_text.find("stackslot %" + slot_name) == std::string::npos);
    }
    for (const std::string &slot_name : dead_shadow_slots) {
        assert(core_ir_text.find("stackslot %" + slot_name) == std::string::npos);
    }
    assert(core_ir_text.find("phi i32") != std::string::npos);

    LowerIrPass lower_ir_pass;
    assert(lower_ir_pass.Run(compiler_context).ok);

    const IRResult *ir_result = compiler_context.get_ir_result();
    assert(ir_result != nullptr);
    assert(ir_result->get_kind() == IrKind::LLVM);

    const std::string llvm_mm =
        extract_llvm_function(ir_result->get_text(), "define void @mm(");
    assert(llvm_mm.find("alloca") == std::string::npos);
    assert(llvm_mm.find(" phi ") != std::string::npos);
    assert(llvm_mm.find("%i.addr") == std::string::npos);
    assert(llvm_mm.find("%j.addr") == std::string::npos);
    assert(llvm_mm.find("%k.addr") == std::string::npos);
    assert(llvm_mm.find("%n.addr") == std::string::npos);
    assert(llvm_mm.find("%A.addr") == std::string::npos);
    assert(llvm_mm.find("%B.addr") == std::string::npos);
    assert(llvm_mm.find("%C.addr") == std::string::npos);

    return 0;
}
