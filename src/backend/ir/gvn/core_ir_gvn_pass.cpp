#include "backend/ir/gvn/core_ir_gvn_pass.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "backend/ir/analysis/alias_analysis.hpp"
#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/memory_ssa_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::are_equivalent_types;
using sysycc::detail::erase_instruction;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

void append_pointer_key(std::string &key, const void *pointer) {
    key += std::to_string(reinterpret_cast<std::uintptr_t>(pointer));
    key.push_back(';');
}

void append_memory_location_key(std::string &key,
                                const CoreIrMemoryLocation &location) {
    key += std::to_string(static_cast<int>(location.root_kind));
    key.push_back(':');
    switch (location.root_kind) {
    case CoreIrMemoryLocationRootKind::StackSlot:
        append_pointer_key(key, location.stack_slot);
        break;
    case CoreIrMemoryLocationRootKind::Global:
        append_pointer_key(key, location.global);
        break;
    case CoreIrMemoryLocationRootKind::ArgumentDerived:
        key += std::to_string(location.parameter_index);
        key.push_back(';');
        break;
    case CoreIrMemoryLocationRootKind::Unknown:
        key += "unknown;";
        break;
    }
    for (std::uint64_t index : location.access_path) {
        key += std::to_string(index);
        key.push_back('/');
    }
    key.push_back(';');
}

CoreIrStackSlot *get_memory_stack_slot(const CoreIrInstruction &instruction) {
    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        return load->get_stack_slot();
    }
    if (const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
        store != nullptr) {
        return store->get_stack_slot();
    }
    return nullptr;
}

CoreIrValue *get_memory_address_value(const CoreIrInstruction &instruction) {
    if (const auto *load = dynamic_cast<const CoreIrLoadInst *>(&instruction);
        load != nullptr) {
        return load->get_address();
    }
    if (const auto *store = dynamic_cast<const CoreIrStoreInst *>(&instruction);
        store != nullptr) {
        return store->get_address();
    }
    return nullptr;
}

bool instructions_share_exact_memory_access(const CoreIrInstruction &lhs,
                                            const CoreIrInstruction &rhs) {
    CoreIrStackSlot *lhs_slot = get_memory_stack_slot(lhs);
    CoreIrStackSlot *rhs_slot = get_memory_stack_slot(rhs);
    if (lhs_slot != nullptr || rhs_slot != nullptr) {
        return lhs_slot != nullptr && lhs_slot == rhs_slot && rhs_slot != nullptr;
    }

    CoreIrValue *lhs_address = get_memory_address_value(lhs);
    CoreIrValue *rhs_address = get_memory_address_value(rhs);
    return lhs_address != nullptr && lhs_address == rhs_address &&
           rhs_address != nullptr;
}

CoreIrAliasKind get_precise_memory_alias_kind(
    const CoreIrInstruction &lhs, const CoreIrInstruction &rhs,
    const CoreIrAliasAnalysisResult &alias_analysis) {
    CoreIrAliasKind alias_kind = alias_analysis.alias_instructions(&lhs, &rhs);
    if (alias_kind != CoreIrAliasKind::MayAlias) {
        return alias_kind;
    }
    return instructions_share_exact_memory_access(lhs, rhs)
               ? CoreIrAliasKind::MustAlias
               : alias_kind;
}

bool is_commutative_binary(CoreIrBinaryOpcode opcode) {
    switch (opcode) {
    case CoreIrBinaryOpcode::Add:
    case CoreIrBinaryOpcode::Mul:
    case CoreIrBinaryOpcode::And:
    case CoreIrBinaryOpcode::Or:
    case CoreIrBinaryOpcode::Xor:
        return true;
    case CoreIrBinaryOpcode::Sub:
    case CoreIrBinaryOpcode::SDiv:
    case CoreIrBinaryOpcode::UDiv:
    case CoreIrBinaryOpcode::SRem:
    case CoreIrBinaryOpcode::URem:
    case CoreIrBinaryOpcode::Shl:
    case CoreIrBinaryOpcode::LShr:
    case CoreIrBinaryOpcode::AShr:
        return false;
    }
    return false;
}

