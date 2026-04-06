#include "backend/ir/loop_idiom/core_ir_loop_idiom_pass.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
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

struct AdditiveReductionInfo {
    CoreIrPhiInst *phi = nullptr;
    CoreIrInstruction *update = nullptr;
    CoreIrValue *initial_value = nullptr;
    CoreIrValue *increment_value = nullptr;
    bool subtract = false;
};

struct BitwiseReductionInfo {
    CoreIrPhiInst *phi = nullptr;
    CoreIrInstruction *update = nullptr;
    CoreIrValue *initial_value = nullptr;
    CoreIrValue *reduction_value = nullptr;
    CoreIrBinaryOpcode opcode = CoreIrBinaryOpcode::Or;
};

bool instruction_is_allowed_loop_idiom_instruction(
    const CoreIrInstruction &instruction, const CoreIrCanonicalInductionVarInfo &iv,
    const AdditiveReductionInfo &reduction) {
    if (&instruction == iv.phi || &instruction == iv.latch_update ||
        &instruction == iv.exit_compare || &instruction == reduction.phi ||
        &instruction == reduction.update) {
        return true;
    }
    if (instruction.get_opcode() == CoreIrOpcode::Phi ||
        instruction.get_opcode() == CoreIrOpcode::Jump ||
        instruction.get_opcode() == CoreIrOpcode::CondJump) {
        return true;
    }
    return false;
}

bool instruction_is_allowed_loop_idiom_instruction(
    const CoreIrInstruction &instruction, const CoreIrCanonicalInductionVarInfo &iv,
    const BitwiseReductionInfo &reduction) {
    if (&instruction == iv.phi || &instruction == iv.latch_update ||
        &instruction == iv.exit_compare || &instruction == reduction.phi ||
        &instruction == reduction.update) {
        return true;
    }
    if (instruction.get_opcode() == CoreIrOpcode::Phi ||
        instruction.get_opcode() == CoreIrOpcode::Jump ||
        instruction.get_opcode() == CoreIrOpcode::CondJump) {
        return true;
    }
    return false;
}

std::optional<AdditiveReductionInfo>
find_additive_reduction(const CoreIrLoopInfo &loop,
                        const CoreIrCanonicalInductionVarInfo &iv) {
    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *preheader = loop.get_preheader();
    if (header == nullptr || preheader == nullptr || iv.latch == nullptr) {
        return std::nullopt;
    }

    for (const auto &instruction_ptr : header->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if (phi == iv.phi || phi->get_incoming_count() != 2) {
            continue;
        }

        std::size_t preheader_index =
            phi->get_incoming_block(0) == preheader
                ? 0
                : phi->get_incoming_block(1) == preheader
                      ? 1
                      : phi->get_incoming_count();
        if (preheader_index >= phi->get_incoming_count()) {
            continue;
        }
        const std::size_t latch_index = preheader_index == 0 ? 1 : 0;
        if (phi->get_incoming_block(latch_index) != iv.latch) {
            continue;
        }

        auto *binary =
            dynamic_cast<CoreIrBinaryInst *>(phi->get_incoming_value(latch_index));
        if (binary == nullptr) {
            continue;
        }

        AdditiveReductionInfo info;
        info.phi = phi;
        info.update = binary;
        info.initial_value = phi->get_incoming_value(preheader_index);

        switch (binary->get_binary_opcode()) {
        case CoreIrBinaryOpcode::Add:
            if (binary->get_lhs() == phi &&
                value_is_loop_invariant(binary->get_rhs(), loop)) {
                info.increment_value = binary->get_rhs();
            } else if (binary->get_rhs() == phi &&
                       value_is_loop_invariant(binary->get_lhs(), loop)) {
                info.increment_value = binary->get_lhs();
            } else {
                continue;
            }
            break;
        case CoreIrBinaryOpcode::Sub:
            if (binary->get_lhs() == phi &&
                value_is_loop_invariant(binary->get_rhs(), loop)) {
                info.increment_value = binary->get_rhs();
                info.subtract = true;
            } else {
                continue;
            }
            break;
        default:
            continue;
        }
        return info;
    }

    return std::nullopt;
}

