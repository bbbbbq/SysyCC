#include "backend/ir/gvn/core_ir_gvn_pass.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "backend/ir/analysis/analysis_manager.hpp"
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

void append_pointer_key(std::string &key, const void *pointer) {
    key += std::to_string(reinterpret_cast<std::uintptr_t>(pointer));
    key.push_back(';');
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
    const std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>> &children,
    std::unordered_map<std::string, CoreIrInstruction *> available_values) {
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
            !is_gvn_candidate(*instruction)) {
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
            changed = run_gvn_block(*child, children, available_values) || changed;
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

    for (const auto &function : module->get_functions()) {
        std::unordered_map<CoreIrBasicBlock *, std::vector<CoreIrBasicBlock *>> children;
        build_dominator_children(*function, *analysis_manager, children);
        const CoreIrCfgAnalysisResult &cfg_analysis =
            analysis_manager->get_or_compute<CoreIrCfgAnalysis>(*function);
        CoreIrBasicBlock *entry_block =
            const_cast<CoreIrBasicBlock *>(cfg_analysis.get_entry_block());
        if (entry_block == nullptr) {
            continue;
        }
        if (run_gvn_block(*entry_block, children, {})) {
            build_result->invalidate_core_ir_analyses(*function);
        }
    }

    return PassResult::Success();
}

} // namespace sysycc
