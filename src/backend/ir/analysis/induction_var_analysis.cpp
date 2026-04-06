#include "backend/ir/analysis/induction_var_analysis.hpp"

#include <cstdint>
#include <optional>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

std::int64_t sign_extend_integer_constant(const CoreIrConstantInt &constant) {
    const auto *integer_type =
        dynamic_cast<const CoreIrIntegerType *>(constant.get_type());
    if (integer_type == nullptr) {
        return static_cast<std::int64_t>(constant.get_value());
    }

    const std::size_t bit_width = integer_type->get_bit_width();
    const std::uint64_t raw = constant.get_value();
    if (bit_width == 0 || bit_width >= 64) {
        return static_cast<std::int64_t>(raw);
    }

    const std::uint64_t sign_bit = std::uint64_t{1} << (bit_width - 1);
    if ((raw & sign_bit) == 0) {
        return static_cast<std::int64_t>(raw);
    }
    const std::uint64_t mask = (~std::uint64_t{0}) << bit_width;
    return static_cast<std::int64_t>(raw | mask);
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

CoreIrComparePredicate swap_compare_predicate(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::SignedLess:
        return CoreIrComparePredicate::SignedGreater;
    case CoreIrComparePredicate::SignedLessEqual:
        return CoreIrComparePredicate::SignedGreaterEqual;
    case CoreIrComparePredicate::SignedGreater:
        return CoreIrComparePredicate::SignedLess;
    case CoreIrComparePredicate::SignedGreaterEqual:
        return CoreIrComparePredicate::SignedLessEqual;
    case CoreIrComparePredicate::UnsignedLess:
        return CoreIrComparePredicate::UnsignedGreater;
    case CoreIrComparePredicate::UnsignedLessEqual:
        return CoreIrComparePredicate::UnsignedGreaterEqual;
    case CoreIrComparePredicate::UnsignedGreater:
        return CoreIrComparePredicate::UnsignedLess;
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return CoreIrComparePredicate::UnsignedLessEqual;
    case CoreIrComparePredicate::Equal:
    case CoreIrComparePredicate::NotEqual:
        return predicate;
    }
    return predicate;
}

bool get_constant_step_from_latch_value(CoreIrPhiInst &phi, CoreIrValue *value,
                                        std::int64_t &step,
                                        CoreIrInstruction *&update_instruction) {
    auto *binary = dynamic_cast<CoreIrBinaryInst *>(value);
    if (binary == nullptr) {
        return false;
    }

    auto *lhs_constant = dynamic_cast<CoreIrConstantInt *>(binary->get_lhs());
    auto *rhs_constant = dynamic_cast<CoreIrConstantInt *>(binary->get_rhs());

    switch (binary->get_binary_opcode()) {
    case CoreIrBinaryOpcode::Add:
        if (binary->get_lhs() == &phi && rhs_constant != nullptr) {
            step = sign_extend_integer_constant(*rhs_constant);
            update_instruction = binary;
            return step != 0;
        }
        if (binary->get_rhs() == &phi && lhs_constant != nullptr) {
            step = sign_extend_integer_constant(*lhs_constant);
            update_instruction = binary;
            return step != 0;
        }
        return false;
    case CoreIrBinaryOpcode::Sub:
        if (binary->get_lhs() == &phi && rhs_constant != nullptr) {
            step = -sign_extend_integer_constant(*rhs_constant);
            update_instruction = binary;
            return step != 0;
        }
        return false;
    default:
        return false;
    }
}

bool header_has_single_inside_and_outside_successor(
    const CoreIrLoopInfo &loop, const CoreIrCfgAnalysisResult &cfg,
    CoreIrBasicBlock *header, CoreIrBasicBlock *&inside_successor,
    CoreIrBasicBlock *&outside_successor, bool &inside_is_true_successor) {
    if (header == nullptr || header->get_instructions().empty()) {
        return false;
    }

    auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
        header->get_instructions().back().get());
    if (branch == nullptr) {
        return false;
    }

    CoreIrBasicBlock *true_block = branch->get_true_block();
    CoreIrBasicBlock *false_block = branch->get_false_block();
    const bool true_inside = loop_contains_block(loop, true_block);
    const bool false_inside = loop_contains_block(loop, false_block);
    if (true_inside == false_inside) {
        return false;
    }

    inside_successor = true_inside ? true_block : false_block;
    outside_successor = true_inside ? false_block : true_block;
    inside_is_true_successor = true_inside;
    return cfg.is_reachable(inside_successor) && cfg.is_reachable(outside_successor);
}