std::string build_gvn_key(const CoreIrInstruction &instruction) {
    std::string key;
    key += std::to_string(static_cast<int>(instruction.get_opcode()));
    key.push_back(':');
    append_pointer_key(key, instruction.get_type());

    if (const auto *binary = dynamic_cast<const CoreIrBinaryInst *>(&instruction);
        binary != nullptr) {
        key += std::to_string(static_cast<int>(binary->get_binary_opcode()));
        key.push_back(':');
        const void *lhs = binary->get_lhs();
        const void *rhs = binary->get_rhs();
        if (is_commutative_binary(binary->get_binary_opcode()) &&
            reinterpret_cast<std::uintptr_t>(lhs) >
                reinterpret_cast<std::uintptr_t>(rhs)) {
            std::swap(lhs, rhs);
        }
        append_pointer_key(key, lhs);
        append_pointer_key(key, rhs);
        return key;
    }

    if (const auto *unary = dynamic_cast<const CoreIrUnaryInst *>(&instruction);
        unary != nullptr) {
        key += std::to_string(static_cast<int>(unary->get_unary_opcode()));
        key.push_back(':');
        append_pointer_key(key, unary->get_operand());
        return key;
    }

    if (const auto *compare = dynamic_cast<const CoreIrCompareInst *>(&instruction);
        compare != nullptr) {
        key += std::to_string(static_cast<int>(compare->get_predicate()));
        key.push_back(':');
        append_pointer_key(key, compare->get_lhs());
        append_pointer_key(key, compare->get_rhs());
        return key;
    }

    if (const auto *cast = dynamic_cast<const CoreIrCastInst *>(&instruction);
        cast != nullptr) {
        key += std::to_string(static_cast<int>(cast->get_cast_kind()));
        key.push_back(':');
        append_pointer_key(key, cast->get_operand());
        return key;
    }

    const auto *gep = dynamic_cast<const CoreIrGetElementPtrInst *>(&instruction);
    if (gep != nullptr) {
        append_pointer_key(key, gep->get_base());
        key += std::to_string(gep->get_index_count());
        key.push_back(':');
        for (std::size_t index = 0; index < gep->get_index_count(); ++index) {
            append_pointer_key(key, gep->get_index(index));
        }
    }

    return key;
}

bool is_gvn_candidate(const CoreIrInstruction &instruction) {
    return dynamic_cast<const CoreIrBinaryInst *>(&instruction) != nullptr ||
           dynamic_cast<const CoreIrUnaryInst *>(&instruction) != nullptr ||
           dynamic_cast<const CoreIrCompareInst *>(&instruction) != nullptr ||
           dynamic_cast<const CoreIrCastInst *>(&instruction) != nullptr ||
           dynamic_cast<const CoreIrGetElementPtrInst *>(&instruction) != nullptr;
}

std::optional<std::string>
build_load_gvn_key(const CoreIrLoadInst &load,
                   const CoreIrAliasAnalysisResult &alias_analysis,
                   const CoreIrMemorySSAAnalysisResult &memory_ssa) {
    CoreIrMemoryAccess *clobber = memory_ssa.get_clobbering_access(&load);
    if (clobber == nullptr) {
        return std::nullopt;
    }

    std::string key = "load:";
    append_pointer_key(key, load.get_type());
    key += "mem:";
    if (const CoreIrMemoryLocation *location =
            alias_analysis.get_location_for_instruction(&load);
        location != nullptr &&
        location->root_kind != CoreIrMemoryLocationRootKind::Unknown) {
        append_memory_location_key(key, *location);
    } else if (load.get_stack_slot() != nullptr) {
        append_pointer_key(key, load.get_stack_slot());
    } else if (load.get_address() != nullptr) {
        append_pointer_key(key, load.get_address());
    } else {
        return std::nullopt;
    }
    key += "clobber:";
    key += std::to_string(clobber->get_id());
    key.push_back(';');
    return key;
}

