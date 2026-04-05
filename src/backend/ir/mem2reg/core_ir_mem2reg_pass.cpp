#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_stack_slot.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

bool erase_instruction(CoreIrBasicBlock &block, CoreIrInstruction *instruction) {
    auto &instructions = block.get_instructions();
    auto it = std::find_if(
        instructions.begin(), instructions.end(),
        [instruction](const std::unique_ptr<CoreIrInstruction> &candidate) {
            return candidate.get() == instruction;
        });
    if (it == instructions.end()) {
        return false;
    }
    (*it)->detach_operands();
    instructions.erase(it);
    return true;
}

void erase_dead_address_chain(CoreIrBasicBlock &block, CoreIrValue *value) {
    CoreIrInstruction *instruction = dynamic_cast<CoreIrInstruction *>(value);
    while (instruction != nullptr &&
           get_core_ir_instruction_effect(*instruction).is_pure_value &&
           instruction->get_uses().empty()) {
        CoreIrValue *next = nullptr;
        if (auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(instruction);
            gep != nullptr) {
            next = gep->get_base();
        }
        if (!erase_instruction(block, instruction)) {
            break;
        }
        instruction = dynamic_cast<CoreIrInstruction *>(next);
    }
}

struct UnitRenameContext {
    const CoreIrPromotionUnitInfo *unit_info = nullptr;
    std::unordered_map<CoreIrBasicBlock *, CoreIrPhiInst *> inserted_phis;
    std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>>
        dominator_children;
    std::vector<CoreIrValue *> value_stack;
    bool changed = false;
};

void build_dominator_children(
    CoreIrFunction &function, CoreIrAnalysisManager &analysis_manager,
    std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>> &children) {
    const CoreIrCfgAnalysisResult &cfg_analysis =
        analysis_manager.get_or_compute<CoreIrCfgAnalysis>(function);
    const CoreIrDominatorTreeAnalysisResult &dominator_tree =
        analysis_manager.get_or_compute<CoreIrDominatorTreeAnalysis>(function);
    for (const auto &block : function.get_basic_blocks()) {
        if (block != nullptr && cfg_analysis.is_reachable(block.get())) {
            children.emplace(block.get(), std::vector<CoreIrBasicBlock *>{});
        }
    }
    for (const auto &block : function.get_basic_blocks()) {
        if (block == nullptr || !cfg_analysis.is_reachable(block.get())) {
            continue;
        }
        CoreIrBasicBlock *idom = dominator_tree.get_immediate_dominator(block.get());
        if (idom != nullptr) {
            children[idom].push_back(block.get());
        }
    }
}

bool is_instruction_in_unit(const CoreIrPromotionUnitInfo &unit_info,
                            const CoreIrInstruction *instruction) {
    for (CoreIrLoadInst *load : unit_info.loads) {
        if (load == instruction) {
            return true;
        }
    }
    for (CoreIrStoreInst *store : unit_info.stores) {
        if (store == instruction) {
            return true;
        }
    }
    return false;
}

CoreIrPhiInst *insert_phi_for_unit(CoreIrBasicBlock &block,
                                   const CoreIrPromotionUnitInfo &unit_info,
                                   std::size_t phi_index) {
    std::string name = unit_info.unit.stack_slot == nullptr
                           ? "mem2reg.phi." + std::to_string(phi_index)
                           : unit_info.unit.stack_slot->get_name() + ".ssa." +
                                 std::to_string(phi_index);
    auto phi = std::make_unique<CoreIrPhiInst>(unit_info.unit.value_type, name);
    return static_cast<CoreIrPhiInst *>(
        block.insert_instruction_before_first_non_phi(std::move(phi)));
}

void insert_mem2reg_phis(CoreIrFunction &function,
                         CoreIrAnalysisManager &analysis_manager,
                         UnitRenameContext &rename_context) {
    const CoreIrPromotionUnitInfo &unit_info = *rename_context.unit_info;
    const CoreIrDominanceFrontierAnalysisResult &dominance_frontier =
        analysis_manager.get_or_compute<CoreIrDominanceFrontierAnalysis>(function);

    std::vector<CoreIrBasicBlock *> worklist(unit_info.def_blocks.begin(),
                                             unit_info.def_blocks.end());
    std::unordered_set<CoreIrBasicBlock *> queued(unit_info.def_blocks.begin(),
                                                  unit_info.def_blocks.end());
    std::size_t phi_index = 0;
    while (!worklist.empty()) {
        CoreIrBasicBlock *block = worklist.back();
        worklist.pop_back();
        if (block == nullptr) {
            continue;
        }

        for (CoreIrBasicBlock *frontier_block :
             dominance_frontier.get_frontier(block)) {
            if (frontier_block == nullptr ||
                rename_context.inserted_phis.find(frontier_block) !=
                    rename_context.inserted_phis.end()) {
                continue;
            }
            CoreIrPhiInst *phi =
                insert_phi_for_unit(*frontier_block, unit_info, phi_index++);
            rename_context.inserted_phis.emplace(frontier_block, phi);
            if (queued.insert(frontier_block).second) {
                worklist.push_back(frontier_block);
            }
        }
    }
}

