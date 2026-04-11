#include "backend/ir/tail_recursion_elimination/core_ir_tail_recursion_elimination_pass.hpp"

#include <memory>
#include <string>
#include <vector>

#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/detail/core_ir_rewrite_utils.hpp"
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

struct TailCallSite {
    CoreIrBasicBlock *block = nullptr;
    CoreIrCallInst *call = nullptr;
    CoreIrReturnInst *ret = nullptr;
};

void replace_value_uses(CoreIrValue &value, CoreIrValue *replacement) {
    const std::vector<CoreIrUse> uses = value.get_uses();
    for (const CoreIrUse &use : uses) {
        if (use.get_user() != nullptr) {
            use.get_user()->set_operand(use.get_operand_index(), replacement);
        }
    }
}

bool block_starts_with_phi(const CoreIrBasicBlock &block) {
    if (block.get_instructions().empty()) {
        return false;
    }
    return dynamic_cast<CoreIrPhiInst *>(block.get_instructions().front().get()) !=
           nullptr;
}

bool return_matches_tail_call(const CoreIrCallInst &call,
                              const CoreIrReturnInst &ret) {
    if (call.get_type() == nullptr) {
        return false;
    }
    if (call.get_type()->get_kind() == CoreIrTypeKind::Void) {
        return ret.get_return_value() == nullptr;
    }
    return ret.get_return_value() == &call;
}

bool collect_tail_call_sites(CoreIrFunction &function,
                             std::vector<TailCallSite> &sites) {
    std::vector<CoreIrCallInst *> tail_calls;
    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr || block->get_instructions().size() < 2) {
            continue;
        }
        auto &instructions = block->get_instructions();
        auto *ret = dynamic_cast<CoreIrReturnInst *>(instructions.back().get());
        auto *call =
            dynamic_cast<CoreIrCallInst *>(instructions[instructions.size() - 2].get());
        if (ret == nullptr || call == nullptr || !call->get_is_direct_call() ||
            call->get_callee_name() != function.get_name() ||
            call->get_argument_count() != function.get_parameters().size() ||
            !return_matches_tail_call(*call, *ret)) {
            continue;
        }
        sites.push_back(TailCallSite{block, call, ret});
        tail_calls.push_back(call);
    }

    if (sites.empty()) {
        return false;
    }

    for (const auto &block_ptr : function.get_basic_blocks()) {
        CoreIrBasicBlock *block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }
        for (const auto &instruction_ptr : block->get_instructions()) {
            auto *call = dynamic_cast<CoreIrCallInst *>(instruction_ptr.get());
            if (call == nullptr || !call->get_is_direct_call() ||
                call->get_callee_name() != function.get_name()) {
                continue;
            }
            bool is_tail_call = false;
            for (CoreIrCallInst *tail_call : tail_calls) {
                if (tail_call == call) {
                    is_tail_call = true;
                    break;
                }
            }
            if (!is_tail_call) {
                return false;
            }
        }
    }

    return true;
}

