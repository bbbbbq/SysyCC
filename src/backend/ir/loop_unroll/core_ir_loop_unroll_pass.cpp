#include "backend/ir/loop_unroll/core_ir_loop_unroll_pass.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/induction_var_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/analysis/scalar_evolution_lite_analysis.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"

namespace sysycc {

namespace {

using sysycc::detail::erase_instruction;
using sysycc::detail::insert_instruction_before;

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

bool instruction_is_unrollable_body_instruction(const CoreIrInstruction &instruction) {
    if (instruction.get_is_terminator() ||
        instruction.get_opcode() == CoreIrOpcode::Phi ||
        instruction.get_opcode() == CoreIrOpcode::Call) {
        return false;
    }
    switch (instruction.get_opcode()) {
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
    case CoreIrOpcode::GetElementPtr:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Binary:
    case CoreIrOpcode::Unary:
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Cast:
        return true;
    default:
        return false;
    }
}

CoreIrInstruction *clone_instruction(
    const CoreIrInstruction &instruction, CoreIrContext &core_ir_context,
    const std::unordered_map<const CoreIrValue *, CoreIrValue *> &value_map) {
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
    case CoreIrOpcode::AddressOfFunction: {
        const auto &address =
            static_cast<const CoreIrAddressOfFunctionInst &>(instruction);
        auto clone = std::make_unique<CoreIrAddressOfFunctionInst>(
            address.get_type(), address.get_name(), address.get_function());
        clone->set_source_span(address.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::AddressOfGlobal: {
        const auto &address =
            static_cast<const CoreIrAddressOfGlobalInst &>(instruction);
        auto clone = std::make_unique<CoreIrAddressOfGlobalInst>(
            address.get_type(), address.get_name(), address.get_global());
        clone->set_source_span(address.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::AddressOfStackSlot: {
        const auto &address =
            static_cast<const CoreIrAddressOfStackSlotInst &>(instruction);
        auto clone = std::make_unique<CoreIrAddressOfStackSlotInst>(
            address.get_type(), address.get_name(), address.get_stack_slot());
        clone->set_source_span(address.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::GetElementPtr: {
        const auto &gep = static_cast<const CoreIrGetElementPtrInst &>(instruction);
        std::vector<CoreIrValue *> indices;
        indices.reserve(gep.get_index_count());
        for (std::size_t index = 0; index < gep.get_index_count(); ++index) {
            indices.push_back(remap(gep.get_index(index)));
        }
        auto clone = std::make_unique<CoreIrGetElementPtrInst>(
            gep.get_type(), gep.get_name(), remap(gep.get_base()),
            std::move(indices));
        clone->set_source_span(gep.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Load: {
        const auto &load = static_cast<const CoreIrLoadInst &>(instruction);
        std::unique_ptr<CoreIrLoadInst> clone;
        if (load.get_stack_slot() != nullptr) {
            clone = std::make_unique<CoreIrLoadInst>(
                load.get_type(), load.get_name(), load.get_stack_slot());
        } else {
            clone = std::make_unique<CoreIrLoadInst>(
                load.get_type(), load.get_name(), remap(load.get_address()));
        }
        clone->set_source_span(load.get_source_span());
        return clone.release();
    }
    case CoreIrOpcode::Store: {
        const auto &store = static_cast<const CoreIrStoreInst &>(instruction);
        std::unique_ptr<CoreIrStoreInst> clone;
        if (store.get_stack_slot() != nullptr) {
            clone = std::make_unique<CoreIrStoreInst>(
                store.get_type(), remap(store.get_value()), store.get_stack_slot());
        } else {
            clone = std::make_unique<CoreIrStoreInst>(
                store.get_type(), remap(store.get_value()),
                remap(store.get_address()));
        }
        clone->set_source_span(store.get_source_span());
        return clone.release();
    }
    default:
        return nullptr;
    }
}

std::size_t count_unrollable_body_instructions(const CoreIrBasicBlock &body) {
    std::size_t count = 0;
    for (const auto &instruction_ptr : body.get_instructions()) {
        const CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr || instruction->get_is_terminator()) {
            continue;
        }
        ++count;
    }
    return count;
}

std::size_t count_unrollable_body_instructions(const std::vector<CoreIrBasicBlock *> &blocks) {
    std::size_t count = 0;
    for (CoreIrBasicBlock *block : blocks) {
        if (block != nullptr) {
            count += count_unrollable_body_instructions(*block);
        }
    }
    return count;
}

struct HeaderPhiInfo {
    CoreIrPhiInst *phi = nullptr;
    CoreIrValue *initial_value = nullptr;
    CoreIrValue *latch_value = nullptr;
};

std::optional<std::vector<HeaderPhiInfo>> collect_header_phi_infos(
    CoreIrBasicBlock &header, CoreIrBasicBlock *preheader, CoreIrBasicBlock *latch) {
    if (preheader == nullptr || latch == nullptr) {
        return std::nullopt;
    }

    std::vector<HeaderPhiInfo> phi_infos;
    for (const auto &instruction_ptr : header.get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if (phi->get_incoming_count() != 2) {
            return std::nullopt;
        }

        std::size_t preheader_index =
            phi->get_incoming_block(0) == preheader
                ? 0
                : phi->get_incoming_block(1) == preheader
                      ? 1
                      : phi->get_incoming_count();
        if (preheader_index >= phi->get_incoming_count()) {
            return std::nullopt;
        }

        const std::size_t latch_index = preheader_index == 0 ? 1 : 0;
        if (phi->get_incoming_block(latch_index) != latch) {
            return std::nullopt;
        }

        phi_infos.push_back(
            HeaderPhiInfo{phi, phi->get_incoming_value(preheader_index),
                          phi->get_incoming_value(latch_index)});
    }

    return phi_infos.empty() ? std::nullopt
                             : std::optional<std::vector<HeaderPhiInfo>>(
                                   std::move(phi_infos));
}

bool instruction_has_only_unrollable_uses(
    const CoreIrInstruction &instruction, const CoreIrLoopInfo &loop,
    const std::unordered_set<CoreIrPhiInst *> &header_phis) {
    for (const CoreIrUse &use : instruction.get_uses()) {
        CoreIrInstruction *user = use.get_user();
        if (user == nullptr) {
            continue;
        }
        if (loop_contains_block(loop, get_use_location_block(use))) {
            continue;
        }
        auto *phi = dynamic_cast<CoreIrPhiInst *>(user);
        if (phi != nullptr && header_phis.find(phi) != header_phis.end() &&
            phi->get_incoming_block(use.get_operand_index()) == instruction.get_parent()) {
            continue;
        }
        return false;
    }
    return true;
}

void replace_header_phi_external_uses(
    const std::vector<HeaderPhiInfo> &phi_infos, const CoreIrLoopInfo &loop,
    const std::unordered_map<CoreIrPhiInst *, CoreIrValue *> &final_values) {
    for (const HeaderPhiInfo &phi_info : phi_infos) {
        auto it = final_values.find(phi_info.phi);
        if (phi_info.phi == nullptr || it == final_values.end()) {
            continue;
        }
        CoreIrValue *replacement = it->second;
        const std::vector<CoreIrUse> uses = phi_info.phi->get_uses();
        for (const CoreIrUse &use : uses) {
            CoreIrInstruction *user = use.get_user();
            if (user == nullptr) {
                continue;
            }
            if (loop_contains_block(loop, get_use_location_block(use))) {
                continue;
            }
            user->set_operand(use.get_operand_index(), replacement);
        }
    }
}

bool rewrite_exit_lcssa_phis(CoreIrBasicBlock &exit_block, const CoreIrLoopInfo &loop,
                             const std::unordered_map<CoreIrPhiInst *, CoreIrValue *> &final_values) {
    std::vector<std::pair<CoreIrPhiInst *, CoreIrValue *>> rewrites;
    for (const auto &instruction_ptr : exit_block.get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }

        CoreIrValue *replacement = nullptr;
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            CoreIrBasicBlock *incoming_block = phi->get_incoming_block(index);
            if (!loop_contains_block(loop, incoming_block)) {
                return false;
            }

            CoreIrValue *incoming_value = phi->get_incoming_value(index);
            if (auto *incoming_phi = dynamic_cast<CoreIrPhiInst *>(incoming_value);
                incoming_phi != nullptr) {
                auto it = final_values.find(incoming_phi);
                if (it == final_values.end()) {
                    return false;
                }
                incoming_value = it->second;
            } else if (auto *incoming_instruction =
                           dynamic_cast<CoreIrInstruction *>(incoming_value);
                       incoming_instruction != nullptr &&
                       loop_contains_block(loop, incoming_instruction->get_parent())) {
                return false;
            }

            if (replacement == nullptr) {
                replacement = incoming_value;
                continue;
            }
            if (replacement != incoming_value) {
                return false;
            }
        }

        if (replacement == nullptr) {
            return false;
        }
        rewrites.emplace_back(phi, replacement);
    }

    for (const auto &[phi, replacement] : rewrites) {
        phi->replace_all_uses_with(replacement);
        erase_instruction(exit_block, phi);
    }
    return true;
}

bool fully_unroll_small_loop(CoreIrFunction &,
                             const CoreIrLoopInfo &loop,
                             const CoreIrCanonicalInductionVarInfo &iv,
                             const CoreIrScalarEvolutionLiteAnalysisResult &scev,
                             CoreIrContext &core_ir_context) {
    std::optional<std::uint64_t> trip_count = scev.get_constant_trip_count(loop);
    if (!trip_count.has_value() || *trip_count > 128 || loop.get_latches().size() != 1 ||
        loop.get_blocks().size() < 2 || loop.get_blocks().size() > 3 ||
        loop.get_preheader() == nullptr ||
        iv.phi == nullptr || iv.latch == nullptr) {
        return false;
    }

    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *latch = iv.latch;
    CoreIrBasicBlock *body = nullptr;
    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block != nullptr && block != header && block != latch) {
            body = block;
        }
    }
    if (header == nullptr || latch == nullptr) {
        return false;
    }
    if (body == nullptr) {
        body = latch;
    }
    if (body->get_instructions().empty() || latch->get_instructions().empty()) {
        return false;
    }
    const auto phi_infos = collect_header_phi_infos(*header, loop.get_preheader(), latch);
    if (!phi_infos.has_value()) {
        return false;
    }
    std::unordered_set<CoreIrPhiInst *> header_phi_set;
    for (const HeaderPhiInfo &phi_info : *phi_infos) {
        if (phi_info.phi != nullptr) {
            header_phi_set.insert(phi_info.phi);
        }
    }

    auto *header_branch = dynamic_cast<CoreIrCondJumpInst *>(
        header->get_instructions().back().get());
    auto *body_jump = dynamic_cast<CoreIrJumpInst *>(
        body->get_instructions().back().get());
    auto *latch_jump = dynamic_cast<CoreIrJumpInst *>(
        latch->get_instructions().back().get());
    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        loop.get_preheader()->get_instructions().back().get());
    if (header_branch == nullptr || body_jump == nullptr || latch_jump == nullptr ||
        preheader_jump == nullptr || preheader_jump->get_target_block() != header) {
        return false;
    }
    if (body == latch) {
        if (body_jump->get_target_block() != header) {
            return false;
        }
    } else if (body_jump->get_target_block() != latch ||
               latch_jump->get_target_block() != header) {
        return false;
    }

    CoreIrBasicBlock *exit_block = nullptr;
    if (!loop_contains_block(loop, header_branch->get_true_block())) {
        exit_block = header_branch->get_true_block();
    } else if (!loop_contains_block(loop, header_branch->get_false_block())) {
        exit_block = header_branch->get_false_block();
    }
    if (exit_block == nullptr) {
        return false;
    }

    const std::size_t body_instruction_count =
        count_unrollable_body_instructions({body, body == latch ? nullptr : latch});
    if (body_instruction_count == 0 ||
        body_instruction_count * *trip_count > 768) {
        return false;
    }

    for (const auto &instruction_ptr : header->get_instructions()) {
        CoreIrInstruction *instruction = instruction_ptr.get();
        if (instruction == nullptr ||
            header_phi_set.find(dynamic_cast<CoreIrPhiInst *>(instruction)) !=
                header_phi_set.end() ||
            instruction == iv.exit_compare || instruction == iv.exit_branch) {
            continue;
        }
        return false;
    }

    for (CoreIrBasicBlock *block : {body, body == latch ? nullptr : latch}) {
        if (block == nullptr) {
            continue;
        }
        if (block_has_phi(*block)) {
            return false;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr || instruction->get_is_terminator()) {
                continue;
            }
            if (!instruction_is_unrollable_body_instruction(*instruction)) {
                return false;
            }
            if (!instruction_has_only_unrollable_uses(*instruction, loop,
                                                      header_phi_set)) {
                return false;
            }
        }
    }

    std::unordered_map<CoreIrPhiInst *, CoreIrValue *> current_phi_values;
    for (const HeaderPhiInfo &phi_info : *phi_infos) {
        current_phi_values.emplace(phi_info.phi, phi_info.initial_value);
    }
    for (std::uint64_t iteration = 0; iteration < *trip_count; ++iteration) {
        std::unordered_map<const CoreIrValue *, CoreIrValue *> value_map;
        for (const HeaderPhiInfo &phi_info : *phi_infos) {
            value_map.emplace(phi_info.phi, current_phi_values.at(phi_info.phi));
        }

        for (CoreIrBasicBlock *block : {body, body == latch ? nullptr : latch}) {
            if (block == nullptr) {
                continue;
            }
            for (const auto &instruction_ptr : block->get_instructions()) {
                CoreIrInstruction *instruction = instruction_ptr.get();
                if (instruction == nullptr || instruction->get_is_terminator()) {
                    continue;
                }
                CoreIrInstruction *clone =
                    clone_instruction(*instruction, core_ir_context, value_map);
                if (clone == nullptr) {
                    return false;
                }
                value_map.emplace(instruction, clone);
                insert_instruction_before(
                    *loop.get_preheader(),
                    loop.get_preheader()->get_instructions().back().get(),
                    std::unique_ptr<CoreIrInstruction>(clone));
            }
        }

        for (const HeaderPhiInfo &phi_info : *phi_infos) {
            auto next_it = value_map.find(phi_info.latch_value);
            current_phi_values[phi_info.phi] =
                next_it == value_map.end() ? phi_info.latch_value : next_it->second;
        }
    }

    replace_header_phi_external_uses(*phi_infos, loop, current_phi_values);
    if (!rewrite_exit_lcssa_phis(*exit_block, loop, current_phi_values)) {
        return false;
    }
    preheader_jump->set_target_block(exit_block);
    for (const HeaderPhiInfo &phi_info : *phi_infos) {
        phi_info.phi->remove_incoming_block(loop.get_preheader());
    }
    return true;
}

} // namespace

PassKind CoreIrLoopUnrollPass::Kind() const { return PassKind::CoreIrLoopUnroll; }

const char *CoreIrLoopUnrollPass::Name() const { return "CoreIrLoopUnrollPass"; }

PassResult CoreIrLoopUnrollPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    CoreIrContext *core_ir_context = build_result->get_context();
    if (analysis_manager == nullptr || core_ir_context == nullptr) {
        return PassResult::Failure("missing core ir loop unroll dependencies");
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
        const CoreIrInductionVarAnalysisResult &induction_vars =
            analysis_manager->get_or_compute<CoreIrInductionVarAnalysis>(*function);
        const CoreIrScalarEvolutionLiteAnalysisResult &scev =
            analysis_manager->get_or_compute<CoreIrScalarEvolutionLiteAnalysis>(*function);

        bool function_changed = false;
        for (const auto &loop_ptr : loop_info.get_loops()) {
            if (loop_ptr == nullptr) {
                continue;
            }
            const CoreIrCanonicalInductionVarInfo *iv =
                induction_vars.get_canonical_induction_var(*loop_ptr);
            if (iv == nullptr) {
                continue;
            }
            function_changed =
                fully_unroll_small_loop(*function, *loop_ptr, *iv, scev,
                                        *core_ir_context) ||
                function_changed;
        }
        if (function_changed) {
            effects.changed_functions.insert(function.get());
            effects.cfg_changed_functions.insert(function.get());
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