std::optional<BitwiseReductionInfo>
find_bitwise_reduction(const CoreIrLoopInfo &loop,
                       const CoreIrCanonicalInductionVarInfo &iv) {
    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *preheader = loop.get_preheader();
    if (header == nullptr || preheader == nullptr || iv.latch == nullptr) {
        return std::nullopt;
    }

    for (const auto &instruction_ptr : header->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        if (phi == iv.phi || phi->get_incoming_count() != 2) {
            continue;
        }

        std::size_t preheader_index =
            phi->get_incoming_block(0) == preheader
                ? 0
                : phi->get_incoming_block(1) == preheader
                      ? 1
                      : phi->get_incoming_count();
        if (preheader_index >= phi->get_incoming_count()) {
            continue;
        }
        const std::size_t latch_index = preheader_index == 0 ? 1 : 0;
        if (phi->get_incoming_block(latch_index) != iv.latch) {
            continue;
        }

        auto *binary =
            dynamic_cast<CoreIrBinaryInst *>(phi->get_incoming_value(latch_index));
        if (binary == nullptr) {
            continue;
        }
        const CoreIrBinaryOpcode opcode = binary->get_binary_opcode();
        if (opcode != CoreIrBinaryOpcode::And &&
            opcode != CoreIrBinaryOpcode::Or &&
            opcode != CoreIrBinaryOpcode::Xor) {
            continue;
        }

        BitwiseReductionInfo info;
        info.phi = phi;
        info.update = binary;
        info.initial_value = phi->get_incoming_value(preheader_index);
        info.opcode = opcode;

        if (binary->get_lhs() == phi &&
            value_is_loop_invariant(binary->get_rhs(), loop)) {
            info.reduction_value = binary->get_rhs();
        } else if (binary->get_rhs() == phi &&
                   value_is_loop_invariant(binary->get_lhs(), loop)) {
            info.reduction_value = binary->get_lhs();
        } else {
            continue;
        }
        return info;
    }

    return std::nullopt;
}

CoreIrPhiInst *find_lcssa_phi_for_value(CoreIrBasicBlock &exit_block,
                                        CoreIrInstruction &value) {
    for (const auto &instruction_ptr : exit_block.get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction_ptr.get());
        if (phi == nullptr) {
            break;
        }
        for (std::size_t index = 0; index < phi->get_incoming_count(); ++index) {
            if (phi->get_incoming_value(index) == &value) {
                return phi;
            }
        }
    }
    return nullptr;
}

CoreIrInstruction *insert_binary_before_terminator(CoreIrBasicBlock &block,
                                                   CoreIrBinaryOpcode opcode,
                                                   const CoreIrType *type,
                                                   std::string name,
                                                   CoreIrValue *lhs,
                                                   CoreIrValue *rhs) {
    auto instruction =
        std::make_unique<CoreIrBinaryInst>(opcode, type, std::move(name), lhs, rhs);
    return insert_instruction_before(block, block.get_instructions().back().get(),
                                     std::move(instruction));
}

bool fold_counted_additive_reduction(CoreIrFunction &function,
                                     const CoreIrLoopInfo &loop,
                                     const CoreIrCanonicalInductionVarInfo &iv,
                                     const AdditiveReductionInfo &reduction,
                                     const CoreIrScalarEvolutionLiteAnalysisResult &scev,
                                     CoreIrContext &core_ir_context) {
    std::optional<std::uint64_t> trip_count = scev.get_constant_trip_count(loop);
    if (!trip_count.has_value() || loop.get_exit_blocks().size() != 1 ||
        loop.get_preheader() == nullptr) {
        return false;
    }

    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr ||
                instruction_is_allowed_loop_idiom_instruction(*instruction, iv,
                                                              reduction)) {
                continue;
            }
            return false;
        }
    }

    CoreIrBasicBlock *exit_block = *loop.get_exit_blocks().begin();
    CoreIrPhiInst *exit_phi = exit_block == nullptr
                                  ? nullptr
                                  : find_lcssa_phi_for_value(*exit_block, *reduction.phi);
    if (exit_phi == nullptr) {
        return false;
    }

    auto *integer_type =
        dynamic_cast<const CoreIrIntegerType *>(reduction.phi->get_type());
    if (integer_type == nullptr) {
        return false;
    }

    CoreIrValue *trip_count_value = core_ir_context.create_constant<CoreIrConstantInt>(
        integer_type, *trip_count);
    CoreIrInstruction *mul = insert_binary_before_terminator(
        *loop.get_preheader(), CoreIrBinaryOpcode::Mul, reduction.phi->get_type(),
        reduction.phi->get_name() + ".idiom.mul", reduction.increment_value,
        trip_count_value);
    if (mul == nullptr) {
        return false;
    }
    CoreIrInstruction *final_value = insert_binary_before_terminator(
        *loop.get_preheader(),
        reduction.subtract ? CoreIrBinaryOpcode::Sub : CoreIrBinaryOpcode::Add,
        reduction.phi->get_type(), reduction.phi->get_name() + ".idiom.final",
        reduction.initial_value, mul);
    if (final_value == nullptr) {
        return false;
    }

    exit_phi->replace_all_uses_with(final_value);
    erase_instruction(*exit_block, exit_phi);

    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        loop.get_preheader()->get_instructions().back().get());
    if (preheader_jump == nullptr) {
        return false;
    }
    preheader_jump->set_target_block(exit_block);
    return true;
}