CoreIrValue *resolve_load_replacement(
    const CoreIrLoadInst &load, const CoreIrAliasAnalysisResult &alias_analysis,
    const CoreIrMemorySSAAnalysisResult &memory_ssa,
    const std::unordered_map<std::string, CoreIrValue *> &available_values,
    std::optional<std::string> &key_out) {
    key_out = build_load_gvn_key(load, alias_analysis, memory_ssa);
    if (!key_out.has_value()) {
        return nullptr;
    }

    auto available_it = available_values.find(*key_out);
    if (available_it != available_values.end()) {
        return available_it->second;
    }

    auto *def = dynamic_cast<CoreIrMemoryDefAccess *>(
        memory_ssa.get_clobbering_access(&load));
    auto *store = def == nullptr ? nullptr
                                 : dynamic_cast<const CoreIrStoreInst *>(
                                       def->get_instruction());
    if (store == nullptr || store->get_value() == nullptr ||
        !are_equivalent_types(load.get_type(), store->get_value()->get_type()) ||
        get_precise_memory_alias_kind(load, *store, alias_analysis) !=
            CoreIrAliasKind::MustAlias) {
        return nullptr;
    }
    return const_cast<CoreIrValue *>(store->get_value());
}

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

bool run_gvn_block(
    CoreIrBasicBlock &block,
    const CoreIrAliasAnalysisResult &alias_analysis,
    const CoreIrMemorySSAAnalysisResult &memory_ssa,
    const std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>> &children,
    std::unordered_map<std::string, CoreIrValue *> available_values) {
    bool changed = false;
    auto &instructions = block.get_instructions();
    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() + index);
            changed = true;
            continue;
        }
        if (dynamic_cast<CoreIrPhiInst *>(instruction) != nullptr ||
            instruction->get_is_terminator()) {
            ++index;
            continue;
        }

        if (auto *load = dynamic_cast<CoreIrLoadInst *>(instruction); load != nullptr) {
            std::optional<std::string> load_key;
            CoreIrValue *replacement = resolve_load_replacement(
                *load, alias_analysis, memory_ssa, available_values, load_key);
            if (replacement != nullptr) {
                load->replace_all_uses_with(replacement);
                erase_instruction(block, load);
                changed = true;
                if (load_key.has_value()) {
                    available_values[*load_key] = replacement;
                }
                continue;
            }
            if (load_key.has_value()) {
                available_values[*load_key] = load;
            }
            ++index;
            continue;
        }

        if (!is_gvn_candidate(*instruction)) {
            ++index;
            continue;
        }

        const std::string key = build_gvn_key(*instruction);
        auto it = available_values.find(key);
        if (it != available_values.end()) {
            instruction->replace_all_uses_with(it->second);
            erase_instruction(block, instruction);
            changed = true;
            continue;
        }
        available_values.emplace(key, instruction);
        ++index;
    }

    auto child_it = children.find(&block);
    if (child_it != children.end()) {
        for (CoreIrBasicBlock *child : child_it->second) {
            changed = run_gvn_block(*child, alias_analysis, memory_ssa, children,
                                    available_values) ||
                      changed;
        }
    }
    return changed;
}

} // namespace

PassKind CoreIrGvnPass::Kind() const { return PassKind::CoreIrGvn; }

const char *CoreIrGvnPass::Name() const { return "CoreIrGvnPass"; }

PassResult CoreIrGvnPass::Run(CompilerContext &context) {
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
        const CoreIrAliasAnalysisResult &alias_analysis =
            analysis_manager->get_or_compute<CoreIrAliasAnalysis>(*function);
        const CoreIrMemorySSAAnalysisResult &memory_ssa =
            analysis_manager->get_or_compute<CoreIrMemorySSAAnalysis>(*function);
        std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>> children;
        build_dominator_children(*function, *analysis_manager, children);
        const CoreIrCfgAnalysisResult &cfg_analysis =
            analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
        CoreIrBasicBlock *entry_block =
            const_cast<CoreIrBasicBlock *>(cfg_analysis.get_entry_block());
        if (entry_block == nullptr) {
            continue;
        }
        if (run_gvn_block(*entry_block, alias_analysis, memory_ssa, children, {})) {
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
