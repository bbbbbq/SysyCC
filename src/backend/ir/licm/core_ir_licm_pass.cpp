#include "backend/ir/licm/core_ir_licm_pass.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/alias_analysis.hpp"
#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/function_effect_summary_analysis.hpp"
#include "backend/ir/analysis/scalar_evolution_lite_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/analysis/memory_ssa_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

bool loop_contains_block(const CoreIrLoopInfo &loop,
                         const CoreIrBasicBlock *block) {
    return block != nullptr &&
           loop.get_blocks().find(const_cast<CoreIrBasicBlock *>(block)) !=
               loop.get_blocks().end();
}

bool value_is_loop_invariant(CoreIrValue *value, const CoreIrLoopInfo &loop) {
    if (value == nullptr) {
        return false;
    }
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return true;
    }
    return !loop_contains_block(loop, instruction->get_parent());
}

bool instruction_operands_are_loop_invariant(const CoreIrInstruction &instruction,
                                             const CoreIrLoopInfo &loop) {
    for (CoreIrValue *operand : instruction.get_operands()) {
        if (!value_is_loop_invariant(operand, loop)) {
            return false;
        }
    }
    return true;
}

bool block_is_must_execute_in_loop(
    const CoreIrBasicBlock *block, const CoreIrLoopInfo &loop,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree) {
    if (block == nullptr) {
        return false;
    }
    for (CoreIrBasicBlock *exit_block : loop.get_exit_blocks()) {
        if (exit_block != nullptr &&
            !dominator_tree.dominates(const_cast<CoreIrBasicBlock *>(block),
                                      exit_block)) {
            return false;
        }
    }
    for (CoreIrBasicBlock *latch : loop.get_latches()) {
        if (latch != nullptr &&
            !dominator_tree.dominates(const_cast<CoreIrBasicBlock *>(block),
                                      latch)) {
            return false;
        }
    }
    return true;
}

CoreIrEffectInfo get_instruction_effect(
    const CoreIrInstruction &instruction, CoreIrModule *module,
    CoreIrAnalysisManager &analysis_manager) {
    CoreIrEffectInfo effect = get_core_ir_instruction_effect(instruction);
    const auto *call = dynamic_cast<const CoreIrCallInst *>(&instruction);
    if (call == nullptr || !call->get_is_direct_call() || module == nullptr) {
        return effect;
    }

    CoreIrFunction *callee = module->find_function(call->get_callee_name());
    if (callee == nullptr || callee->get_basic_blocks().empty()) {
        return effect;
    }
    return analysis_manager
        .get_or_compute<CoreIrFunctionEffectSummaryAnalysis>(*callee)
        .get_effect_info();
}

bool instruction_is_pure_licm_candidate(
    const CoreIrInstruction &instruction, CoreIrModule *module,
    CoreIrAnalysisManager &analysis_manager) {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::GetElementPtr:
        return get_instruction_effect(instruction, module, analysis_manager)
            .is_pure_value;
    case CoreIrOpcode::Phi:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Call:
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::Return:
        return false;
    }

    return false;
}

bool block_has_may_alias_memory_write(
    const CoreIrBasicBlock &block, const CoreIrInstruction &query_instruction,
    const CoreIrMemoryLocation &query_location, CoreIrModule *module,
    CoreIrAnalysisManager &analysis_manager,
    const CoreIrAliasAnalysisResult &alias_analysis,
    const CoreIrMemorySSAAnalysisResult &memory_ssa) {
    for (const auto &instruction_ptr : block.get_instructions()) {
        const CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction == &query_instruction) {
            continue;
        }

        const CoreIrEffectInfo effect =
            get_instruction_effect(*instruction, module, analysis_manager);
        if (!memory_behavior_writes(effect.memory_behavior)) {
            continue;
        }

        if (memory_ssa.get_access_for_instruction(instruction) == nullptr) {
            return true;
        }

        const CoreIrMemoryLocation *write_location =
            alias_analysis.get_location_for_instruction(instruction);
        if (write_location == nullptr ||
            write_location->root_kind == CoreIrMemoryLocationRootKind::Unknown) {
            return true;
        }

        if (alias_core_ir_memory_locations(query_location, *write_location) !=
            CoreIrAliasKind::NoAlias) {
            return true;
        }
    }

    return false;
}

