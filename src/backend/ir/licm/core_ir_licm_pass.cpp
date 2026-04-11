#include "backend/ir/licm/core_ir_licm_pass.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/alias_analysis.hpp"
#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/function_effect_summary_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/analysis/memory_ssa_analysis.hpp"
#include "backend/ir/analysis/scalar_evolution_lite_analysis.hpp"
#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

PassResult fail_missing_core_ir(CompilerContext &context,
                                const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
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

bool instruction_is_pure_licm_candidate(
    const CoreIrInstruction &instruction, CoreIrModule *module,
    CoreIrAnalysisManager &analysis_manager);

bool value_is_recursively_loop_invariant(
    CoreIrValue *value, const CoreIrLoopInfo &loop, CoreIrModule *module,
    CoreIrAnalysisManager &analysis_manager,
    std::unordered_set<const CoreIrInstruction *> &visiting) {
    if (value == nullptr) {
        return false;
    }
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        return true;
    }
    if (!loop_contains_block(loop, instruction->get_parent())) {
        return true;
    }
    if (!instruction_is_pure_licm_candidate(*instruction, module,
                                            analysis_manager) ||
        !visiting.insert(instruction).second) {
        return false;
    }
    for (CoreIrValue *operand : instruction->get_operands()) {
        if (!value_is_recursively_loop_invariant(operand, loop, module,
                                                 analysis_manager, visiting)) {
            visiting.erase(instruction);
            return false;
        }
    }
    visiting.erase(instruction);
    return true;
}

bool instruction_operands_are_loop_invariant(
    const CoreIrInstruction &instruction, const CoreIrLoopInfo &loop,
    CoreIrModule *module, CoreIrAnalysisManager &analysis_manager) {
    std::unordered_set<const CoreIrInstruction *> visiting;
    for (CoreIrValue *operand : instruction.get_operands()) {
        if (!value_is_recursively_loop_invariant(operand, loop, module,
                                                 analysis_manager, visiting)) {
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

CoreIrEffectInfo
get_instruction_effect(const CoreIrInstruction &instruction,
                       CoreIrModule *module,
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
    case CoreIrOpcode::Select:
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

bool instruction_is_speculatively_safe_address_materialization(
    const CoreIrInstruction &instruction) {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::GetElementPtr:
    case CoreIrOpcode::Cast:
        return true;
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Select:
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

CoreIrBasicBlock *
get_unique_outside_predecessor(const CoreIrLoopInfo &loop,
                               const CoreIrCfgAnalysisResult &cfg) {
    CoreIrBasicBlock *header = loop.get_header();
    if (header == nullptr) {
        return nullptr;
    }

    CoreIrBasicBlock *outside_predecessor = nullptr;
    for (CoreIrBasicBlock *predecessor : cfg.get_predecessors(header)) {
        if (predecessor == nullptr || loop_contains_block(loop, predecessor) ||
            !cfg.is_reachable(predecessor)) {
            continue;
        }
        if (outside_predecessor != nullptr) {
            return nullptr;
        }
        outside_predecessor = predecessor;
    }
    return outside_predecessor;
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
            write_location->kind == CoreIrMemoryLocationKind::Unknown) {
            return true;
        }

        if (alias_core_ir_memory_locations(query_location, *write_location) !=
            CoreIrAliasKind::NoAlias) {
            return true;
        }
    }

    return false;
}

bool address_is_recursively_loop_invariant(
    CoreIrValue *address, const CoreIrLoopInfo &loop, CoreIrModule *module,
    CoreIrAnalysisManager &analysis_manager) {
    std::unordered_set<const CoreIrInstruction *> visiting;
    return value_is_recursively_loop_invariant(address, loop, module,
                                               analysis_manager, visiting);
}

bool load_has_invariant_address(const CoreIrLoadInst &load,
                                const CoreIrLoopInfo &loop,
                                CoreIrModule *module,
                                CoreIrAnalysisManager &analysis_manager) {
    if (load.get_stack_slot() != nullptr) {
        return true;
    }
    return address_is_recursively_loop_invariant(load.get_address(), loop,
                                                 module, analysis_manager);
}

bool store_has_invariant_address(const CoreIrStoreInst &store,
                                 const CoreIrLoopInfo &loop,
                                 CoreIrModule *module,
                                 CoreIrAnalysisManager &analysis_manager) {
    if (store.get_stack_slot() != nullptr) {
        return true;
    }
    return address_is_recursively_loop_invariant(store.get_address(), loop,
                                                 module, analysis_manager);
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
    return dominator_tree.dominates(store.get_parent(),
                                    instruction.get_parent());
}

bool load_is_hoistable(const CoreIrLoadInst &load, const CoreIrLoopInfo &loop,
                       CoreIrModule *module,
                       CoreIrAnalysisManager &analysis_manager,
                       const CoreIrAliasAnalysisResult &alias_analysis,
                       const CoreIrMemorySSAAnalysisResult &memory_ssa,
                       CoreIrFunction &function) {
    if (!load_has_invariant_address(load, loop, module, analysis_manager)) {
        return false;
    }

    const CoreIrMemoryLocation *location =
        alias_analysis.get_location_for_instruction(&load);
    if (location == nullptr ||
        location->kind == CoreIrMemoryLocationKind::Unknown) {
        return false;
    }
    if (location->kind == CoreIrMemoryLocationKind::ArgumentDerived &&
        !location->exact_access_path) {
        return false;
    }

    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr == nullptr ||
            !loop_contains_block(loop, block_ptr.get())) {
            continue;
        }
        if (block_has_may_alias_memory_write(*block_ptr, load, *location,
                                             module, analysis_manager,
                                             alias_analysis, memory_ssa)) {
            return false;
        }
    }

    // MemorySSA clobbers are location-agnostic at this point. Once the explicit
    // alias-aware scan proves the loop contains no conflicting writes, the
    // remaining in-loop phis/defs should not block hoisting.
    return true;
}

bool load_is_speculatively_safe_to_hoist(
    const CoreIrLoadInst &load, const CoreIrLoopInfo &loop,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree,
    const CoreIrAliasAnalysisResult &alias_analysis, CoreIrFunction &function) {
    CoreIrStackSlot *stack_slot = load.get_stack_slot();
    if (stack_slot == nullptr) {
        const CoreIrMemoryLocation *location =
            alias_analysis.get_location_for_instruction(&load);
        return location != nullptr &&
               (location->kind == CoreIrMemoryLocationKind::Global ||
                (location->kind == CoreIrMemoryLocationKind::ArgumentDerived &&
                 location->exact_access_path));
    }

    CoreIrBasicBlock *header = loop.get_header();
    if (header == nullptr) {
        return false;
    }

    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr == nullptr ||
            loop_contains_block(loop, block_ptr.get())) {
            continue;
        }
        if (!dominator_tree.dominates(block_ptr.get(), header)) {
            continue;
        }
        for (const auto &instruction_ptr : block_ptr->get_instructions()) {
            auto *store =
                dynamic_cast<CoreIrStoreInst *>(instruction_ptr.get());
            if (store != nullptr && store->get_stack_slot() == stack_slot) {
                return true;
            }
        }
    }
    return false;
}

