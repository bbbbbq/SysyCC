#include "backend/ir/loop_rotate/core_ir_loop_rotate_pass.hpp"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/effect/core_ir_effect.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
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

bool block_has_phi(const CoreIrBasicBlock &block) {
    for (const auto &instruction : block.get_instructions()) {
        if (instruction == nullptr) {
            continue;
        }
        if (instruction->get_opcode() == CoreIrOpcode::Phi) {
            return true;
        }
        break;
    }
    return false;
}

bool loop_contains_block(const CoreIrLoopInfo &loop,
                         const CoreIrBasicBlock *block) {
    return block != nullptr &&
           loop.get_blocks().find(const_cast<CoreIrBasicBlock *>(block)) !=
               loop.get_blocks().end();
}

bool is_cloneable_header_instruction(const CoreIrInstruction &instruction) {
    if (instruction.get_is_terminator() || instruction.get_has_side_effect()) {
        return false;
    }
    const CoreIrEffectInfo effect = get_core_ir_instruction_effect(instruction);
    if (!effect.is_pure_value) {
        return false;
    }
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Cast:
        return true;
    case CoreIrOpcode::Phi:
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::GetElementPtr:
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

bool header_instructions_only_feed_condition(const CoreIrBasicBlock &header) {
    if (header.get_instructions().empty()) {
        return false;
    }
    const CoreIrInstruction *terminator = header.get_instructions().back().get();
    auto *branch = dynamic_cast<const CoreIrCondJumpInst *>(terminator);
    if (branch == nullptr) {
        return false;
    }

    std::unordered_set<const CoreIrInstruction *> allowed_users{branch};
    for (const auto &instruction : header.get_instructions()) {
        const CoreIrInstruction *current = instruction.get();
        if (current == nullptr || current == terminator) {
            continue;
        }
        if (!is_cloneable_header_instruction(*current)) {
            return false;
        }
        for (const CoreIrUse &use : current->get_uses()) {
            if (allowed_users.find(use.get_user()) == allowed_users.end()) {
                return false;
            }
        }
        allowed_users.insert(current);
    }
    return true;
}

CoreIrInstruction *clone_instruction_with_map(
    const CoreIrInstruction &instruction,
    const std::unordered_map<CoreIrValue *, CoreIrValue *> &value_map) {
    auto remap = [&value_map](CoreIrValue *value) -> CoreIrValue * {
        auto it = value_map.find(value);
        return it == value_map.end() ? value : it->second;
    };

    switch (instruction.get_opcode()) {
    case CoreIrOpcode::Binary: {
        const auto &binary = static_cast<const CoreIrBinaryInst &>(instruction);
        auto clone = std::make_unique<CoreIrBinaryInst>(
            binary.get_binary_opcode(), binary.get_type(), binary.get_name(),
            remap(binary.get_lhs()), remap(binary.get_rhs()));
        clone->set_source_span(binary.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Unary: {
        const auto &unary = static_cast<const CoreIrUnaryInst &>(instruction);
        auto clone = std::make_unique<CoreIrUnaryInst>(
            unary.get_unary_opcode(), unary.get_type(), unary.get_name(),
            remap(unary.get_operand()));
        clone->set_source_span(unary.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Compare: {
        const auto &compare = static_cast<const CoreIrCompareInst &>(instruction);
        auto clone = std::make_unique<CoreIrCompareInst>(
            compare.get_predicate(), compare.get_type(), compare.get_name(),
            remap(compare.get_lhs()), remap(compare.get_rhs()));
        clone->set_source_span(compare.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Cast: {
        const auto &cast = static_cast<const CoreIrCastInst &>(instruction);
        auto clone = std::make_unique<CoreIrCastInst>(
            cast.get_cast_kind(), cast.get_type(), cast.get_name(),
            remap(cast.get_operand()));
        clone->set_source_span(cast.get_source_span());
        return clone.release();
    }
    default:
        return nullptr;
    }
}

bool rotate_loop(CoreIrFunction &function, const CoreIrLoopInfo &loop) {
    CoreIrBasicBlock *preheader = loop.get_preheader();
    CoreIrBasicBlock *header = loop.get_header();
    if (preheader == nullptr || header == nullptr || loop.get_latches().size() != 1 ||
        block_has_phi(*header) || header->get_instructions().empty() ||
        !header_instructions_only_feed_condition(*header)) {
        return false;
    }

    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        preheader->get_instructions().empty() ? nullptr
                                              : preheader->get_instructions().back().get());
    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        header->get_instructions().back().get());
    if (preheader_jump == nullptr || header_branch == nullptr ||
        preheader_jump->get_target_block() != header) {
        return false;
    }

    CoreIrBasicBlock *inside_successor = nullptr;
    CoreIrBasicBlock *outside_successor = nullptr;
    if (loop_contains_block(loop, header_branch->get_true_block()) &&
        !loop_contains_block(loop, header_branch->get_false_block())) {
        inside_successor = header_branch->get_true_block();
        outside_successor = header_branch->get_false_block();
    } else if (!loop_contains_block(loop, header_branch->get_true_block()) &&
               loop_contains_block(loop, header_branch->get_false_block())) {
        inside_successor = header_branch->get_false_block();
        outside_successor = header_branch->get_true_block();
    } else {
        return false;
    }

    if (inside_successor == nullptr || outside_successor == nullptr ||
        block_has_phi(*inside_successor) || block_has_phi(*outside_successor)) {
        return false;
    }

    std::unordered_map<CoreIrValue *, CoreIrValue *> value_map;
    std::vector<std::unique_ptr<CoreIrInstruction>> cloned_instructions;
    for (const auto &instruction_ptr : header->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction->get_is_terminator()) {
            continue;
        }
        CoreIrInstruction *clone =
            clone_instruction_with_map(*instruction, value_map);
        if (clone == nullptr) {
            return false;
        }
        value_map[instruction] = clone;
        cloned_instructions.emplace_back(clone);
    }

    CoreIrValue *cloned_condition = header_branch->get_condition();
    auto condition_it = value_map.find(header_branch->get_condition());
    if (condition_it != value_map.end()) {
        cloned_condition = condition_it->second;
    }

    auto &preheader_instructions = preheader->get_instructions();
    preheader_instructions.pop_back();
    for (auto &clone : cloned_instructions) {
        clone->set_parent(preheader);
        preheader_instructions.push_back(std::move(clone));
    }
    auto rotated_branch = std::make_unique<CoreIrCondJumpInst>(
        header_branch->get_type(), cloned_condition, inside_successor,
        outside_successor);
    rotated_branch->set_source_span(header_branch->get_source_span());
    rotated_branch->set_parent(preheader);
    preheader_instructions.push_back(std::move(rotated_branch));
    return true;
}

} // namespace

PassKind CoreIrLoopRotatePass::Kind() const { return PassKind::CoreIrLoopRotate; }

const char *CoreIrLoopRotatePass::Name() const { return "CoreIrLoopRotatePass"; }

PassResult CoreIrLoopRotatePass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure("missing core ir loop rotate dependencies");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);

        std::vector<const CoreIrLoopInfo *> ordered_loops;
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr != nullptr) {
                ordered_loops.push_back(loop_ptr.get());
            }
        }
        std::sort(ordered_loops.begin(), ordered_loops.end(),
                  [](const CoreIrLoopInfo *lhs, const CoreIrLoopInfo *rhs) {
                      return lhs->get_depth() > rhs->get_depth();
                  });

        bool function_changed = false;
        for (const CoreIrLoopInfo *loop : ordered_loops) {
            function_changed = rotate_loop(*function, *loop) || function_changed;
        }
        if (function_changed) {
            effects.changed_functions.insert(function.get());
            effects.cfg_changed_functions.insert(function.get());
        }
    }

    effects.preserved_analyses = effects.has_changes()
                                     ? CoreIrPreservedAnalyses::preserve_none()
                                     : CoreIrPreservedAnalyses::preserve_all();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc

