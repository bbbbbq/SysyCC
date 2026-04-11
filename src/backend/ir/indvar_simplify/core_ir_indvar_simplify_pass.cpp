#include "backend/ir/indvar_simplify/core_ir_indvar_simplify_pass.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/induction_var_analysis.hpp"
#include "backend/ir/analysis/dominator_tree_analysis.hpp"
#include "backend/ir/analysis/loop_info_analysis.hpp"
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

PassResult fail_missing_core_ir(CompilerContext &context,
                                const char *pass_name) {
    const std::string message =
        std::string(pass_name) + " requires a built core ir result";
    context.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                              message);
    return PassResult::Failure(message);
}

CoreIrComparePredicate
negate_compare_predicate(CoreIrComparePredicate predicate) {
    switch (predicate) {
    case CoreIrComparePredicate::Equal:
        return CoreIrComparePredicate::NotEqual;
    case CoreIrComparePredicate::NotEqual:
        return CoreIrComparePredicate::Equal;
    case CoreIrComparePredicate::SignedLess:
        return CoreIrComparePredicate::SignedGreaterEqual;
    case CoreIrComparePredicate::SignedLessEqual:
        return CoreIrComparePredicate::SignedGreater;
    case CoreIrComparePredicate::SignedGreater:
        return CoreIrComparePredicate::SignedLessEqual;
    case CoreIrComparePredicate::SignedGreaterEqual:
        return CoreIrComparePredicate::SignedLess;
    case CoreIrComparePredicate::UnsignedLess:
        return CoreIrComparePredicate::UnsignedGreaterEqual;
    case CoreIrComparePredicate::UnsignedLessEqual:
        return CoreIrComparePredicate::UnsignedGreater;
    case CoreIrComparePredicate::UnsignedGreater:
        return CoreIrComparePredicate::UnsignedLessEqual;
    case CoreIrComparePredicate::UnsignedGreaterEqual:
        return CoreIrComparePredicate::UnsignedLess;
    }
    return predicate;
}

bool canonicalize_loop_compare(const CoreIrCanonicalInductionVarInfo &iv) {
    if (iv.exit_compare == nullptr || iv.phi == nullptr ||
        iv.exit_bound == nullptr) {
        return false;
    }

    bool changed = false;
    if (iv.exit_compare->get_lhs() != iv.phi) {
        iv.exit_compare->set_operand(0, iv.phi);
        iv.exit_compare->set_operand(1, iv.exit_bound);
        iv.exit_compare->set_predicate(iv.normalized_predicate);
        changed = true;
    } else if (iv.exit_compare->get_rhs() != iv.exit_bound ||
               iv.exit_compare->get_predicate() != iv.normalized_predicate) {
        iv.exit_compare->set_operand(1, iv.exit_bound);
        iv.exit_compare->set_predicate(iv.normalized_predicate);
        changed = true;
    }

    if (iv.exit_branch == nullptr) {
        return changed;
    }

    CoreIrComparePredicate expected_branch_predicate = iv.normalized_predicate;
    if (!iv.inside_successor_is_true) {
        expected_branch_predicate =
            negate_compare_predicate(expected_branch_predicate);
    }
    if (iv.exit_compare->get_predicate() != expected_branch_predicate) {
        iv.exit_compare->set_predicate(expected_branch_predicate);
        changed = true;
    }
    return changed;
}

