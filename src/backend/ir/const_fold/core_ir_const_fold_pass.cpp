#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"

#include <memory>

#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/core/ir_basic_block.hpp"
#include "backend/ir/shared/core/ir_constant.hpp"
#include "backend/ir/shared/core/ir_context.hpp"
#include "backend/ir/shared/core/ir_function.hpp"
#include "backend/ir/shared/core/ir_instruction.hpp"
#include "backend/ir/shared/core/ir_module.hpp"
#include "backend/ir/shared/core/ir_type.hpp"
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

const CoreIrConstantInt *as_int_constant(const CoreIrValue *value) {
    return dynamic_cast<const CoreIrConstantInt *>(value);
}

void remove_phi_incoming_from_predecessor(CoreIrBasicBlock *successor,
                                          CoreIrBasicBlock *predecessor) {
    if (successor == nullptr || predecessor == nullptr) {
        return;
    }
    for (const auto &instruction : successor->get_instructions()) {
        auto *phi = dynamic_cast<CoreIrPhiInst *>(instruction.get());
        if (phi == nullptr) {
            break;
        }
        phi->remove_incoming_block(predecessor);
    }
}

bool simplify_constant_conditional_branch(CoreIrBasicBlock &block,
                                          const CoreIrType *void_type) {
    auto &instructions = block.get_instructions();
    if (instructions.empty()) {
        return false;
    }
    auto *cond_jump =
        dynamic_cast<CoreIrCondJumpInst *>(instructions.back().get());
    if (cond_jump == nullptr) {
        return false;
    }
    const auto *condition = as_int_constant(cond_jump->get_condition());
    if (condition == nullptr) {
        return false;
    }

    CoreIrBasicBlock *target =
        condition->get_value() != 0 ? cond_jump->get_true_block()
                                    : cond_jump->get_false_block();
    CoreIrBasicBlock *removed_successor =
        condition->get_value() != 0 ? cond_jump->get_false_block()
                                    : cond_jump->get_true_block();
    remove_phi_incoming_from_predecessor(removed_successor, &block);
    cond_jump->detach_operands();
    auto replacement = std::make_unique<CoreIrJumpInst>(void_type, target);
    replacement->set_parent(&block);
    instructions.back() = std::move(replacement);
    return true;
}

} // namespace

PassKind CoreIrConstFoldPass::Kind() const {
    return PassKind::CoreIrConstFold;
}

const char *CoreIrConstFoldPass::Name() const {
    return "CoreIrConstFoldPass";
}

PassResult CoreIrConstFoldPass::Run(CompilerContext &context) {
    CoreIrBuildResult *build_result = context.get_core_ir_build_result();
    if (build_result == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    CoreIrContext *core_ir_context = build_result->get_context();
    CoreIrModule *module = build_result->get_module();
    if (core_ir_context == nullptr || module == nullptr) {
        return fail_missing_core_ir(context, Name());
    }

    const auto *void_type = core_ir_context->create_type<CoreIrVoidType>();
    CoreIrPassEffects effects;
    for (const auto &function : module->get_functions()) {
        bool function_changed = false;
        bool cfg_changed = false;
        for (const auto &block : function->get_basic_blocks()) {
            cfg_changed =
                simplify_constant_conditional_branch(*block, void_type) ||
                cfg_changed;
            function_changed = cfg_changed || function_changed;
        }
        if (function_changed) {
            effects.changed_functions.insert(function.get());
        }
        if (cfg_changed) {
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