bool eliminate_tail_recursion(CoreIrFunction &function) {
    auto &basic_blocks = function.get_basic_blocks();
    CoreIrBasicBlock *header =
        basic_blocks.empty() ? nullptr : basic_blocks.front().get();
    if (header == nullptr || block_starts_with_phi(*header)) {
        return false;
    }

    std::vector<TailCallSite> tail_call_sites;
    if (!collect_tail_call_sites(function, tail_call_sites)) {
        return false;
    }

    const CoreIrType *void_type =
        tail_call_sites.front().ret == nullptr ? nullptr
                                               : tail_call_sites.front().ret->get_type();
    if (void_type == nullptr) {
        return false;
    }

    auto entry = std::make_unique<CoreIrBasicBlock>(
        function.get_name() + ".tailrecurse.entry");
    entry->set_parent(&function);
    CoreIrBasicBlock *entry_block = entry.get();
    entry_block->append_instruction(
        std::make_unique<CoreIrJumpInst>(void_type, header));
    basic_blocks.insert(basic_blocks.begin(), std::move(entry));

    auto backedge = std::make_unique<CoreIrBasicBlock>(
        function.get_name() + ".tailrecurse.backedge");
    backedge->set_parent(&function);
    CoreIrBasicBlock *backedge_block = backedge.get();
    basic_blocks.insert(basic_blocks.begin() + 1, std::move(backedge));

    std::vector<CoreIrPhiInst *> parameter_phis;
    std::vector<CoreIrPhiInst *> backedge_phis;
    parameter_phis.reserve(function.get_parameters().size());
    backedge_phis.reserve(function.get_parameters().size());
    for (std::size_t index = 0; index < function.get_parameters().size(); ++index) {
        CoreIrParameter *parameter = function.get_parameters()[index].get();
        if (parameter == nullptr || parameter->get_type() == nullptr) {
            return false;
        }
        std::string phi_name = parameter->get_name().empty()
                                   ? "arg" + std::to_string(index) + ".tr"
                                   : parameter->get_name() + ".tr";
        auto phi = std::make_unique<CoreIrPhiInst>(parameter->get_type(), phi_name);
        CoreIrPhiInst *phi_ptr = static_cast<CoreIrPhiInst *>(
            header->insert_instruction_before_first_non_phi(std::move(phi)));
        if (phi_ptr == nullptr) {
            return false;
        }
        replace_value_uses(*parameter, phi_ptr);
        phi_ptr->add_incoming(entry_block, parameter);
        parameter_phis.push_back(phi_ptr);

        std::string backedge_phi_name =
            parameter->get_name().empty()
                ? "arg" + std::to_string(index) + ".tr.next"
                : parameter->get_name() + ".tr.next";
        auto backedge_phi =
            std::make_unique<CoreIrPhiInst>(parameter->get_type(),
                                            backedge_phi_name);
        CoreIrPhiInst *backedge_phi_ptr =
            static_cast<CoreIrPhiInst *>(backedge_block->append_instruction(
                std::move(backedge_phi)));
        if (backedge_phi_ptr == nullptr) {
            return false;
        }
        phi_ptr->add_incoming(backedge_block, backedge_phi_ptr);
        backedge_phis.push_back(backedge_phi_ptr);
    }

    bool changed = false;
    for (const TailCallSite &site : tail_call_sites) {
        if (site.block == nullptr || site.call == nullptr || site.ret == nullptr) {
            return false;
        }
        for (std::size_t index = 0; index < backedge_phis.size(); ++index) {
            backedge_phis[index]->add_incoming(site.block,
                                               site.call->get_argument(index));
        }
        if (!erase_instruction(*site.block, site.ret) ||
            !erase_instruction(*site.block, site.call)) {
            return false;
        }
        site.block->append_instruction(
            std::make_unique<CoreIrJumpInst>(void_type, backedge_block));
        changed = true;
    }

    backedge_block->append_instruction(
        std::make_unique<CoreIrJumpInst>(void_type, header));

    if (changed) {
        function.set_is_norecurse(true);
    }
    return changed;
}

} // namespace

PassKind CoreIrTailRecursionEliminationPass::Kind() const {
    return PassKind::CoreIrTailRecursionElimination;
}

const char *CoreIrTailRecursionEliminationPass::Name() const {
    return "CoreIrTailRecursionEliminationPass";
}

PassResult CoreIrTailRecursionEliminationPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    CoreIrModule *module =
        build_result == nullptr ? nullptr : build_result->get_module();
    if (module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        if (function == nullptr || !eliminate_tail_recursion(*function)) {
            continue;
        }
        effects.changed_functions.insert(function.get());
        effects.cfg_changed_functions.insert(function.get());
    }

    if (!effects.has_changes()) {
        effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
        return PassResult::Success(std::move(effects));
    }

    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_none();
    return PassResult::Success(std::move(effects));
}

} // namespace sysycc
