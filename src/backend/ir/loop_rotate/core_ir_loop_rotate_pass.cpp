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
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::erase_instruction;

PassResult fail_missing_core_ir(CompilerContext &context,
                                const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
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
    case CoreIrOpcode::Select:
    case CoreIrOpcode::Cast:
        return true;
    case CoreIrOpcode::Phi:
    case CoreIrOpcode::ExtractElement:
    case CoreIrOpcode::InsertElement:
    case CoreIrOpcode::ShuffleVector:
    case CoreIrOpcode::VectorReduceAdd:
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::GetElementPtr:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Call:
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::IndirectJump:
    case CoreIrOpcode::Return:
        return false;
    }
    return false;
}

struct HeaderPhiInfo {
    CoreIrPhiInst *phi = nullptr;
    std::size_t preheader_index = 0;
    std::size_t latch_index = 0;
    CoreIrValue *preheader_value = nullptr;
    CoreIrValue *latch_value = nullptr;
};

CoreIrInstruction *clone_instruction_with_map(
    const CoreIrInstruction &instruction,
    const std::unordered_map<CoreIrValue *, CoreIrValue *> &value_map);

CoreIrBasicBlock *get_use_location_block(const CoreIrUse &use) {
    CoreIrInstruction *user = use.get_user();
    if (user == nullptr) {
        return nullptr;
    }
    if (auto *phi = dynamic_cast<CoreIrPhiInst *>(user); phi != nullptr) {
        return phi->get_incoming_block(use.get_operand_index());
    }
    return user->get_parent();
}

bool collect_header_shape(const CoreIrBasicBlock &header,
                          CoreIrBasicBlock *preheader,
                          CoreIrBasicBlock *latch,
                          std::vector<HeaderPhiInfo> &header_phis,
                          std::vector<CoreIrInstruction *> &header_instructions) {
    if (header.get_instructions().empty()) {
        return false;
    }
    const CoreIrInstruction *terminator =
        header.get_instructions().back().get();
    auto *branch = dynamic_cast<const CoreIrCondJumpInst *>(terminator);
    if (branch == nullptr) {
        return false;
    }

    for (const auto &instruction : header.get_instructions()) {
        const CoreIrInstruction *current = instruction.get();
        if (current == nullptr || current == terminator) {
            continue;
        }
        if (current->get_opcode() == CoreIrOpcode::Phi) {
            auto *phi = const_cast<CoreIrPhiInst *>(
                static_cast<const CoreIrPhiInst *>(current));
            if (!header_instructions.empty() || phi->get_incoming_count() != 2) {
                return false;
            }

            const std::size_t incoming_count = phi->get_incoming_count();
            std::size_t preheader_index =
                phi->get_incoming_block(0) == preheader
                    ? 0
                    : phi->get_incoming_block(1) == preheader
                          ? 1
                          : incoming_count;
            if (preheader_index >= incoming_count) {
                return false;
            }
            const std::size_t latch_index = preheader_index == 0 ? 1 : 0;
            if (phi->get_incoming_block(latch_index) != latch) {
                return false;
            }

            header_phis.push_back(HeaderPhiInfo{
                phi, preheader_index, latch_index,
                phi->get_incoming_value(preheader_index),
                phi->get_incoming_value(latch_index)});
            continue;
        }
        if (!is_cloneable_header_instruction(*current)) {
            return false;
        }
        header_instructions.push_back(const_cast<CoreIrInstruction *>(current));
    }

    std::unordered_set<const CoreIrInstruction *> allowed_users{branch};
    for (auto it = header_instructions.rbegin();
         it != header_instructions.rend(); ++it) {
        const CoreIrInstruction *current = *it;
        for (const CoreIrUse &use : current->get_uses()) {
            if (allowed_users.find(use.get_user()) == allowed_users.end()) {
                return false;
            }
        }
        allowed_users.insert(current);
    }
    return true;
}