CoreIrMemoryAccess *get_effective_clobbering_access(
    const CoreIrInstruction &instruction, CoreIrModule *module,
    CoreIrAnalysisManager &analysis_manager,
    const CoreIrMemorySSAAnalysisResult &memory_ssa) {
    CoreIrMemoryAccess *cursor = memory_ssa.get_clobbering_access(&instruction);
    while (auto *def = dynamic_cast<CoreIrMemoryDefAccess *>(cursor)) {
        const CoreIrInstruction *def_instruction = def->get_instruction();
        if (def_instruction == nullptr) {
            return cursor;
        }
        const CoreIrEffectInfo effect =
            get_instruction_effect(*def_instruction, module, analysis_manager);
        if (memory_behavior_writes(effect.memory_behavior)) {
            return cursor;
        }
        cursor = def->get_defining_access();
    }
    return cursor;
}

bool load_has_invariant_address(const CoreIrLoadInst &load, const CoreIrLoopInfo &loop) {
    if (load.get_stack_slot() != nullptr) {
        return true;
    }
    return value_is_loop_invariant(load.get_address(), loop);
}

bool store_has_invariant_address(const CoreIrStoreInst &store, const CoreIrLoopInfo &loop) {
    if (store.get_stack_slot() != nullptr) {
        return true;
    }
    return value_is_loop_invariant(store.get_address(), loop);
}

bool instruction_precedes_in_block(const CoreIrInstruction &lhs,
                                   const CoreIrInstruction &rhs) {
    if (lhs.get_parent() == nullptr || lhs.get_parent() != rhs.get_parent()) {
        return false;
    }
    for (const auto &instruction_ptr : lhs.get_parent()->get_instructions()) {
        if (instruction_ptr.get() == &lhs) {
            return true;
        }
        if (instruction_ptr.get() == &rhs) {
            return false;
        }
    }
    return false;
}

bool instruction_executes_after_store(
    const CoreIrStoreInst &store, const CoreIrInstruction &instruction,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree) {
    if (instruction.get_parent() == store.get_parent()) {
        return instruction_precedes_in_block(store, instruction);
    }
    return dominator_tree.dominates(store.get_parent(), instruction.get_parent());
}

bool load_is_hoistable(const CoreIrLoadInst &load, const CoreIrLoopInfo &loop,
                       CoreIrModule *module,
                       CoreIrAnalysisManager &analysis_manager,
                       const CoreIrAliasAnalysisResult &alias_analysis,
                       const CoreIrMemorySSAAnalysisResult &memory_ssa,
                       CoreIrFunction &function) {
    if (!load_has_invariant_address(load, loop)) {
        return false;
    }

    const CoreIrMemoryLocation *location =
        alias_analysis.get_location_for_instruction(&load);
    if (location == nullptr ||
        location->root_kind == CoreIrMemoryLocationRootKind::Unknown) {
        return false;
    }

    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr == nullptr || !loop_contains_block(loop, block_ptr.get())) {
            continue;
        }
        if (block_has_may_alias_memory_write(*block_ptr, load, *location, module,
                                             analysis_manager, alias_analysis,
                                             memory_ssa)) {
            return false;
        }
    }

    CoreIrMemoryAccess *clobber =
        get_effective_clobbering_access(load, module, analysis_manager, memory_ssa);
    if (clobber == nullptr ||
        dynamic_cast<CoreIrMemoryLiveOnEntryAccess *>(clobber) != nullptr) {
        return true;
    }
    if (auto *def = dynamic_cast<CoreIrMemoryDefAccess *>(clobber); def != nullptr) {
        const CoreIrInstruction *def_instruction = def->get_instruction();
        return def_instruction == nullptr ||
               !loop_contains_block(loop, def_instruction->get_parent());
    }
    if (auto *phi = dynamic_cast<CoreIrMemoryPhiAccess *>(clobber); phi != nullptr) {
        return !loop_contains_block(loop, phi->get_block());
    }
    return false;
}