bool fold_counted_bitwise_reduction(CoreIrFunction &function,
                                    const CoreIrLoopInfo &loop,
                                    const CoreIrCanonicalInductionVarInfo &iv,
                                    const BitwiseReductionInfo &reduction,
                                    const CoreIrScalarEvolutionLiteAnalysisResult &scev) {
    std::optional<std::uint64_t> trip_count = scev.get_constant_trip_count(loop);
    if (!trip_count.has_value() || loop.get_exit_blocks().size() != 1 ||
        loop.get_preheader() == nullptr) {
        return false;
    }

    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            CoreIrInstruction *instruction = instruction_ptr.get();
            if (instruction == nullptr ||
                instruction_is_allowed_loop_idiom_instruction(*instruction, iv,
                                                              reduction)) {
                continue;
            }
            return false;
        }
    }

    CoreIrBasicBlock *exit_block = *loop.get_exit_blocks().begin();
    CoreIrPhiInst *exit_phi = exit_block == nullptr
                                  ? nullptr
                                  : find_lcssa_phi_for_value(*exit_block, *reduction.phi);
    if (exit_phi == nullptr) {
        return false;
    }

    CoreIrValue *final_value = reduction.initial_value;
    if (*trip_count > 0) {
        switch (reduction.opcode) {
        case CoreIrBinaryOpcode::And:
        case CoreIrBinaryOpcode::Or:
            final_value = insert_binary_before_terminator(
                *loop.get_preheader(), reduction.opcode, reduction.phi->get_type(),
                reduction.phi->get_name() + ".idiom.final",
                reduction.initial_value, reduction.reduction_value);
            break;
        case CoreIrBinaryOpcode::Xor:
            if ((*trip_count & 1U) != 0U) {
                final_value = insert_binary_before_terminator(
                    *loop.get_preheader(), reduction.opcode, reduction.phi->get_type(),
                    reduction.phi->get_name() + ".idiom.final",
                    reduction.initial_value, reduction.reduction_value);
            }
            break;
        default:
            return false;
        }
    }
    if (final_value == nullptr) {
        return false;
    }

    exit_phi->replace_all_uses_with(final_value);
    erase_instruction(*exit_block, exit_phi);

    auto *preheader_jump = dynamic_cast<CoreIrJumpInst *>(
        loop.get_preheader()->get_instructions().back().get());
    if (preheader_jump == nullptr) {
        return false;
    }
    preheader_jump->set_target_block(exit_block);
    return true;
}

} // namespace

PassKind CoreIrLoopIdiomPass::Kind() const { return PassKind::CoreIrLoopIdiom; }

const char *CoreIrLoopIdiomPass::Name() const { return "CoreIrLoopIdiomPass"; }

PassResult CoreIrLoopIdiomPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module = build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager = build_result->get_analysis_manager();
    CoreIrContext *core_ir_context = build_result->get_context();
    if (analysis_manager == nullptr || core_ir_context == nullptr) {
        return PassResult::Failure("missing core ir loop idiom dependencies");
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
            const std::optional<AdditiveReductionInfo> reduction =
                find_additive_reduction(*loop_ptr, *iv);
            if (reduction.has_value()) {
                function_changed =
                    fold_counted_additive_reduction(*function, *loop_ptr, *iv,
                                                   *reduction, scev,
                                                   *core_ir_context) ||
                    function_changed;
                continue;
            }
            const std::optional<BitwiseReductionInfo> bitwise_reduction =
                find_bitwise_reduction(*loop_ptr, *iv);
            if (!bitwise_reduction.has_value()) {
                continue;
            }
            function_changed =
                fold_counted_bitwise_reduction(*function, *loop_ptr, *iv,
                                               *bitwise_reduction, scev) ||
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