bool build_cloned_header_condition(
    const std::vector<CoreIrInstruction *> &header_instructions,
    CoreIrValue *original_condition,
    const std::unordered_map<CoreIrValue *, CoreIrValue *> &seed_value_map,
    std::vector<std::unique_ptr<CoreIrInstruction>> &cloned_instructions,
    CoreIrValue *&cloned_condition) {
    std::unordered_map<CoreIrValue *, CoreIrValue *> value_map = seed_value_map;
    for (CoreIrInstruction *instruction : header_instructions) {
        if (instruction == nullptr) {
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

    cloned_condition = original_condition;
    auto condition_it = value_map.find(original_condition);
    if (condition_it != value_map.end()) {
        cloned_condition = condition_it->second;
    }
    return true;
}

bool resolve_rotated_exit_value(
    CoreIrValue *value,
    const std::unordered_map<CoreIrPhiInst *, HeaderPhiInfo *> &header_phi_map,
    CoreIrBasicBlock *header, CoreIrValue *&preheader_value,
    CoreIrValue *&latch_value) {
    if (value == nullptr) {
        return false;
    }

    if (auto *phi = dynamic_cast<CoreIrPhiInst *>(value); phi != nullptr) {
        auto it = header_phi_map.find(phi);
        if (it == header_phi_map.end()) {
            return false;
        }
        preheader_value = it->second->preheader_value;
        latch_value = it->second->latch_value;
        return true;
    }

    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction != nullptr && instruction->get_parent() == header) {
        return false;
    }

    preheader_value = value;
    latch_value = value;
    return true;
}

bool rewrite_outside_successor_phis(
    CoreIrBasicBlock *outside_successor, CoreIrBasicBlock *header,
    CoreIrBasicBlock *preheader, CoreIrBasicBlock *latch,
    const std::unordered_map<CoreIrPhiInst *, HeaderPhiInfo *> &header_phi_map) {
    if (outside_successor == nullptr || header == nullptr || preheader == nullptr ||
        latch == nullptr) {
        return false;
    }

    for (const auto &instruction : outside_successor->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction.get());
        if (phi == nullptr) {
            break;
        }

        bool has_header_incoming = false;
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) == header) {
                has_header_incoming = true;
            } else if (phi->get_incoming_block(index) == preheader ||
                       phi->get_incoming_block(index) == latch) {
                return false;
            }
        }
        if (!has_header_incoming) {
            continue;
        }

        CoreIrValue *preheader_value = nullptr;
        CoreIrValue *latch_value = nullptr;
        bool resolved = false;
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_block(index) != header) {
                continue;
            }
            if (resolved) {
                return false;
            }
            resolved = resolve_rotated_exit_value(phi->get_incoming_value(index),
                                                  header_phi_map, header,
                                                  preheader_value, latch_value);
        }
        if (!resolved) {
            return false;
        }

        phi->remove_incoming_block(header);
        phi->add_incoming(preheader, preheader_value);
        phi->add_incoming(latch, latch_value);
    }
    return true;
}