bool load_is_speculatively_safe_to_hoist(
    const CoreIrLoadInst &load, const CoreIrLoopInfo &loop,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree,
    CoreIrFunction &function) {
    CoreIrStackSlot *stack_slot = load.get_stack_slot();
    CoreIrBasicBlock *header = loop.get_header();
    if (stack_slot == nullptr || header == nullptr) {
        return false;
    }

    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr == nullptr || loop_contains_block(loop, block_ptr.get())) {
            continue;
        }
        if (!dominator_tree.dominates(block_ptr.get(), header)) {
            continue;
        }
        for (const auto &instruction_ptr : block_ptr->get_instructions()) {
            auto *store = dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
            if (store != nullptr && store->get_stack_slot() == stack_slot) {
                return true;
            }
        }
    }
    return false;
}

bool store_is_hoistable(const CoreIrStoreInst &store, const CoreIrLoopInfo &loop,
                        const CoreIrDominatorTreeAnalysisResult &dominator_tree,
                        CoreIrModule *module,
                        CoreIrAnalysisManager &analysis_manager,
                        const CoreIrAliasAnalysisResult &alias_analysis,
                        CoreIrFunction &function) {
    if (!store_has_invariant_address(store, loop) ||
        !value_is_loop_invariant(store.get_value(), loop)) {
        return false;
    }

    const CoreIrMemoryLocation *location =
        alias_analysis.get_location_for_instruction(&store);
    if (location == nullptr ||
        location->root_kind == CoreIrMemoryLocationRootKind::Unknown) {
        return false;
    }

    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr == nullptr || !loop_contains_block(loop, block_ptr.get())) {
            continue;
        }
        for (const auto &instruction_ptr : block_ptr->get_instructions()) {
            const CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr || instruction == &store) {
                continue;
            }

            const CoreIrEffectInfo effect =
                get_instruction_effect(*instruction, module, analysis_manager);
            if (!memory_behavior_reads(effect.memory_behavior) &&
                !memory_behavior_writes(effect.memory_behavior)) {
                continue;
            }

            const CoreIrMemoryLocation *other_location =
                alias_analysis.get_location_for_instruction(instruction);
            if (other_location == nullptr ||
                other_location->root_kind == CoreIrMemoryLocationRootKind::Unknown) {
                if (memory_behavior_writes(effect.memory_behavior)) {
                    return false;
                }
                if (memory_behavior_reads(effect.memory_behavior) &&
                    instruction_executes_after_store(store, *instruction,
                                                     dominator_tree)) {
                    continue;
                }
                return false;
            }
            if (alias_core_ir_memory_locations(*location, *other_location) ==
                CoreIrAliasKind::NoAlias) {
                continue;
            }

            if (memory_behavior_writes(effect.memory_behavior)) {
                return false;
            }
            if (memory_behavior_reads(effect.memory_behavior) &&
                !instruction_executes_after_store(store, *instruction,
                                                 dominator_tree)) {
                return false;
            }
        }
    }

    return true;
}

std::size_t count_loop_uses(const CoreIrInstruction &instruction,
                           const CoreIrLoopInfo &loop) {
    std::size_t use_count = 0;
    for (const CoreIrUse &use : instruction.get_uses()) {
        const CoreIrInstruction *user = use.get_user();
        if (user != nullptr && loop_contains_block(loop, user->get_parent())) {
            ++use_count;
        }
    }
    return use_count;
}

const CoreIrInstruction *get_single_loop_user(const CoreIrInstruction &instruction,
                                              const CoreIrLoopInfo &loop) {
    const CoreIrInstruction *loop_user = nullptr;
    for (const CoreIrUse &use : instruction.get_uses()) {
        const CoreIrInstruction *user = use.get_user();
        if (user == nullptr || !loop_contains_block(loop, user->get_parent())) {
            continue;
        }
        if (loop_user != nullptr && loop_user != user) {
            return nullptr;
        }
        loop_user = user;
    }
    return loop_user;
}