bool store_is_hoistable(const CoreIrStoreInst &store,
                        const CoreIrLoopInfo &loop,
                        const CoreIrDominatorTreeAnalysisResult &dominator_tree,
                        CoreIrModule *module,
                        CoreIrAnalysisManager &analysis_manager,
                        const CoreIrAliasAnalysisResult &alias_analysis,
                        CoreIrFunction &function) {
    if (!store_has_invariant_address(store, loop, module, analysis_manager) ||
        !value_is_loop_invariant(store.get_value(), loop)) {
        return false;
    }

    const CoreIrMemoryLocation *location =
        alias_analysis.get_location_for_instruction(&store);
    if (location == nullptr ||
        location->kind == CoreIrMemoryLocationKind::Unknown) {
        return false;
    }

    for (const auto &block_ptr : function.get_basic_blocks()) {
        if (block_ptr == nullptr ||
            !loop_contains_block(loop, block_ptr.get())) {
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
                other_location->kind == CoreIrMemoryLocationKind::Unknown) {
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

const CoreIrInstruction *
get_single_loop_user(const CoreIrInstruction &instruction,
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

bool is_profitable_to_hoist(
    const CoreIrInstruction &instruction, const CoreIrLoopInfo &loop,
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
        return count_loop_uses(instruction, loop) >= 1;
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Select:
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

    const CoreIrInstruction *single_user =
        get_single_loop_user(instruction, loop);
    return single_user != nullptr && !single_user->get_is_terminator() &&
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
                                                   alias_analysis, function);
    }

    if (!block_is_must_execute_in_loop(instruction.get_parent(), loop,
                                       dominator_tree)) {
        if (!instruction_is_speculatively_safe_address_materialization(
                instruction)) {
            return false;
        }
    }

    if (const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
        store != nullptr) {
        return store_is_hoistable(*store, loop, dominator_tree, module,
                                  analysis_manager, alias_analysis, function);
    }

    if (!instruction_is_pure_licm_candidate(instruction, module,
                                            analysis_manager)) {
        return false;
    }
    return instruction_operands_are_loop_invariant(instruction, loop, module,
                                                   analysis_manager);
}

bool instruction_is_hoistable(
    const CoreIrInstruction &instruction, const CoreIrLoopInfo &loop,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree,
    const CoreIrScalarEvolutionLiteAnalysisResult &scev, CoreIrModule *module,
    CoreIrAnalysisManager &analysis_manager,
    const CoreIrAliasAnalysisResult &alias_analysis,
    const CoreIrMemorySSAAnalysisResult &memory_ssa, CoreIrFunction &function) {
    return is_safe_to_hoist(instruction, loop, dominator_tree, module,
                            analysis_manager, alias_analysis, memory_ssa,
                            function) &&
           is_profitable_to_hoist(instruction, loop, scev);
}

bool instruction_can_hoist_to_non_dedicated_predecessor(
    const CoreIrInstruction &instruction, const CoreIrLoopInfo &loop,
    const CoreIrDominatorTreeAnalysisResult &dominator_tree,
    CoreIrModule *module, CoreIrAnalysisManager &analysis_manager,
    const CoreIrAliasAnalysisResult &alias_analysis,
    const CoreIrMemorySSAAnalysisResult &memory_ssa, CoreIrFunction &function) {
    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        return load_is_hoistable(*load, loop, module, analysis_manager,
                                 alias_analysis, memory_ssa, function) &&
               load_is_speculatively_safe_to_hoist(*load, loop, dominator_tree,
                                                   alias_analysis, function);
    }
    return instruction_is_speculatively_safe_address_materialization(
               instruction) &&
           instruction_operands_are_loop_invariant(instruction, loop, module,
                                                   analysis_manager);
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
                      const CoreIrCfgAnalysisResult &cfg,
                      const CoreIrDominatorTreeAnalysisResult &dominator_tree,
                      const CoreIrScalarEvolutionLiteAnalysisResult &scev,
                      CoreIrModule *module,
                      CoreIrAnalysisManager &analysis_manager,
                      const CoreIrAliasAnalysisResult &alias_analysis,
                      const CoreIrMemorySSAAnalysisResult &memory_ssa) {
    CoreIrBasicBlock *hoist_block = loop.get_preheader();
    bool dedicated_preheader =
        hoist_block != nullptr && !loop_contains_block(loop, hoist_block);
    if (!dedicated_preheader) {
        hoist_block = get_unique_outside_predecessor(loop, cfg);
    }
    if (hoist_block == nullptr || loop_contains_block(loop, hoist_block)) {
        return false;
    }

    bool changed = false;
    while (true) {
        bool iteration_changed = false;
        for (const auto &block_ptr : function.get_basic_blocks()) {
            if (block_ptr == nullptr ||
                !loop_contains_block(loop, block_ptr.get())) {
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

                if (!instruction_is_hoistable(*instruction, loop,
                                              dominator_tree, scev, module,
                                              analysis_manager, alias_analysis,
                                              memory_ssa, function)) {
                    ++index;
                    continue;
                }
                if (!dedicated_preheader &&
                    !instruction_can_hoist_to_non_dedicated_predecessor(
                        *instruction, loop, dominator_tree, module,
                        analysis_manager, alias_analysis, memory_ssa,
                        function)) {
                    ++index;
                    continue;
                }

                if (move_instruction_to_preheader(*block_ptr, index,
                                                  *hoist_block)) {
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
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager =
        build_result->get_analysis_manager();
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
        const CoreIrCfgAnalysisResult &cfg =
            analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
        const CoreIrDominatorTreeAnalysisResult &dominator_tree =
            analysis_manager->get_or_compute<CoreIrDominatorTreeAnalysis>(
                *function);

        const CoreIrAliasAnalysisResult &alias_analysis =
            analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
        const CoreIrMemorySSAAnalysisResult &memory_ssa =
            analysis_manager->get_or_compute<CoreIrMemorySSAAnalysis>(
                *function);

        std::vector<const CoreIrLoopInfo *> ordered_loops;
        ordered_loops.reserve(loop_info.get_loops().size());
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr != nullptr) {
                ordered_loops.push_back(loop_ptr.get());
            }
        }
        std::stable_sort(
            ordered_loops.begin(), ordered_loops.end(),
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
            function_changed =
                run_licm_on_loop(*function, *loop, cfg, dominator_tree, scev,
                                 module, *analysis_manager, alias_analysis,
                                 memory_ssa) ||
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
    effects.preserved_analyses.preserve(CoreIrAnalysisKind::CallGraph);
    effects.preserved_analyses.preserve(CoreIrAnalysisKind::FunctionAttrs);
    effects.preserved_analyses.preserve_cfg_family();
    effects.preserved_analyses.preserve(CoreIrAnalysisKind::EscapeAnalysis);
    effects.preserved_analyses.preserve(CoreIrAnalysisKind::AliasAnalysis);
    effects.preserved_analyses.preserve(
        CoreIrAnalysisKind::FunctionEffectSummary);
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