bool repair_phi_external_uses(const CoreIrLoopInfo &loop,
                              CoreIrBasicBlock *outside_successor,
                              const std::vector<HeaderPhiInfo> &header_phis) {
    if (outside_successor == nullptr) {
        return false;
    }

    for (const HeaderPhiInfo &info : header_phis) {
        if (info.phi == nullptr) {
            continue;
        }

        bool has_external_use = false;
        for (const CoreIrUse &use : info.phi->get_uses()) {
            if (!loop_contains_block(loop, get_use_location_block(use))) {
                has_external_use = true;
                break;
            }
        }
        if (!has_external_use) {
            continue;
        }

        auto exit_phi_inst = std::make_unique<CoreIrPhiInst>(
            info.phi->get_type(), info.phi->get_name() + ".loop.rotate.exit");
        auto *exit_phi = static_cast<CoreIrPhiInst *>(
            outside_successor->insert_instruction_before_first_non_phi(
                std::move(exit_phi_inst)));
        if (exit_phi == nullptr) {
            return false;
        }
        exit_phi->add_incoming(info.phi->get_incoming_block(info.preheader_index),
                               info.preheader_value);
        exit_phi->add_incoming(info.phi->get_incoming_block(info.latch_index),
                               info.latch_value);

        const std::vector<CoreIrUse> uses = info.phi->get_uses();
        for (const CoreIrUse &use : uses) {
            CoreIrBasicBlock *use_block = get_use_location_block(use);
            if (loop_contains_block(loop, use_block) || use.get_user() == nullptr) {
                continue;
            }
            use.get_user()->set_operand(use.get_operand_index(), exit_phi);
        }
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
        const auto &compare =
            static_cast<const CoreIrCompareInst &>(instruction);
        auto clone = std::make_unique<CoreIrCompareInst>(
            compare.get_predicate(), compare.get_type(), compare.get_name(),
            remap(compare.get_lhs()), remap(compare.get_rhs()));
        clone->set_source_span(compare.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Select: {
        const auto &select = static_cast<const CoreIrSelectInst &>(instruction);
        auto clone = std::make_unique<CoreIrSelectInst>(
            select.get_type(), select.get_name(), remap(select.get_condition()),
            remap(select.get_true_value()), remap(select.get_false_value()));
        clone->set_source_span(select.get_source_span());
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

bool rotate_phi_free_loop(CoreIrBasicBlock *preheader, CoreIrBasicBlock *header,
                          CoreIrCondJumpInst *header_branch,
                          CoreIrBasicBlock *inside_successor,
                          CoreIrBasicBlock *outside_successor,
                          const std::vector<CoreIrInstruction *> &header_instructions) {
    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        preheader->get_instructions().empty()
            ? nullptr
            : preheader->get_instructions().back().get());
    if (preheader_jump == nullptr || preheader_jump->get_target_block() != header) {
        return false;
    }

    std::vector<std::unique_ptr<CoreIrInstruction>> cloned_instructions;
    CoreIrValue *cloned_condition = nullptr;
    if (!build_cloned_header_condition(header_instructions,
                                       header_branch->get_condition(), {},
                                       cloned_instructions, cloned_condition)) {
        return false;
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

bool rotate_phi_header_loop(
    const CoreIrLoopInfo &loop, CoreIrBasicBlock *preheader,
    CoreIrBasicBlock *header, CoreIrBasicBlock *latch,
    CoreIrCondJumpInst *header_branch, CoreIrBasicBlock *inside_successor,
    CoreIrBasicBlock *outside_successor,
    const std::vector<HeaderPhiInfo> &header_phis,
    const std::vector<CoreIrInstruction *> &header_instructions) {
    if (header_phis.empty() || loop.get_exit_blocks().size() != 1 ||
        *loop.get_exit_blocks().begin() != outside_successor) {
        return false;
    }

    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        preheader->get_instructions().empty()
            ? nullptr
            : preheader->get_instructions().back().get());
    auto *latch_jump = dynamic_cast<CoreIrJumpInst *>(
        latch->get_instructions().empty() ? nullptr
                                          : latch->get_instructions().back().get());
    if (preheader_jump == nullptr || latch_jump == nullptr ||
        preheader_jump->get_target_block() != header ||
        latch_jump->get_target_block() != header) {
        return false;
    }

    std::unordered_map<CoreIrPhiInst *, HeaderPhiInfo *> header_phi_map;
    std::unordered_map<CoreIrValue *, CoreIrValue *> preheader_value_map;
    std::unordered_map<CoreIrValue *, CoreIrValue *> latch_value_map;
    for (const HeaderPhiInfo &info : header_phis) {
        if (info.phi == nullptr) {
            return false;
        }
        header_phi_map.emplace(info.phi, const_cast<HeaderPhiInfo *>(&info));
        preheader_value_map.emplace(info.phi, info.preheader_value);
        latch_value_map.emplace(info.phi, info.latch_value);
    }

    if (!rewrite_outside_successor_phis(outside_successor, header, preheader, latch,
                                        header_phi_map) ||
        !repair_phi_external_uses(loop, outside_successor, header_phis)) {
        return false;
    }

    std::vector<std::unique_ptr<CoreIrInstruction>> preheader_clones;
    CoreIrValue *preheader_condition = nullptr;
    if (!build_cloned_header_condition(header_instructions,
                                       header_branch->get_condition(),
                                       preheader_value_map, preheader_clones,
                                       preheader_condition)) {
        return false;
    }

    std::vector<std::unique_ptr<CoreIrInstruction>> latch_clones;
    CoreIrValue *latch_condition = nullptr;
    if (!build_cloned_header_condition(header_instructions,
                                       header_branch->get_condition(),
                                       latch_value_map, latch_clones,
                                       latch_condition)) {
        return false;
    }

    const bool inside_is_true =
        header_branch->get_true_block() == inside_successor;
    const CoreIrType *terminator_type = header_branch->get_type();
    const auto terminator_source_span = header_branch->get_source_span();
    CoreIrBasicBlock *continue_true_block = inside_is_true ? header : outside_successor;
    CoreIrBasicBlock *continue_false_block = inside_is_true ? outside_successor : header;

    auto &preheader_instructions = preheader->get_instructions();
    preheader_instructions.pop_back();
    for (auto &clone : preheader_clones) {
        clone->set_parent(preheader);
        preheader_instructions.push_back(std::move(clone));
    }
    auto preheader_branch = std::make_unique<CoreIrCondJumpInst>(
        terminator_type, preheader_condition, continue_true_block,
        continue_false_block);
    preheader_branch->set_source_span(terminator_source_span);
    preheader_branch->set_parent(preheader);
    preheader_instructions.push_back(std::move(preheader_branch));

    auto &latch_instructions = latch->get_instructions();
    latch_instructions.pop_back();
    for (auto &clone : latch_clones) {
        clone->set_parent(latch);
        latch_instructions.push_back(std::move(clone));
    }
    auto latch_branch = std::make_unique<CoreIrCondJumpInst>(
        terminator_type, latch_condition, continue_true_block,
        continue_false_block);
    latch_branch->set_source_span(terminator_source_span);
    latch_branch->set_parent(latch);
    latch_instructions.push_back(std::move(latch_branch));

    std::vector<CoreIrInstruction *> to_remove = header_instructions;
    for (CoreIrInstruction *instruction : to_remove) {
        if (instruction == nullptr) {
            continue;
        }
        if (!erase_instruction(*header, instruction)) {
            return false;
        }
    }

    auto &header_insts = header->get_instructions();
    if (header_insts.empty()) {
        return false;
    }
    header_insts.pop_back();
    auto header_jump =
        std::make_unique<CoreIrJumpInst>(terminator_type, inside_successor);
    header_jump->set_source_span(terminator_source_span);
    header_jump->set_parent(header);
    header_insts.push_back(std::move(header_jump));
    return true;
}

bool rotate_loop(const CoreIrLoopInfo &loop) {
    CoreIrBasicBlock *preheader = loop.get_preheader();
    CoreIrBasicBlock *header = loop.get_header();
    if (preheader == nullptr || header == nullptr ||
        loop.get_latches().size() != 1 || header->get_instructions().empty()) {
        return false;
    }

    CoreIrBasicBlock *latch = *loop.get_latches().begin();
    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        header->get_instructions().back().get());
    if (latch == nullptr || header_branch == nullptr) {
        return false;
    }

    std::vector<HeaderPhiInfo> header_phis;
    std::vector<CoreIrInstruction *> header_instructions;
    if (!collect_header_shape(*header, preheader, latch, header_phis,
                              header_instructions)) {
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

    if (inside_successor == nullptr || outside_successor == nullptr) {
        return false;
    }

    if (header_phis.empty()) {
        if (block_has_phi(*inside_successor) || block_has_phi(*outside_successor)) {
            return false;
        }
        return rotate_phi_free_loop(preheader, header, header_branch,
                                    inside_successor, outside_successor,
                                    header_instructions);
    }

    return rotate_phi_header_loop(loop, preheader, header, latch, header_branch,
                                  inside_successor, outside_successor,
                                  header_phis, header_instructions);
}

} // namespace

PassKind CoreIrLoopRotatePass::Kind() const {
    return PassKind::CoreIrLoopRotate;
}

const char *CoreIrLoopRotatePass::Name() const {
    return "CoreIrLoopRotatePass";
}

PassResult CoreIrLoopRotatePass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager =
        build_result->get_analysis_manager();
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
            function_changed =
                rotate_loop(*loop) || function_changed;
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