bool is_profitable_to_hoist(const CoreIrInstruction &instruction,
                            const CoreIrLoopInfo &loop,
                            const CoreIrScalarEvolutionLiteAnalysisResult &scev) {
    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        return true;
    }
    if (dynamic_cast<const CoreIrStoreInst *>(&instruction) != nullptr) {
        return true;
    }

    switch (instruction.get_opcode()) {
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
        return false;
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::GetElementPtr:
        break;
    default:
        return false;
    }

    const std::size_t loop_uses = count_loop_uses(instruction, loop);
    if (loop_uses >= 2) {
        return true;
    }
    const std::optional<std::uint64_t> trip_count =
        scev.get_constant_trip_count(loop);
    if (loop_uses >= 1 && trip_count.has_value() && *trip_count >= 2) {
        return true;
    }
    if (loop_uses != 1) {
        return false;
    }

    const CoreIrInstruction *single_user = get_single_loop_user(instruction, loop);
    return single_user != nullptr &&
           !single_user->get_is_terminator() &&
           single_user->get_opcode() != CoreIrOpcode::Phi;
}

bool is_safe_to_hoist(const CoreIrInstruction &instruction,
                      const CoreIrLoopInfo &loop,
                      const CoreIrDominatorTreeAnalysisResult &dominator_tree,
                      CoreIrModule *module,
                      CoreIrAnalysisManager &analysis_manager,
                      const CoreIrAliasAnalysisResult &alias_analysis,
                      const CoreIrMemorySSAAnalysisResult &memory_ssa,
                      CoreIrFunction &function) {
    if (instruction.get_is_terminator() ||
        instruction.get_opcode() == CoreIrOpcode::Phi) {
        return false;
    }

    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        if (!load_is_hoistable(*load, loop, module, analysis_manager,
                               alias_analysis, memory_ssa, function)) {
            return false;
        }
        return block_is_must_execute_in_loop(instruction.get_parent(), loop,
                                             dominator_tree) ||
               load_is_speculatively_safe_to_hoist(*load, loop, dominator_tree,
                                                   function);
    }

    if (!block_is_must_execute_in_loop(instruction.get_parent(), loop,
                                       dominator_tree)) {
        return false;
    }

    if (const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
        store != nullptr) {
        return store_is_hoistable(*store, loop, dominator_tree, module,
                                  analysis_manager, alias_analysis, function);
    }

    if (!instruction_is_pure_licm_candidate(instruction, module, analysis_manager)) {
        return false;
    }
    return instruction_operands_are_loop_invariant(instruction, loop);
}

bool instruction_is_hoistable(const CoreIrInstruction &instruction,
                              const CoreIrLoopInfo &loop,
                              const CoreIrDominatorTreeAnalysisResult &dominator_tree,
                              const CoreIrScalarEvolutionLiteAnalysisResult &scev,
                              CoreIrModule *module,
                              CoreIrAnalysisManager &analysis_manager,
                              const CoreIrAliasAnalysisResult &alias_analysis,
                              const CoreIrMemorySSAAnalysisResult &memory_ssa,
                              CoreIrFunction &function) {
    return is_safe_to_hoist(instruction, loop, dominator_tree, module,
                            analysis_manager, alias_analysis, memory_ssa,
                            function) &&
           is_profitable_to_hoist(instruction, loop, scev);
}

bool move_instruction_to_preheader(CoreIrBasicBlock &source, std::size_t index,
                                   CoreIrBasicBlock &preheader) {
    auto &source_instructions = source.get_instructions();
    if (index >= source_instructions.size()) {
        return false;
    }

    std::unique_ptr<CoreIrInstruction> instruction =
        std::move(source_instructions[index]);
    source_instructions.erase(source_instructions.begin() +
                              static_cast<std::ptrdiff_t>(index));
    if (instruction == nullptr) {
        return false;
    }

    instruction->set_parent(&preheader);
    auto &preheader_instructions = preheader.get_instructions();
    auto insert_it = preheader_instructions.end();
    if (!preheader_instructions.empty() &&
        preheader_instructions.back() != nullptr &&
        preheader_instructions.back()->get_is_terminator()) {
        insert_it = preheader_instructions.end() - 1;
    }
    preheader_instructions.insert(insert_it, std::move(instruction));
    return true;
}