void add_phi_incoming_for_successors(CoreIrBasicBlock &block,
                                     UnitRenameContext &rename_context) {
    if (block.get_instructions().empty() || rename_context.value_stack.empty()) {
        return;
    }

    auto add_incoming = [&rename_context, &block](CoreIrBasicBlock *successor) {
        if (successor == nullptr) {
            return;
        }
        auto phi_it = rename_context.inserted_phis.find(successor);
        if (phi_it == rename_context.inserted_phis.end() || phi_it->second == nullptr) {
            return;
        }
        phi_it->second->add_incoming(&block, rename_context.value_stack.back());
        rename_context.changed = true;
    };

    CoreIrInstruction *terminator = block.get_instructions().back().get();
    if (auto *jump = dynamic_cast<CoreIrJumpInst *>(terminator); jump != nullptr) {
        add_incoming(jump->get_target_block());
    } else if (auto *cond_jump = dynamic_cast<CoreIrCondJumpInst *>(terminator);
               cond_jump != nullptr) {
        add_incoming(cond_jump->get_true_block());
        add_incoming(cond_jump->get_false_block());
    }
}

bool rename_promoted_unit(CoreIrBasicBlock &block, UnitRenameContext &rename_context) {
    const CoreIrPromotionUnitInfo &unit_info = *rename_context.unit_info;
    std::size_t saved_depth = rename_context.value_stack.size();

    auto phi_it = rename_context.inserted_phis.find(&block);
    if (phi_it != rename_context.inserted_phis.end() && phi_it->second != nullptr) {
        rename_context.value_stack.push_back(phi_it->second);
    }

    auto &instructions = block.get_instructions();
    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() + static_cast<std::ptrdiff_t>(index));
            rename_context.changed = true;
            continue;
        }
        if (dynamic_cast<CoreIrPhiInst *>(instruction) != nullptr) {
            ++index;
            continue;
        }
        if (!is_instruction_in_unit(unit_info, instruction)) {
            ++index;
            continue;
        }

        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction); load != nullptr) {
            if (rename_context.value_stack.empty()) {
                ++index;
                continue;
            }
            CoreIrValue *original_address = load->get_address();
            load->replace_all_uses_with(rename_context.value_stack.back());
            erase_instruction(block, load);
            erase_dead_address_chain(block, original_address);
            rename_context.changed = true;
            continue;
        }

        if (auto *store = dynamic_cast<CoreIrStoreInst *>(instruction); store != nullptr) {
            CoreIrValue *stored_value = store->get_value();
            CoreIrValue *original_address = store->get_address();
            rename_context.value_stack.push_back(stored_value);
            erase_instruction(block, store);
            erase_dead_address_chain(block, original_address);
            rename_context.changed = true;
            continue;
        }

        ++index;
    }

    add_phi_incoming_for_successors(block, rename_context);
    for (CoreIrBasicBlock *child : rename_context.dominator_children[&block]) {
        rename_promoted_unit(*child, rename_context);
    }

    while (rename_context.value_stack.size() > saved_depth) {
        rename_context.value_stack.pop_back();
    }
    return rename_context.changed;
}

void remove_fully_promoted_stack_slots(CoreIrFunction &function) {
    auto &stack_slots = function.get_stack_slots();
    stack_slots.erase(
        std::remove_if(stack_slots.begin(), stack_slots.end(),
                       [&function](const std::unique_ptr<CoreIrStackSlot> &slot) {
                           if (slot == nullptr) {
                               return true;
                           }
                           for (const auto &block : function.get_basic_blocks()) {
                               if (block == nullptr) {
                                   continue;
                               }
                               for (const auto &instruction : block->get_instructions()) {
                                   if (instruction == nullptr) {
                                       continue;
                                   }
                                   if (auto *address =
                                           dynamic_cast<CoreIrAddressOfStackSlotInst *>(
                                               instruction.get());
                                       address != nullptr &&
                                       address->get_stack_slot() == slot.get()) {
                                       return false;
                                   }
                                   if (auto *load = dynamic_cast<CoreIrLoadInst *>(
                                           instruction.get());
                                       load != nullptr &&
                                       load->get_stack_slot() == slot.get()) {
                                       return false;
                                   }
                                   if (auto *store = dynamic_cast<CoreIrStoreInst *>(
                                           instruction.get());
                                       store != nullptr &&
                                       store->get_stack_slot() == slot.get()) {
                                       return false;
                                   }
                               }
                           }
                           return true;
                       }),
        stack_slots.end());
}

} // namespace

PassKind CoreIrMem2RegPass::Kind() const { return PassKind::CoreIrMem2Reg; }

const char *CoreIrMem2RegPass::Name() const { return "CoreIrMem2RegPass"; }

PassResult CoreIrMem2RegPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir analysis manager");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrPromotableStackSlotAnalysisResult &promotable_units =
            analysis_manager->get_or_compute<CoreIrPromotableStackSlotAnalysis>(
                *function);
        if (promotable_units.get_unit_infos().empty()) {
            continue;
        }

        bool function_changed = false;
        for (const CoreIrPromotionUnitInfo &unit_info :
             promotable_units.get_unit_infos()) {
            UnitRenameContext rename_context;
            rename_context.unit_info = &unit_info;
            build_dominator_children(*function, *analysis_manager,
                                     rename_context.dominator_children);
            insert_mem2reg_phis(*function, *analysis_manager, rename_context);
            const CoreIrCfgAnalysisResult &cfg_analysis =
                analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
            CoreIrBasicBlock *entry_block = const_cast<CoreIrBasicBlock *>(
                cfg_analysis.get_entry_block());
            if (entry_block != nullptr) {
                rename_promoted_unit(*entry_block, rename_context);
            }
            function_changed = rename_context.changed || function_changed;
        }

        if (function_changed) {
            remove_fully_promoted_stack_slots(*function);
            effects.changed_functions.insert(function.get());
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    effects.preserved_analyses.preserve_cfg_family();
    effects.preserved_analyses.preserve_loop_family();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