bool loop_contains_block(const CoreIrLoopInfo &loop,
                         const CoreIrBasicBlock *block) {
    return block != nullptr &&
           loop.get_blocks().find(const_cast<CoreIrBasicBlock *>(block)) !=
               loop.get_blocks().end();
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

bool instruction_has_external_use(const CoreIrInstruction &instruction,
                                  const CoreIrLoopInfo &loop) {
    for (const CoreIrUse &use : instruction.get_uses()) {
        if (!loop_contains_block(loop, get_use_location_block(use))) {
            return true;
        }
    }
    return false;
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

bool parameter_is_readonly(const CoreIrFunction &function, CoreIrValue *value) {
    auto *parameter = dynamic_cast<CoreIrParameter *>(value);
    if (parameter == nullptr) {
        return false;
    }

    const auto &parameters = function.get_parameters();
    const auto &parameter_readonly = function.get_parameter_readonly();
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (parameters[index].get() != parameter) {
            continue;
        }
        return index < parameter_readonly.size() && parameter_readonly[index];
    }
    return false;
}

struct SignedScaleInfo {
    CoreIrValue *factor = nullptr;
    bool negate = false;
};

CoreIrValue *materialize_signed_value(CoreIrBasicBlock &block,
                                      const std::string &base_name,
                                      CoreIrValue *value, bool negate,
                                      CoreIrInstruction *anchor) {
    if (value == nullptr) {
        return nullptr;
    }
    if (!negate) {
        return value;
    }

    auto negate_instruction = std::make_unique<CoreIrUnaryInst>(
        CoreIrUnaryOpcode::Negate, value->get_type(), base_name + ".neg",
        value);
    return insert_instruction_before(block, anchor,
                                     std::move(negate_instruction));
}

CoreIrValue *materialize_scaled_value(CoreIrBasicBlock &block,
                                      const std::string &base_name,
                                      CoreIrValue *base_value,
                                      const SignedScaleInfo &scale,
                                      CoreIrInstruction *anchor) {
    if (base_value == nullptr) {
        return nullptr;
    }

    CoreIrValue *scaled_value = base_value;
    if (scale.factor != nullptr) {
        auto multiply = std::make_unique<CoreIrBinaryInst>(
            CoreIrBinaryOpcode::Mul, base_value->get_type(), base_name + ".mul",
            base_value, scale.factor);
        scaled_value =
            insert_instruction_before(block, anchor, std::move(multiply));
    }
    return materialize_signed_value(block, base_name, scaled_value,
                                    scale.negate, anchor);
}

CoreIrValue *materialize_affine_initial_value(
    CoreIrBasicBlock &preheader, const std::string &base_name,
    CoreIrValue *iv_initial, const SignedScaleInfo &scale,
    const CoreIrType *result_type, CoreIrBinaryOpcode combine_opcode,
    CoreIrValue *other_value,
    bool scaled_on_lhs, CoreIrInstruction *anchor) {
    CoreIrValue *scaled_value = materialize_scaled_value(
        preheader, base_name + ".init", iv_initial, scale, anchor);
    if (scaled_value == nullptr) {
        return nullptr;
    }
    if (other_value == nullptr) {
        return scaled_value;
    }

    CoreIrValue *lhs = scaled_on_lhs ? scaled_value : other_value;
    CoreIrValue *rhs = scaled_on_lhs ? other_value : scaled_value;
    auto combine = std::make_unique<CoreIrBinaryInst>(
        combine_opcode, result_type, base_name + ".init", lhs, rhs);
    return insert_instruction_before(preheader, anchor, std::move(combine));
}

CoreIrValue *create_integer_step_value(CoreIrBasicBlock &preheader,
                                       CoreIrContext &core_ir_context,
                                       const std::string &base_name,
                                       CoreIrValue *factor, std::int64_t step,
                                       CoreIrInstruction *anchor) {
    if (factor == nullptr || factor->get_type() == nullptr || step == 0) {
        return nullptr;
    }
    if (step == 1) {
        return factor;
    }
    if (step == -1) {
        auto negate = std::make_unique<CoreIrUnaryInst>(
            CoreIrUnaryOpcode::Negate, factor->get_type(), base_name + ".delta",
            factor);
        return insert_instruction_before(preheader, anchor, std::move(negate));
    }

    auto *step_constant = core_ir_context.create_constant<CoreIrConstantInt>(
        factor->get_type(), static_cast<std::uint64_t>(step));
    auto delta = std::make_unique<CoreIrBinaryInst>(
        CoreIrBinaryOpcode::Mul, factor->get_type(), base_name + ".delta",
        factor, step_constant);
    return insert_instruction_before(preheader, anchor, std::move(delta));
}

std::optional<SignedScaleInfo> match_scaled_iv_value(
    CoreIrValue *value, const CoreIrLoopInfo &loop,
    const CoreIrCanonicalInductionVarInfo &iv,
    const std::unordered_map<const CoreIrPhiInst *, CoreIrValue *>
        &reduced_factors) {
    if (value == iv.phi) {
        return SignedScaleInfo{};
    }

    auto *phi = dynamic_cast<CoreIrPhiInst *>(value);
    if (phi != nullptr) {
        auto it = reduced_factors.find(phi);
        if (it != reduced_factors.end()) {
            return SignedScaleInfo{it->second, false};
        }
    }

    auto *binary = dynamic_cast<CoreIrBinaryInst *>(value);
    if (binary == nullptr ||
        binary->get_binary_opcode() != CoreIrBinaryOpcode::Mul ||
        binary->get_type() != iv.phi->get_type()) {
        return std::nullopt;
    }

    CoreIrValue *factor = nullptr;
    if (binary->get_lhs() == iv.phi) {
        factor = binary->get_rhs();
    } else if (binary->get_rhs() == iv.phi) {
        factor = binary->get_lhs();
    } else {
        return std::nullopt;
    }

    auto *factor_instruction = dynamic_cast<CoreIrInstruction *>(factor);
    if (factor_instruction != nullptr &&
        loop_contains_block(loop, factor_instruction->get_parent())) {
        return std::nullopt;
    }
    return SignedScaleInfo{factor, false};
}

bool match_iv_plus_constant(CoreIrValue *value, CoreIrPhiInst *iv_phi,
                            std::int64_t &offset) {
    if (value == iv_phi) {
        offset = 0;
        return true;
    }

    auto *binary = dynamic_cast<CoreIrBinaryInst *>(value);
    if (binary == nullptr) {
        return false;
    }

    auto decode_constant = [](CoreIrValue *candidate,
                              std::int64_t &decoded) -> bool {
        auto *constant = dynamic_cast<CoreIrConstantInt *>(candidate);
        if (constant == nullptr) {
            return false;
        }
        decoded = static_cast<std::int64_t>(constant->get_value());
        return true;
    };

    std::int64_t constant_value = 0;
    switch (binary->get_binary_opcode()) {
    case CoreIrBinaryOpcode::Add:
        if (binary->get_lhs() == iv_phi &&
            decode_constant(binary->get_rhs(), constant_value)) {
            offset = constant_value;
            return true;
        }
        if (binary->get_rhs() == iv_phi &&
            decode_constant(binary->get_lhs(), constant_value)) {
            offset = constant_value;
            return true;
        }
        return false;
    case CoreIrBinaryOpcode::Sub:
        if (binary->get_lhs() == iv_phi &&
            decode_constant(binary->get_rhs(), constant_value)) {
            offset = -constant_value;
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool match_constant_delta(CoreIrValue *base_value, CoreIrValue *value,
                          std::int64_t &offset) {
    if (base_value == value) {
        offset = 0;
        return true;
    }

    auto *base_constant = dynamic_cast<CoreIrConstantInt *>(base_value);
    auto *constant = dynamic_cast<CoreIrConstantInt *>(value);
    if (base_constant == nullptr || constant == nullptr) {
        auto *binary = dynamic_cast<CoreIrBinaryInst *>(value);
        if (binary == nullptr) {
            return false;
        }

        std::int64_t constant_value = 0;
        auto decode_constant = [](CoreIrValue *candidate,
                                  std::int64_t &decoded) -> bool {
            auto *constant = dynamic_cast<CoreIrConstantInt *>(candidate);
            if (constant == nullptr) {
                return false;
            }
            decoded = static_cast<std::int64_t>(constant->get_value());
            return true;
        };

        switch (binary->get_binary_opcode()) {
        case CoreIrBinaryOpcode::Add:
            if (binary->get_lhs() == base_value &&
                decode_constant(binary->get_rhs(), constant_value)) {
                offset = constant_value;
                return true;
            }
            if (binary->get_rhs() == base_value &&
                decode_constant(binary->get_lhs(), constant_value)) {
                offset = constant_value;
                return true;
            }
            return false;
        case CoreIrBinaryOpcode::Sub:
            if (binary->get_lhs() == base_value &&
                decode_constant(binary->get_rhs(), constant_value)) {
                offset = -constant_value;
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    offset = static_cast<std::int64_t>(constant->get_value()) -
             static_cast<std::int64_t>(base_constant->get_value());
    return true;
}

bool match_shifted_header_iv(CoreIrValue *value,
                             const CoreIrCanonicalInductionVarInfo &iv,
                             std::int64_t &offset) {
    auto *phi = dynamic_cast<CoreIrPhiInst *>(value);
    if (phi == nullptr || phi == iv.phi || phi->get_parent() != iv.header ||
        phi->get_incoming_count() != 2) {
        return false;
    }

    std::size_t preheader_index =
        phi->get_incoming_block(0) == iv.preheader
            ? 0
            : phi->get_incoming_block(1) == iv.preheader
                  ? 1
                  : phi->get_incoming_count();
    if (preheader_index >= phi->get_incoming_count()) {
        return false;
    }
    const std::size_t latch_index = preheader_index == 0 ? 1 : 0;
    if (phi->get_incoming_block(latch_index) != iv.latch) {
        return false;
    }

    std::int64_t initial_offset = 0;
    if (!match_constant_delta(iv.initial_value, phi->get_incoming_value(preheader_index),
                              initial_offset)) {
        return false;
    }

    std::int64_t update_offset = 0;
    if (!match_iv_plus_constant(phi->get_incoming_value(latch_index), phi,
                                update_offset) ||
        update_offset != iv.step) {
        return false;
    }

    offset = initial_offset;
    return true;
}

bool match_iv_indexed_load(CoreIrLoadInst &load,
                           const CoreIrCanonicalInductionVarInfo &iv,
                           CoreIrValue *&root_base, std::int64_t &offset) {
    auto *gep = dynamic_cast<CoreIrGetElementPtrInst *>(
        detail::unwrap_trivial_zero_index_geps(load.get_address()));
    if (gep == nullptr) {
        return false;
    }

    CoreIrValue *candidate_root = detail::unwrap_trivial_zero_index_geps(
        gep->get_base());
    if (candidate_root == nullptr || gep->get_index_count() == 0) {
        return false;
    }
    const auto *root_pointer_type =
        dynamic_cast<const CoreIrPointerType *>(candidate_root->get_type());
    if (root_pointer_type == nullptr ||
        dynamic_cast<const CoreIrIntegerType *>(
            root_pointer_type->get_pointee_type()) == nullptr) {
        return false;
    }

    CoreIrValue *index = gep->get_index(0);
    if (gep->get_index_count() == 2 &&
        detail::is_zero_integer_constant(gep->get_index(0))) {
        index = gep->get_index(1);
    } else if (gep->get_index_count() != 1) {
        return false;
    }

    if (index == nullptr ||
        (!match_iv_plus_constant(index, iv.phi, offset) &&
         !match_shifted_header_iv(index, iv, offset))) {
        return false;
    }

    root_base = candidate_root;
    return true;
}

bool strength_reduce_row_bound_recurrence(
    const CoreIrLoopInfo &loop, const CoreIrCanonicalInductionVarInfo &iv,
    const CoreIrDominatorTreeAnalysisResult &dom_tree,
    CoreIrContext &core_ir_context) {
    if (iv.phi == nullptr || iv.header == nullptr || iv.preheader == nullptr ||
        iv.latch == nullptr || iv.initial_value == nullptr || iv.step != 1) {
        return false;
    }

    CoreIrFunction *function = iv.header->get_parent();
    if (function == nullptr) {
        return false;
    }
    std::size_t readonly_integer_pointer_params = 0;
    const auto &parameters = function->get_parameters();
    const auto &parameter_readonly = function->get_parameter_readonly();
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        if (index >= parameter_readonly.size() || !parameter_readonly[index] ||
            parameters[index] == nullptr) {
            continue;
        }
        const auto *pointer_type =
            dynamic_cast<const CoreIrPointerType *>(parameters[index]->get_type());
        if (pointer_type != nullptr &&
            dynamic_cast<const CoreIrIntegerType *>(
                pointer_type->get_pointee_type()) != nullptr) {
            ++readonly_integer_pointer_params;
        }
    }
    if (readonly_integer_pointer_params < 2) {
        return false;
    }

    std::unordered_map<CoreIrValue *, std::vector<CoreIrLoadInst *>> start_loads_by_base;
    std::unordered_map<CoreIrValue *, std::vector<CoreIrLoadInst *>> end_loads_by_base;

    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            auto *load = dynamic_cast<CoreIrLoadInst *>(instruction_ptr.get());
            if (load == nullptr || load->get_stack_slot() != nullptr) {
                continue;
            }
            if (!dom_tree.dominates(block, iv.latch)) {
                continue;
            }

            CoreIrValue *root_base = nullptr;
            std::int64_t offset = 0;
            if (!match_iv_indexed_load(*load, iv, root_base, offset) ||
                root_base == nullptr ||
                !parameter_is_readonly(*function, root_base)) {
                continue;
            }

            if (offset == 0) {
                start_loads_by_base[root_base].push_back(load);
            } else if (offset == 1 &&
                       dom_tree.dominates(load->get_parent(), iv.latch)) {
                end_loads_by_base[root_base].push_back(load);
            }
        }
    }

    CoreIrInstruction *preheader_anchor =
        iv.preheader->get_instructions().empty()
            ? nullptr
            : iv.preheader->get_instructions().back().get();
    bool changed = false;
    std::size_t recurrence_index = 0;

    for (auto &[root_base, start_loads] : start_loads_by_base) {
        auto end_it = end_loads_by_base.find(root_base);
        if (root_base == nullptr || start_loads.empty() ||
            end_it == end_loads_by_base.end() || end_it->second.empty()) {
            continue;
        }

        CoreIrLoadInst *carry_load = end_it->second.front();
        for (CoreIrLoadInst *candidate : end_it->second) {
            if (candidate != nullptr &&
                dom_tree.dominates(candidate->get_parent(), carry_load->get_parent())) {
                carry_load = candidate;
            }
        }
        if (carry_load == nullptr || carry_load->get_address() == nullptr) {
            continue;
        }

        auto init_gep = std::make_unique<CoreIrGetElementPtrInst>(
            carry_load->get_address()->get_type(),
            root_base->get_name() + ".row.start.addr.init",
            root_base, std::vector<CoreIrValue *>{iv.initial_value});
        auto *init_gep_ptr = static_cast<CoreIrGetElementPtrInst *>(
            preheader_anchor == nullptr
                ? iv.preheader->append_instruction(std::move(init_gep))
                : insert_instruction_before(*iv.preheader, preheader_anchor,
                                            std::move(init_gep)));
        if (init_gep_ptr == nullptr) {
            continue;
        }

        auto init_load = std::make_unique<CoreIrLoadInst>(
            carry_load->get_type(),
            root_base->get_name() + ".row.start.init." +
                std::to_string(recurrence_index),
            init_gep_ptr);
        auto *init_load_ptr = static_cast<CoreIrLoadInst *>(
            preheader_anchor == nullptr
                ? iv.preheader->append_instruction(std::move(init_load))
                : insert_instruction_before(*iv.preheader, preheader_anchor,
                                            std::move(init_load)));
        if (init_load_ptr == nullptr) {
            continue;
        }

        auto recurrence_phi_inst = std::make_unique<CoreIrPhiInst>(
            carry_load->get_type(),
            root_base->get_name() + ".row.start.loop." +
                std::to_string(recurrence_index++));
        auto *recurrence_phi = static_cast<CoreIrPhiInst *>(
            iv.header->insert_instruction_before_first_non_phi(
                std::move(recurrence_phi_inst)));
        if (recurrence_phi == nullptr) {
            continue;
        }
        recurrence_phi->add_incoming(iv.preheader, init_load_ptr);
        recurrence_phi->add_incoming(iv.latch, carry_load);

        for (CoreIrLoadInst *load : start_loads) {
            if (load == nullptr) {
                continue;
            }
            load->replace_all_uses_with(recurrence_phi);
            changed = erase_instruction(*load->get_parent(), load) || changed;
        }

        for (CoreIrLoadInst *load : end_it->second) {
            if (load == nullptr || load == carry_load) {
                continue;
            }
            if (!dom_tree.dominates(carry_load->get_parent(), load->get_parent())) {
                continue;
            }
            load->replace_all_uses_with(carry_load);
            changed = erase_instruction(*load->get_parent(), load) || changed;
        }
    }

    return changed;
}

bool strength_reduce_scaled_induction(const CoreIrLoopInfo &loop,
                                      const CoreIrCanonicalInductionVarInfo &iv,
                                      CoreIrContext &core_ir_context) {
    if (iv.phi == nullptr || iv.preheader == nullptr || iv.latch == nullptr ||
        iv.initial_value == nullptr) {
        return false;
    }

    auto *integer_type =
        dynamic_cast<const CoreIrIntegerType *>(iv.phi->get_type());
    if (integer_type == nullptr) {
        return false;
    }

    CoreIrInstruction *preheader_anchor =
        iv.preheader->get_instructions().empty()
            ? nullptr
            : iv.preheader->get_instructions().back().get();
    CoreIrInstruction *latch_anchor =
        iv.latch->get_instructions().empty()
            ? nullptr
            : iv.latch->get_instructions().back().get();
    if (preheader_anchor == nullptr || latch_anchor == nullptr) {
        return false;
    }

    std::unordered_map<CoreIrValue *, CoreIrPhiInst *> reduced_by_factor;
    std::unordered_map<const CoreIrPhiInst *, CoreIrValue *>
        reduced_factor_by_phi;
    bool changed = false;
    std::size_t reduced_index = 0;

    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }

        std::vector<CoreIrBinaryInst *> candidates;
        for (const auto &instruction_ptr : block->get_instructions()) {
            auto *binary =
                dynamic_cast<CoreIrBinaryInst *>(instruction_ptr.get());
            if (binary == nullptr ||
                binary->get_binary_opcode() != CoreIrBinaryOpcode::Mul ||
                binary->get_type() != iv.phi->get_type() ||
                instruction_has_external_use(*binary, loop)) {
                continue;
            }

            CoreIrValue *factor = nullptr;
            if (binary->get_lhs() == iv.phi) {
                factor = binary->get_rhs();
            } else if (binary->get_rhs() == iv.phi) {
                factor = binary->get_lhs();
            } else {
                continue;
            }

            auto *factor_instruction =
                dynamic_cast<CoreIrInstruction *>(factor);
            if (factor_instruction != nullptr &&
                loop_contains_block(loop, factor_instruction->get_parent())) {
                continue;
            }
            candidates.push_back(binary);
        }

        for (CoreIrBinaryInst *candidate : candidates) {
            if (candidate == nullptr) {
                continue;
            }

            CoreIrValue *factor = candidate->get_lhs() == iv.phi
                                      ? candidate->get_rhs()
                                      : candidate->get_lhs();
            if (factor == nullptr) {
                continue;
            }

            CoreIrPhiInst *derived_phi = nullptr;
            auto reduced_it = reduced_by_factor.find(factor);
            if (reduced_it != reduced_by_factor.end()) {
                derived_phi = reduced_it->second;
            } else {
                auto initial_mul = std::make_unique<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::Mul, iv.phi->get_type(),
                    candidate->get_name() + ".init", iv.initial_value, factor);
                CoreIrInstruction *initial_value =
                    preheader_anchor == nullptr
                        ? iv.preheader->append_instruction(
                              std::move(initial_mul))
                        : insert_instruction_before(*iv.preheader,
                                                    preheader_anchor,
                                                    std::move(initial_mul));
                if (initial_value == nullptr) {
                    continue;
                }

                CoreIrValue *delta = create_integer_step_value(
                    *iv.preheader, core_ir_context, candidate->get_name(),
                    factor, iv.step, preheader_anchor);
                if (delta == nullptr) {
                    continue;
                }

                auto derived_phi_inst = std::make_unique<CoreIrPhiInst>(
                    iv.phi->get_type(), candidate->get_name() + ".loop." +
                                            std::to_string(reduced_index++));
                derived_phi = static_cast<CoreIrPhiInst *>(
                    iv.header->insert_instruction_before_first_non_phi(
                        std::move(derived_phi_inst)));
                if (derived_phi == nullptr) {
                    continue;
                }
                derived_phi->add_incoming(iv.preheader, initial_value);

                auto next_value = std::make_unique<CoreIrBinaryInst>(
                    CoreIrBinaryOpcode::Add, iv.phi->get_type(),
                    derived_phi->get_name() + ".next", derived_phi, delta);
                CoreIrInstruction *next_instruction =
                    iv.latch->insert_instruction_before_first_non_phi(
                        std::move(next_value));
                if (next_instruction == nullptr) {
                    continue;
                }
                derived_phi->add_incoming(iv.latch, next_instruction);
                reduced_by_factor.emplace(factor, derived_phi);
                reduced_factor_by_phi.emplace(derived_phi, factor);
            }

            if (derived_phi == nullptr) {
                continue;
            }
            candidate->replace_all_uses_with(derived_phi);
            erase_instruction(*block, candidate);
            changed = true;
        }
    }

    if (loop.get_blocks().size() > 3) {
        return changed;
    }

    for (CoreIrBasicBlock *block : loop.get_blocks()) {
        if (block == nullptr) {
            continue;
        }

        std::vector<CoreIrBinaryInst *> candidates;
        for (const auto &instruction_ptr : block->get_instructions()) {
            auto *binary =
                dynamic_cast<CoreIrBinaryInst *>(instruction_ptr.get());
            if (binary == nullptr || binary == iv.latch_update ||
                (binary->get_binary_opcode() != CoreIrBinaryOpcode::Add &&
                 binary->get_binary_opcode() != CoreIrBinaryOpcode::Sub) ||
                binary->get_type() != iv.phi->get_type() ||
                instruction_has_external_use(*binary, loop)) {
                continue;
            }
            candidates.push_back(binary);
        }

        for (CoreIrBinaryInst *candidate : candidates) {
            if (candidate == nullptr) {
                continue;
            }

            std::optional<SignedScaleInfo> scale = match_scaled_iv_value(
                candidate->get_lhs(), loop, iv, reduced_factor_by_phi);
            CoreIrValue *other_value = candidate->get_rhs();
            bool scaled_on_lhs = true;
            if (!scale.has_value() ||
                !value_is_loop_invariant(other_value, loop)) {
                scale = match_scaled_iv_value(candidate->get_rhs(), loop, iv,
                                              reduced_factor_by_phi);
                other_value = candidate->get_lhs();
                scaled_on_lhs = false;
            }
            if (!scale.has_value() ||
                !value_is_loop_invariant(other_value, loop)) {
                continue;
            }

            if (candidate->get_binary_opcode() == CoreIrBinaryOpcode::Sub &&
                !scaled_on_lhs) {
                scale->negate = !scale->negate;
            }

            CoreIrValue *signed_factor = scale->factor;
            if (signed_factor == nullptr) {
                signed_factor =
                    core_ir_context.create_constant<CoreIrConstantInt>(
                        integer_type, 1);
            }
            signed_factor = materialize_signed_value(
                *iv.preheader, candidate->get_name() + ".scale", signed_factor,
                scale->negate, preheader_anchor);
            if (signed_factor == nullptr) {
                continue;
            }

            CoreIrValue *initial_value = materialize_affine_initial_value(
                *iv.preheader, candidate->get_name(), iv.initial_value, *scale,
                iv.phi->get_type(), candidate->get_binary_opcode(), other_value,
                scaled_on_lhs, preheader_anchor);
            if (initial_value == nullptr) {
                continue;
            }

            CoreIrValue *delta = create_integer_step_value(
                *iv.preheader, core_ir_context, candidate->get_name(),
                signed_factor, iv.step, preheader_anchor);
            if (delta == nullptr) {
                continue;
            }

            auto derived_phi_inst = std::make_unique<CoreIrPhiInst>(
                iv.phi->get_type(), candidate->get_name() + ".loop." +
                                        std::to_string(reduced_index++));
            auto *derived_phi = static_cast<CoreIrPhiInst *>(
                iv.header->insert_instruction_before_first_non_phi(
                    std::move(derived_phi_inst)));
            if (derived_phi == nullptr) {
                continue;
            }
            derived_phi->add_incoming(iv.preheader, initial_value);

            auto next_value = std::make_unique<CoreIrBinaryInst>(
                CoreIrBinaryOpcode::Add, iv.phi->get_type(),
                derived_phi->get_name() + ".next", derived_phi, delta);
            CoreIrInstruction *next_instruction =
                iv.latch->insert_instruction_before_first_non_phi(
                    std::move(next_value));
            if (next_instruction == nullptr) {
                continue;
            }
            derived_phi->add_incoming(iv.latch, next_instruction);

            candidate->replace_all_uses_with(derived_phi);
            erase_instruction(*block, candidate);
            changed = true;
        }
    }

    return changed;
}

} // namespace

