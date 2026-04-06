#include "backend/ir/local_cse/core_ir_local_cse_pass.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

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

std::string build_local_cse_key(const CoreIrInstruction &instruction) {
    std::string key;
    key += std::to_string(static_cast<int>(instruction.get_opcode()));
    key.push_back(':');
    append_pointer_key(key, instruction.get_type());

    if (const auto *binary = dynamic_cast<const CoreIrBinaryInst *>(&instruction);
        binary != nullptr) {
        key += std::to_string(static_cast<int>(binary->get_binary_opcode()));
        key.push_back(':');
        append_pointer_key(key, binary->get_lhs());
        append_pointer_key(key, binary->get_rhs());
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

bool is_local_cse_candidate(const CoreIrInstruction &instruction) {
    return dynamic_cast<const CoreIrBinaryInst *>(&instruction) != nullptr ||
           dynamic_cast<const CoreIrUnaryInst *>(&instruction) != nullptr ||
           dynamic_cast<const CoreIrCompareInst *>(&instruction) != nullptr ||
           dynamic_cast<const CoreIrCastInst *>(&instruction) != nullptr ||
           dynamic_cast<const CoreIrGetElementPtrInst *>(&instruction) != nullptr;
}

bool run_local_cse(CoreIrBasicBlock &block) {
    bool changed = false;
    std::unordered_map<std::string, CoreIrInstruction *> available_values;
    auto &instructions = block.get_instructions();

    std::size_t index = 0;
    while (index < instructions.size()) {
        CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr) {
            instructions.erase(instructions.begin() + index);
            changed = true;
            continue;
        }

        if (!is_local_cse_candidate(*instruction)) {
            ++index;
            continue;
        }

        const std::string key = build_local_cse_key(*instruction);
        auto it = available_values.find(key);
        if (it == available_values.end()) {
            available_values.emplace(key, instruction);
            ++index;
            continue;
        }

        instruction->replace_all_uses_with(it->second);
        erase_instruction(block, instruction);
        changed = true;
    }

    return changed;
}

} // namespace

PassKind CoreIrLocalCsePass::Kind() const { return PassKind::CoreIrLocalCse; }

const char *CoreIrLocalCsePass::Name() const { return "CoreIrLocalCsePass"; }

PassResult CoreIrLocalCsePass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        bool function_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            function_changed = run_local_cse(*block) || function_changed;
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
    effects.preserved_analyses.preserve_loop_family();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