bool run_licm_on_loop(CoreIrFunction &function, const CoreIrLoopInfo &loop,
                      const CoreIrDominatorTreeAnalysisResult &dominator_tree,
                      const CoreIrScalarEvolutionLiteAnalysisResult &scev,
                      CoreIrModule *module,
                      CoreIrAnalysisManager &analysis_manager,
                      const CoreIrAliasAnalysisResult &alias_analysis,
                      const CoreIrMemorySSAAnalysisResult &memory_ssa) {
    CoreIrBasicBlock *preheader = loop.get_preheader();
    if (preheader == nullptr || loop_contains_block(loop, preheader)) {
        return false;
    }

    bool changed = false;
    while (true) {
        bool iteration_changed = false;
        for (const auto &block_ptr : function.get_basic_blocks()) {
            if (block_ptr == nullptr || !loop_contains_block(loop, block_ptr.get())) {
                continue;
            }

            auto &instructions = block_ptr->get_instructions();
            std::size_t index = 0;
            while (index < instructions.size()) {
                CoreIrInstruction *instruction = instructions[index].get();
                if (instruction == nullptr) {
                    instructions.erase(instructions.begin() +
                                       static_cast<std::ptrdiff_t>(index));
                    iteration_changed = true;
                    changed = true;
                    continue;
                }

                if (!instruction_is_hoistable(*instruction, loop, dominator_tree,
                                              scev, module,
                                              analysis_manager, alias_analysis,
                                              memory_ssa, function)) {
                    ++index;
                    continue;
                }

                if (move_instruction_to_preheader(*block_ptr, index, *preheader)) {
                    iteration_changed = true;
                    changed = true;
                    continue;
                }
                ++index;
            }
        }

        if (!iteration_changed) {
            break;
        }
    }

    return changed;
}

} // namespace

PassKind CoreIrLicmPass::Kind() const { return PassKind::CoreIrLicm; }

const char *CoreIrLicmPass::Name() const { return "CoreIrLicmPass"; }

PassResult CoreIrLicmPass::Run(CompilerContext &context) {
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
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
        if (loop_info.get_loops().empty()) {
            continue;
        }
        const CoreIrScalarEvolutionLiteAnalysisResult &scev =
            analysis_manager->get_or_compute<CoreIrScalarEvolutionLiteAnalysis>(
                *function);
        const CoreIrDominatorTreeAnalysisResult &dominator_tree =
            analysis_manager->get_or_compute<CoreIrDominatorTreeAnalysis>(
                *function);

        const CoreIrAliasAnalysisResult &alias_analysis =
            analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
        const CoreIrMemorySSAAnalysisResult &memory_ssa =
            analysis_manager->get_or_compute<CoreIrMemorySSAAnalysis>(*function);

        std::vector<const CoreIrLoopInfo *> ordered_loops;
        ordered_loops.reserve(loop_info.get_loops().size());
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr != nullptr) {
                ordered_loops.push_back(loop_ptr.get());
            }
        }
        std::stable_sort(ordered_loops.begin(), ordered_loops.end(),
                         [](const CoreIrLoopInfo *lhs, const CoreIrLoopInfo *rhs) {
                             if (lhs == nullptr || rhs == nullptr) {
                                 return rhs != nullptr;
                             }
                             return lhs->get_depth() > rhs->get_depth();
                         });

        bool function_changed = false;
        for (const CoreIrLoopInfo *loop : ordered_loops) {
            if (loop == nullptr) {
                continue;
            }
            function_changed = run_licm_on_loop(
                                   *function, *loop, dominator_tree, scev, module,
                                   *analysis_manager, alias_analysis, memory_ssa) ||
                               function_changed;
        }
        if (function_changed) {
            effects.changed_functions.insert(function.get());
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    effects.preserved_analyses.preserve_cfg_family();
    effects.preserved_analyses.preserve(CoreIrAnalysisKind::AliasAnalysis);
    effects.preserved_analyses.preserve(CoreIrAnalysisKind::FunctionEffectSummary);
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
