#include "backend/ir/analysis/scalar_evolution_lite_analysis.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_type.hpp"

namespace sysycc {

namespace {

bool loop_contains_block(const CoreIrLoopInfo &loop,
                         const CoreIrBasicBlock *block) {
    return block != nullptr &&
           loop.get_blocks().find(const_cast<CoreIrBasicBlock *>(block)) !=
               loop.get_blocks().end();
}

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

std::optional<std::int64_t> as_constant_int(CoreIrValue *value) {
    auto *constant = dynamic_cast<CoreIrConstantInt *>(value);
    if (constant == nullptr) {
        return std::nullopt;
    }
    return sign_extend_integer_constant(*constant);
}

std::optional<std::uint64_t>
compute_constant_trip_count(const CoreIrCanonicalInductionVarInfo &iv) {
    const std::optional<std::int64_t> init_value =
        as_constant_int(iv.initial_value);
    const std::optional<std::int64_t> bound_value =
        as_constant_int(iv.exit_bound);
    if (!init_value.has_value() || !bound_value.has_value()) {
        return std::nullopt;
    }

    const std::int64_t init = *init_value;
    const std::int64_t bound = *bound_value;
    const std::int64_t step = iv.step;
    if (step == 0) {
        return std::nullopt;
    }

    auto ceil_div_positive = [](std::int64_t numerator,
                                std::int64_t denominator) -> std::uint64_t {
        return static_cast<std::uint64_t>((numerator + denominator - 1) /
                                          denominator);
    };

    switch (iv.normalized_predicate) {
    case CoreIrComparePredicate::SignedLess:
        if (step <= 0) {
            return std::nullopt;
        }
        if (init >= bound) {
            return std::uint64_t{0};
        }
        return ceil_div_positive(bound - init, step);
    case CoreIrComparePredicate::SignedLessEqual:
        if (step <= 0) {
            return std::nullopt;
        }
        if (init > bound) {
            return std::uint64_t{0};
        }
        return ceil_div_positive((bound - init) + 1, step);
    case CoreIrComparePredicate::SignedGreater:
        if (step >= 0) {
            return std::nullopt;
        }
        if (init <= bound) {
            return std::uint64_t{0};
        }
        return ceil_div_positive(init - bound, -step);
    case CoreIrComparePredicate::SignedGreaterEqual:
        if (step >= 0) {
            return std::nullopt;
        }
        if (init < bound) {
            return std::uint64_t{0};
        }
        return ceil_div_positive((init - bound) + 1, -step);
    case CoreIrComparePredicate::UnsignedLess:
    case CoreIrComparePredicate::UnsignedLessEqual:
    case CoreIrComparePredicate::UnsignedGreater:
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        if (init < 0 || bound < 0) {
            return std::nullopt;
        }
        switch (iv.normalized_predicate) {
        case CoreIrComparePredicate::UnsignedLess:
            if (step <= 0) {
                return std::nullopt;
            }
            if (static_cast<std::uint64_t>(init) >=
                static_cast<std::uint64_t>(bound)) {
                return std::uint64_t{0};
            }
            return ceil_div_positive(bound - init, step);
        case CoreIrComparePredicate::UnsignedLessEqual:
            if (step <= 0) {
                return std::nullopt;
            }
            if (static_cast<std::uint64_t>(init) >
                static_cast<std::uint64_t>(bound)) {
                return std::uint64_t{0};
            }
            return ceil_div_positive((bound - init) + 1, step);
        case CoreIrComparePredicate::UnsignedGreater:
            if (step >= 0) {
                return std::nullopt;
            }
            if (static_cast<std::uint64_t>(init) <=
                static_cast<std::uint64_t>(bound)) {
                return std::uint64_t{0};
            }
            return ceil_div_positive(init - bound, -step);
        case CoreIrComparePredicate::UnsignedGreaterEqual:
            if (step >= 0) {
                return std::nullopt;
            }
            if (static_cast<std::uint64_t>(init) <
                static_cast<std::uint64_t>(bound)) {
                return std::uint64_t{0};
            }
            return ceil_div_positive((init - bound) + 1, -step);
        default:
            return std::nullopt;
        }
    case CoreIrComparePredicate::Equal:
    case CoreIrComparePredicate::NotEqual:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace

void CoreIrScalarEvolutionLiteAnalysisResult::set_canonical_induction_var(
    const CoreIrBasicBlock *header,
    const CoreIrCanonicalInductionVarInfo &info) {
    if (header == nullptr || !info.is_valid()) {
        return;
    }
    canonical_induction_vars_[header] = info;
}

void CoreIrScalarEvolutionLiteAnalysisResult::set_backedge_taken_count(
    const CoreIrBasicBlock *header, const CoreIrBackedgeTakenCountInfo &info) {
    if (header == nullptr || !info.has_symbolic_count) {
        return;
    }
    backedge_taken_counts_[header] = info;
}

bool CoreIrScalarEvolutionLiteAnalysisResult::compute_loop_invariant(
    CoreIrValue *value, const CoreIrLoopInfo &loop,
    std::unordered_map<const CoreIrValue *, bool> &cache,
    std::unordered_map<const CoreIrValue *, bool> &visiting) const {
    if (value == nullptr) {
        return false;
    }

    auto cache_it = cache.find(value);
    if (cache_it != cache.end()) {
        return cache_it->second;
    }
    if (visiting[value]) {
        return false;
    }
    visiting[value] = true;

    bool invariant = false;
    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        invariant = true;
    } else if (!loop_contains_block(loop, instruction->get_parent())) {
        invariant = true;
    } else {
        switch (instruction->get_opcode()) {
        case CoreIrOpcode::AddressOfFunction:
        case CoreIrOpcode::AddressOfGlobal:
        case CoreIrOpcode::AddressOfStackSlot:
            invariant = true;
            break;
        case CoreIrOpcode::Binary:
        case CoreIrOpcode::Unary:
        case CoreIrOpcode::Compare:
        case CoreIrOpcode::Select:
        case CoreIrOpcode::Cast:
        case CoreIrOpcode::GetElementPtr: {
            invariant = true;
            for (CoreIrValue *operand : instruction->get_operands()) {
                if (!compute_loop_invariant(operand, loop, cache, visiting)) {
                    invariant = false;
                    break;
                }
            }
            break;
        }
        case CoreIrOpcode::Phi:
        case CoreIrOpcode::Load:
        case CoreIrOpcode::Store:
        case CoreIrOpcode::Call:
        case CoreIrOpcode::Jump:
        case CoreIrOpcode::CondJump:
        case CoreIrOpcode::Return:
            invariant = false;
            break;
        }
    }

    cache[value] = invariant;
    visiting.erase(value);
    return invariant;
}

bool CoreIrScalarEvolutionLiteAnalysisResult::is_loop_invariant(
    CoreIrValue *value, const CoreIrLoopInfo &loop) const {
    std::unordered_map<const CoreIrValue *, bool> cache;
    std::unordered_map<const CoreIrValue *, bool> visiting;
    return compute_loop_invariant(value, loop, cache, visiting);
}

CoreIrScevExpr CoreIrScalarEvolutionLiteAnalysisResult::compute_expr(
    CoreIrValue *value, const CoreIrLoopInfo &loop,
    std::unordered_map<const CoreIrValue *, CoreIrScevExpr> &cache,
    std::unordered_map<const CoreIrValue *, bool> &visiting) const {
    if (value == nullptr) {
        return CoreIrScevExpr::unknown();
    }

    auto cache_it = cache.find(value);
    if (cache_it != cache.end()) {
        return cache_it->second;
    }
    if (visiting[value]) {
        return CoreIrScevExpr::unknown();
    }
    visiting[value] = true;

    if (const std::optional<std::int64_t> constant_value =
            as_constant_int(value);
        constant_value.has_value()) {
        CoreIrScevExpr expr = CoreIrScevExpr::constant_expr(*constant_value);
        cache[value] = expr;
        visiting.erase(value);
        return expr;
    }

    auto *instruction = dynamic_cast<CoreIrInstruction *>(value);
    if (instruction == nullptr) {
        CoreIrScevExpr expr = CoreIrScevExpr::unknown();
        cache[value] = expr;
        visiting.erase(value);
        return expr;
    }

    if (!loop_contains_block(loop, instruction->get_parent())) {
        CoreIrScevExpr expr = CoreIrScevExpr::unknown();
        cache[value] = expr;
        visiting.erase(value);
        return expr;
    }

    const CoreIrCanonicalInductionVarInfo *iv =
        get_canonical_induction_var(loop);
    if (iv != nullptr && instruction == iv->phi) {
        CoreIrScevExpr expr = CoreIrScevExpr::add_rec(
            iv->initial_value, iv->step, loop.get_header());
        cache[value] = expr;
        visiting.erase(value);
        return expr;
    }

    CoreIrScevExpr expr = CoreIrScevExpr::unknown();
    switch (instruction->get_opcode()) {
    case CoreIrOpcode::AddressOfFunction:
    case CoreIrOpcode::AddressOfGlobal:
    case CoreIrOpcode::AddressOfStackSlot:
        expr = CoreIrScevExpr::constant_expr(0);
        break;
    case CoreIrOpcode::Binary: {
        auto *binary = dynamic_cast<CoreIrBinaryInst *>(instruction);
        CoreIrScevExpr lhs =
            compute_expr(binary->get_lhs(), loop, cache, visiting);
        CoreIrScevExpr rhs =
            compute_expr(binary->get_rhs(), loop, cache, visiting);
        switch (binary->get_binary_opcode()) {
        case CoreIrBinaryOpcode::Add:
            if (lhs.kind == CoreIrScevExprKind::Constant &&
                rhs.kind == CoreIrScevExprKind::Constant) {
                expr =
                    CoreIrScevExpr::constant_expr(lhs.constant + rhs.constant);
            } else if (lhs.kind == CoreIrScevExprKind::AddRec &&
                       rhs.kind == CoreIrScevExprKind::Constant) {
                expr = lhs;
            } else if (lhs.kind == CoreIrScevExprKind::Constant &&
                       rhs.kind == CoreIrScevExprKind::AddRec) {
                expr = rhs;
            }
            break;
        case CoreIrBinaryOpcode::Sub:
            if (lhs.kind == CoreIrScevExprKind::Constant &&
                rhs.kind == CoreIrScevExprKind::Constant) {
                expr =
                    CoreIrScevExpr::constant_expr(lhs.constant - rhs.constant);
            } else if (lhs.kind == CoreIrScevExprKind::AddRec &&
                       rhs.kind == CoreIrScevExprKind::Constant) {
                expr = lhs;
            }
            break;
        default:
            break;
        }
        break;
    }
    case CoreIrOpcode::Unary: {
        auto *unary = dynamic_cast<CoreIrUnaryInst *>(instruction);
        CoreIrScevExpr operand =
            compute_expr(unary->get_operand(), loop, cache, visiting);
        if (operand.kind == CoreIrScevExprKind::Constant &&
            unary->get_unary_opcode() == CoreIrUnaryOpcode::Negate) {
            expr = CoreIrScevExpr::constant_expr(-operand.constant);
        }
        break;
    }
    case CoreIrOpcode::Compare:
    case CoreIrOpcode::Select:
    case CoreIrOpcode::Cast:
    case CoreIrOpcode::GetElementPtr:
    case CoreIrOpcode::Load:
    case CoreIrOpcode::Store:
    case CoreIrOpcode::Call:
    case CoreIrOpcode::Jump:
    case CoreIrOpcode::CondJump:
    case CoreIrOpcode::Return:
    case CoreIrOpcode::Phi:
        break;
    }

    cache[value] = expr;
    visiting.erase(value);
    return expr;
}

CoreIrScevExpr CoreIrScalarEvolutionLiteAnalysisResult::get_expr(
    CoreIrValue *value, const CoreIrLoopInfo &loop) const {
    std::unordered_map<const CoreIrValue *, CoreIrScevExpr> cache;
    std::unordered_map<const CoreIrValue *, bool> visiting;
    return compute_expr(value, loop, cache, visiting);
}

const CoreIrCanonicalInductionVarInfo *
CoreIrScalarEvolutionLiteAnalysisResult::get_canonical_induction_var(
    const CoreIrLoopInfo &loop) const noexcept {
    auto it = canonical_induction_vars_.find(loop.get_header());
    return it == canonical_induction_vars_.end() ? nullptr : &it->second;
}

const CoreIrBackedgeTakenCountInfo *
CoreIrScalarEvolutionLiteAnalysisResult::get_backedge_taken_count(
    const CoreIrLoopInfo &loop) const noexcept {
    auto it = backedge_taken_counts_.find(loop.get_header());
    return it == backedge_taken_counts_.end() ? nullptr : &it->second;
}

std::optional<std::uint64_t>
CoreIrScalarEvolutionLiteAnalysisResult::get_constant_trip_count(
    const CoreIrLoopInfo &loop) const noexcept {
    const CoreIrBackedgeTakenCountInfo *count = get_backedge_taken_count(loop);
    return count == nullptr ? std::nullopt : count->constant_trip_count;
}

CoreIrScalarEvolutionLiteAnalysisResult CoreIrScalarEvolutionLiteAnalysis::Run(
    const CoreIrFunction &function,
    const CoreIrCfgAnalysisResult & /*cfg_analysis*/,
    const CoreIrLoopInfoAnalysisResult &loop_info,
    const CoreIrInductionVarAnalysisResult &induction_vars) const {
    CoreIrScalarEvolutionLiteAnalysisResult result(&function);

    for (const auto &loop_ptr : loop_info.get_loops()) {
        if (loop_ptr == nullptr) {
            continue;
        }
        const CoreIrCanonicalInductionVarInfo *iv =
            induction_vars.get_canonical_induction_var(*loop_ptr);
        if (iv == nullptr) {
            continue;
        }
        result.set_canonical_induction_var(loop_ptr->get_header(), *iv);

        CoreIrBackedgeTakenCountInfo count;
        count.has_symbolic_count = true;
        count.initial_value = iv->initial_value;
        count.bound_value = iv->exit_bound;
        count.step = iv->step;
        count.predicate = iv->normalized_predicate;
        count.constant_trip_count = compute_constant_trip_count(*iv);
        result.set_backedge_taken_count(loop_ptr->get_header(), count);
    }

    return result;
}

} // namespace sysycc
