#include "backend/ir/inliner/core_ir_inliner_pass.hpp"

#include <string>
#include <unordered_map>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/detail/core_ir_clone_utils.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::erase_instruction;
using sysycc::detail::clone_instruction_remapped;
using sysycc::detail::insert_instruction_before;

constexpr std::size_t kInlineBudget = 24;
constexpr std::size_t kAlwaysInlineBudget = 96;

PassResult fail_missing_core_ir(CompilerContext &context, const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler, message);
    return PassResult::Failure(message);
}

bool instruction_is_inline_cloneable(const CoreIrInstruction &instruction) {
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::GetElementPtr:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Store:
        return true;
    default:
        return false;
    }
}

bool callee_is_inline_candidate(const CoreIrFunction &callee,
                                const CoreIrCallGraphAnalysisResult &call_graph) {
    if (callee.get_basic_blocks().size() != 1 || !callee.get_stack_slots().empty() ||
        callee.get_is_variadic() || call_graph.is_recursive(&callee) ||
        callee.get_basic_blocks().front() == nullptr) {
        return false;
    }
    const CoreIrBasicBlock &block = *callee.get_basic_blocks().front();
    if (block.get_instructions().empty()) {
        return false;
    }
    const std::size_t budget =
        callee.get_is_always_inline() ? kAlwaysInlineBudget : kInlineBudget;
    if (block.get_instructions().size() > budget) {
        return false;
    }
    const auto &instructions = block.get_instructions();
    auto *return_inst =
        dynamic_cast<const CoreIrReturnInst *>(instructions.back().get());
    if (return_inst == nullptr) {
        return false;
    }
    for (std::size_t index = 0; index + 1 < instructions.size(); ++index) {
        const CoreIrInstruction *instruction = instructions[index].get();
        if (instruction == nullptr || instruction->get_is_terminator() ||
            !instruction_is_inline_cloneable(*instruction)) {
            return false;
        }
    }
    for (const auto &instruction_ptr : instructions) {
        const CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction->get_opcode() == CoreIrOpcode::Phi ||
            instruction->get_opcode() == CoreIrOpcode::AddressOfStackSlot ||
            instruction->get_opcode() == CoreIrOpcode::Call) {
            return false;
        }
    }
    return true;
}

bool inline_direct_call(CoreIrCallInst &call, CoreIrFunction &callee) {
    CoreIrBasicBlock *caller_block = call.get_parent();
    CoreIrBasicBlock *callee_block =
        callee.get_basic_blocks().empty() ? nullptr : callee.get_basic_blocks().front().get();
    if (caller_block == nullptr || callee_block == nullptr ||
        callee_block->get_instructions().empty()) {
        return false;
    }

    std::unordered_map<const CoreIrValue *, CoreIrValue *> value_map;
    for (std::size_t index = 0; index < callee.get_parameters().size(); ++index) {
        CoreIrParameter *parameter = callee.get_parameters()[index].get();
        if (parameter != nullptr && index < call.get_argument_count()) {
            value_map.emplace(parameter, call.get_argument(index));
        }
    }

    for (const auto &instruction_ptr : callee_block->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction->get_is_terminator()) {
            continue;
        }
        std::unique_ptr<CoreIrInstruction> clone =
            clone_instruction_remapped(*instruction, value_map);
        if (clone == nullptr) {
            return false;
        }
        CoreIrInstruction *inserted = insert_instruction_before(
            *caller_block, &call, std::move(clone));
        value_map.emplace(instruction, inserted);
    }

    auto *ret =
        dynamic_cast<CoreIrReturnInst *>(callee_block->get_instructions().back().get());
    if (ret == nullptr) {
        return false;
    }
    CoreIrValue *return_value = ret->get_return_value();
    auto it = value_map.find(return_value);
    if (it != value_map.end()) {
        return_value = it->second;
    }
    if (return_value != nullptr) {
        call.replace_all_uses_with(return_value);
    }
    return erase_instruction(*caller_block, &call);
}

} // namespace

PassKind CoreIrInlinerPass::Kind() const { return PassKind::CoreIrInliner; }

const char *CoreIrInlinerPass::Name() const { return "CoreIrInlinerPass"; }

CoreIrPassMetadata CoreIrInlinerPass::Metadata() const noexcept {
    return CoreIrPassMetadata::core_ir_transform();
}

PassResult CoreIrInlinerPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir analysis manager");
    }

    const CoreIrCallGraphAnalysisResult &call_graph =
        analysis_manager->get_or_compute<CoreIrCallGraphAnalysis>(*module);

    CoreIrPassEffects effects;
    for (const auto &function_ptr : module->get_functions()) {
        CoreIrFunction *caller = function_ptr.get();
        if (caller == nullptr) {
            continue;
        }
        bool changed = false;
        for (const auto &block_ptr : caller->get_basic_blocks()) {
            if (block_ptr == nullptr) {
                continue;
            }
            auto &instructions = block_ptr->get_instructions();
            for (std::size_t index = 0; index < instructions.size();) {
                auto *call = dynamic_cast<CoreIrCallInst *>(instructions[index].get());
                if (call == nullptr || !call->get_is_direct_call()) {
                    ++index;
                    continue;
                }
                CoreIrFunction *callee = module->find_function(call->get_callee_name());
                if (callee == nullptr || callee == caller ||
                    (!callee->get_is_internal_linkage() &&
                     !callee->get_is_always_inline()) ||
                    !callee_is_inline_candidate(*callee, call_graph)) {
                    ++index;
                    continue;
                }
                if (!inline_direct_call(*call, *callee)) {
                    ++index;
                    continue;
                }
                changed = true;
                continue;
            }
        }
        if (changed) {
            effects.module_changed = true;
        }
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
