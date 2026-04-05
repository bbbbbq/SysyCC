#include "complier.hpp"

#include <memory>
#include <utility>

#include "backend/asm_gen/aarch64/aarch64_asm_gen_pass.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"
#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/gvn/core_ir_gvn_pass.hpp"
#include "backend/ir/local_cse/core_ir_local_cse_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/sccp/core_ir_sccp_pass.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"
#include "backend/ir/lower/lower_ir_pass.hpp"
#include "frontend/ast/ast_pass.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/preprocess/preprocess.hpp"
#include "frontend/semantic/semantic_pass.hpp"

namespace sysycc {

Complier::Complier(ComplierOption option) : option_(std::move(option)) {
    sync_context_from_option();
}

void Complier::sync_context_from_option() {
    context_.set_input_file(option_.get_input_file());
    context_.set_include_directories(option_.get_include_directories());
    context_.set_system_include_directories(
        option_.get_system_include_directories());
    context_.set_command_line_macro_options(
        option_.get_command_line_macro_options());
    context_.set_forced_include_files(option_.get_forced_include_files());
    context_.set_no_stdinc(option_.get_no_stdinc());
    context_.set_dump_tokens(option_.dump_tokens());
    context_.set_dump_parse(option_.dump_parse());
    context_.set_dump_ast(option_.dump_ast());
    context_.set_dump_ir(option_.dump_ir());
    context_.set_dump_core_ir(option_.dump_core_ir());
    context_.set_emit_asm(option_.emit_asm());
    context_.set_stop_after_stage(option_.get_stop_after_stage());
    context_.set_backend_options(option_.get_backend_options());
    context_.get_diagnostic_engine().set_warning_policy(
        option_.get_warning_policy());
    context_.configure_dialects(option_.get_enable_gnu_dialect(),
                                option_.get_enable_clang_dialect(),
                                option_.get_enable_builtin_type_extension_pack());
}

void Complier::InitializePasses() {
    if (pipeline_initialized_) {
        return;
    }

    pass_manager_.AddPass(std::make_unique<PreprocessPass>());
    pass_manager_.AddPass(std::make_unique<LexerPass>());
    pass_manager_.AddPass(std::make_unique<ParserPass>());
    pass_manager_.AddPass(std::make_unique<AstPass>());
    pass_manager_.AddPass(std::make_unique<SemanticPass>());
    pass_manager_.AddPass(std::make_unique<BuildCoreIrPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrCanonicalizePass>());
    pass_manager_.AddPass(std::make_unique<CoreIrSimplifyCfgPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrStackSlotForwardPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrDeadStoreEliminationPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrMem2RegPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrCopyPropagationPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrSccpPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrLocalCsePass>());
    pass_manager_.AddPass(std::make_unique<CoreIrGvnPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrConstFoldPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrDcePass>());
    pass_manager_.AddPass(std::make_unique<LowerIrPass>());
    pass_manager_.AddPass(std::make_unique<AArch64AsmGenPass>());
    pipeline_initialized_ = true;
}

void Complier::set_option(ComplierOption option) {
    option_ = std::move(option);
    sync_context_from_option();
}

const ComplierOption &Complier::get_option() const noexcept { return option_; }

CompilerContext &Complier::get_context() noexcept { return context_; }

const CompilerContext &Complier::get_context() const noexcept {
    return context_;
}

void Complier::register_dialect(std::unique_ptr<FrontendDialect> dialect) {
    if (dialect == nullptr) {
        return;
    }
    extra_dialects_.push_back(std::move(dialect));
}

void Complier::AddPass(std::unique_ptr<Pass> pass) {
    pass_manager_.AddPass(std::move(pass));
}

PassResult Complier::validate_dialect_configuration() {
    const auto &registration_errors =
        context_.get_dialect_manager().get_registration_errors();
    if (registration_errors.empty()) {
        return PassResult::Success();
    }

    const std::string summary =
        "invalid dialect configuration: registration conflicts detected";
    context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                               summary);
    for (const std::string &registration_error : registration_errors) {
        context_.get_diagnostic_engine().add_note(DiagnosticStage::Compiler,
                                                  registration_error);
    }
    return PassResult::Failure(summary);
}

PassResult Complier::validate_backend_configuration() {
    const BackendOptions &backend_options = context_.get_backend_options();
    const BackendKind backend_kind = backend_options.get_backend_kind();
    const std::string &target_triple = backend_options.get_target_triple();

    if (backend_kind == BackendKind::AArch64Native) {
        if (context_.get_dump_ir()) {
            const std::string message =
                "--dump-ir is incompatible with --backend=aarch64-native";
            context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        if (!context_.get_emit_asm()) {
            const std::string message =
                "--backend=aarch64-native currently requires -S";
            context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        if (!target_triple.empty() &&
            target_triple != "aarch64-unknown-linux-gnu") {
            const std::string message =
                "unsupported AArch64 native target triple: " + target_triple;
            context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        if (context_.get_stop_after_stage() == StopAfterStage::IR) {
            const std::string message =
                "--stop-after=ir is incompatible with --backend=aarch64-native";
            context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        return PassResult::Success();
    }

    if (context_.get_emit_asm()) {
        const std::string message =
            "-S currently requires --backend=aarch64-native";
        context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }
    if (!target_triple.empty()) {
        const std::string message =
            "--target is only supported with --backend=aarch64-native";
        context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }
    if (context_.get_stop_after_stage() == StopAfterStage::Asm) {
        const std::string message =
            "--stop-after=asm requires --backend=aarch64-native and -S";
        context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }

    return PassResult::Success();
}

PassResult Complier::validate_driver_configuration() {
    switch (option_.get_driver_action()) {
    case DriverAction::InternalPipeline:
        return PassResult::Success();
    case DriverAction::FullCompile: {
        const std::string message =
            "linking is not supported yet; use -E, -fsyntax-only, -S, or -S -emit-llvm";
        context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }
    case DriverAction::CompileOnlyUnsupported: {
        const std::string message =
            "object emission is not supported yet; use -E, -fsyntax-only, -S, or -S -emit-llvm";
        context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }
    case DriverAction::PreprocessOnly:
    case DriverAction::SyntaxOnly:
    case DriverAction::EmitAssembly:
    case DriverAction::EmitLlvmIr:
        return PassResult::Success();
    }

    return PassResult::Success();
}

PassResult Complier::Run() {
    context_.clear_diagnostic_engine();
    context_.clear_core_ir_build_result();
    context_.clear_ir_result();
    context_.clear_asm_result();
    context_.set_core_ir_dump_file_path("");
    context_.set_ir_dump_file_path("");
    context_.set_asm_dump_file_path("");
    sync_context_from_option();
    for (auto &dialect : extra_dialects_) {
        context_.get_dialect_manager().register_dialect(std::move(dialect));
    }
    extra_dialects_.clear();
    PassResult driver_validation_result = validate_driver_configuration();
    if (!driver_validation_result.ok) {
        return driver_validation_result;
    }
    PassResult dialect_validation_result = validate_dialect_configuration();
    if (!dialect_validation_result.ok) {
        return dialect_validation_result;
    }
    PassResult backend_validation_result = validate_backend_configuration();
    if (!backend_validation_result.ok) {
        return backend_validation_result;
    }
    InitializePasses();
    return pass_manager_.Run(context_);
}

} // namespace sysycc