std::optional<CoreIrCanonicalInductionVarInfo>
try_build_canonical_induction_var(const CoreIrLoopInfo &loop,
                                  const CoreIrCfgAnalysisResult &cfg) {
    CoreIrBasicBlock *header = loop.get_header();
    CoreIrBasicBlock *preheader = loop.get_preheader();
    if (header == nullptr || preheader == nullptr || loop.get_latches().size() != 1) {
        return std::nullopt;
    }

    CoreIrBasicBlock *latch = *loop.get_latches().begin();
    if (latch == nullptr) {
        return std::nullopt;
    }

    CoreIrBasicBlock *inside_successor = nullptr;
    CoreIrBasicBlock *outside_successor = nullptr;
    bool inside_is_true_successor = true;
    if (!header_has_single_inside_and_outside_successor(
            loop, cfg, header, inside_successor, outside_successor,
            inside_is_true_successor)) {
        return std::nullopt;
    }

    auto *branch = dynamic_cast<CoreIrCondJumpInst *>(
        header->get_instructions().back().get());
    auto *compare = dynamic_cast<CoreIrCompareInst *>(branch->get_condition());
    if (compare == nullptr) {
        return std::nullopt;
    }

    for (const auto &instruction : header->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction.get());
        if (phi == nullptr) {
            break;
        }
        if (phi->get_incoming_count() != 2) {
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
        if (phi->get_incoming_block(latch_index) != latch) {
            continue;
        }

        CoreIrValue *initial_value = phi->get_incoming_value(preheader_index);
        if (!value_is_loop_invariant(initial_value, loop)) {
            continue;
        }

        std::int64_t step = 0;
        CoreIrInstruction *update_instruction = nullptr;
        if (!get_constant_step_from_latch_value(*phi,
                                                phi->get_incoming_value(latch_index),
                                                step, update_instruction)) {
            continue;
        }

        CoreIrValue *bound = nullptr;
        CoreIrComparePredicate predicate = compare->get_predicate();
        if (compare->get_lhs() == phi &&
            value_is_loop_invariant(compare->get_rhs(), loop)) {
            bound = compare->get_rhs();
        } else if (compare->get_rhs() == phi &&
                   value_is_loop_invariant(compare->get_lhs(), loop)) {
            bound = compare->get_lhs();
            predicate = swap_compare_predicate(predicate);
        } else {
            continue;
        }

        switch (predicate) {
        case CoreIrComparePredicate::SignedLess:
        case CoreIrComparePredicate::SignedLessEqual:
        case CoreIrComparePredicate::SignedGreater:
        case CoreIrComparePredicate::SignedGreaterEqual:
        case CoreIrComparePredicate::UnsignedLess:
        case CoreIrComparePredicate::UnsignedLessEqual:
        case CoreIrComparePredicate::UnsignedGreater:
        case CoreIrComparePredicate::UnsignedGreaterEqual:
            break;
        case CoreIrComparePredicate::Equal:
        case CoreIrComparePredicate::NotEqual:
            continue;
        }

        CoreIrCanonicalInductionVarInfo info;
        info.phi = phi;
        info.header = header;
        info.preheader = preheader;
        info.latch = latch;
        info.initial_value = initial_value;
        info.latch_update = update_instruction;
        info.step = step;
        info.exit_compare = compare;
        info.exit_branch = branch;
        info.exit_bound = bound;
        info.normalized_predicate = predicate;
        info.inside_successor_is_true = inside_is_true_successor;
        return info;
    }

    return std::nullopt;
}

} // namespace

void CoreIrInductionVarAnalysisResult::set_canonical_induction_var(
    const CoreIrBasicBlock *header, CoreIrCanonicalInductionVarInfo info) {
    if (header == nullptr || !info.is_valid()) {
        return;
    }
    canonical_induction_vars_[header] = std::move(info);
}

const CoreIrCanonicalInductionVarInfo *
CoreIrInductionVarAnalysisResult::get_canonical_induction_var(
    const CoreIrBasicBlock *header) const noexcept {
    auto it = canonical_induction_vars_.find(header);
    return it == canonical_induction_vars_.end() ? nullptr : &it->second;
}

const CoreIrCanonicalInductionVarInfo *
CoreIrInductionVarAnalysisResult::get_canonical_induction_var(
    const CoreIrLoopInfo &loop) const noexcept {
    return get_canonical_induction_var(loop.get_header());
}

CoreIrInductionVarAnalysisResult CoreIrInductionVarAnalysis::Run(
    const CoreIrFunction &function, const CoreIrCfgAnalysisResult &cfg_analysis,
    const CoreIrDominatorTreeAnalysisResult & /*dominator_tree*/,
    const CoreIrLoopInfoAnalysisResult &loop_info) const {
    CoreIrInductionVarAnalysisResult result(&function);

    for (const auto &loop_ptr : loop_info.get_loops()) {
        if (loop_ptr == nullptr) {
            continue;
        }
        std::optional<CoreIrCanonicalInductionVarInfo> info =
            try_build_canonical_induction_var(*loop_ptr, cfg_analysis);
        if (info.has_value()) {
            result.set_canonical_induction_var(loop_ptr->get_header(),
                                               std::move(*info));
        }
    }

    return result;
}

} // namespace sysycc