PassKind CoreIrIndVarSimplifyPass::Kind() const {
    return PassKind::CoreIrIndVarSimplify;
}

const char *CoreIrIndVarSimplifyPass::Name() const {
    return "CoreIrIndVarSimplifyPass";
}

PassResult CoreIrIndVarSimplifyPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrAnalysisManager *analysis_manager =
        build_result->get_analysis_manager();
    if (analysis_manager == nullptr) {
        return PassResult::Failure(
            "missing core ir indvar simplify dependencies");
    }

    CoreIrPassEffects effects;
    CoreIrContext *core_ir_context = build_result->get_context();
    if (core_ir_context == nullptr) {
        return PassResult::Failure("missing core ir indvar simplify context");
    }
    for (const auto &function : module->get_functions()) {
        const CoreIrDominatorTreeAnalysisResult &dom_tree =
            analysis_manager->get_or_compute<CoreIrDominatorTreeAnalysis>(
                *function);
        const CoreIrLoopInfoAnalysisResult &loop_info =
            analysis_manager->get_or_compute<CoreIrLoopInfoAnalysis>(*function);
        const CoreIrInductionVarAnalysisResult &induction_vars =
            analysis_manager->get_or_compute<CoreIrInductionVarAnalysis>(
                *function);

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
                canonicalize_loop_compare(*iv) || function_changed;
            function_changed = strength_reduce_scaled_induction(
                                   *loop_ptr, *iv, *core_ir_context) ||
                               function_changed;
            function_changed = strength_reduce_row_bound_recurrence(
                                   *loop_ptr, *iv, dom_tree, *core_ir_context) ||
                               function_changed;
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
    effects.preserved_analyses.preserve(
        CoreIrAnalysisKind::PromotableStackSlot);
    effects.preserved_analyses.preserve_memory_family();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
