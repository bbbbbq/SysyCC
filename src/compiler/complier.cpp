#include "complier.hpp"

#include <dlfcn.h>
#include <memory>
#include <utility>

#include "backend/asm_gen/aarch64/aarch64_asm_gen_pass.hpp"
#include "backend/asm_gen/backend_kind.hpp"
#include "backend/asm_gen/riscv64/riscv64_asm_gen_pass.hpp"
#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"
#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/gvn/core_ir_gvn_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/licm/core_ir_licm_pass.hpp"
#include "backend/ir/local_cse/core_ir_local_cse_pass.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/pipeline/core_ir_pass_pipeline.hpp"
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

namespace {

bool is_native_backend(BackendKind backend_kind) {
    return backend_kind == BackendKind::AArch64Native ||
           backend_kind == BackendKind::Riscv64Native;
}

const char *backend_kind_name(BackendKind backend_kind) {
    switch (backend_kind) {
    case BackendKind::LlvmIr:
        return "llvm-ir";
    case BackendKind::AArch64Native:
        return "aarch64-native";
    case BackendKind::Riscv64Native:
        return "riscv64-native";
    }
    return "unknown";
}

const char *expected_target_triple(BackendKind backend_kind) {
    switch (backend_kind) {
    case BackendKind::AArch64Native:
        return "aarch64-unknown-linux-gnu";
    case BackendKind::Riscv64Native:
        return "riscv64-unknown-linux-gnu";
    case BackendKind::LlvmIr:
        return "";
    }
    return "";
}

std::unique_ptr<Pass> try_create_riscv64_asm_gen_pass() {
    using CreatePassFn = Pass *(*)();
    void *symbol = dlsym(RTLD_DEFAULT, "sysycc_create_riscv64_asm_gen_pass");
    if (symbol == nullptr) {
        return nullptr;
    }
    CreatePassFn create_pass =
        reinterpret_cast<CreatePassFn>(symbol);
    return std::unique_ptr<Pass>(create_pass());
}

} // namespace

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
    context_.set_emit_object(option_.emit_object());
    context_.set_stop_after_stage(option_.get_stop_after_stage());
    context_.set_optimization_level(option_.get_optimization_level());
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

    const BackendKind backend_kind =
        context_.get_backend_options().get_backend_kind();
    const StopAfterStage stop_after_stage = context_.get_stop_after_stage();

    pass_manager_.AddPass(std::make_unique<PreprocessPass>());
    pass_manager_.AddPass(std::make_unique<LexerPass>());
    pass_manager_.AddPass(std::make_unique<ParserPass>());
    pass_manager_.AddPass(std::make_unique<AstPass>());
    pass_manager_.AddPass(std::make_unique<SemanticPass>());
    append_default_core_ir_pipeline(pass_manager_, backend_kind);
    if (!(is_native_backend(backend_kind) &&
          stop_after_stage == StopAfterStage::CoreIr)) {
        pass_manager_.AddPass(std::make_unique<AArch64AsmGenPass>());
        if (std::unique_ptr<Pass> riscv64_pass =
                try_create_riscv64_asm_gen_pass();
            riscv64_pass != nullptr) {
            pass_manager_.AddPass(std::move(riscv64_pass));
        }
    }
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

    if (is_native_backend(backend_kind)) {
        if (!context_.get_emit_asm()) {
            if (!context_.get_emit_object()) {
                const std::string message =
                    std::string("--backend=") + backend_kind_name(backend_kind) +
                    " currently requires -S or -c";
                context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                           message);
                return PassResult::Failure(message);
            }
        }
        if (context_.get_emit_asm() && context_.get_emit_object()) {
            const std::string message =
                std::string("--backend=") + backend_kind_name(backend_kind) +
                " cannot emit asm and object at the same time";
            context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        if (!target_triple.empty() &&
            target_triple != expected_target_triple(backend_kind)) {
            const std::string message =
                "unsupported " + std::string(backend_kind_name(backend_kind)) +
                " target triple: " + target_triple;
            context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        if (context_.get_stop_after_stage() == StopAfterStage::IR) {
            const std::string message =
                std::string("--stop-after=ir is incompatible with --backend=") +
                backend_kind_name(backend_kind);
            context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        if (context_.get_dump_ir()) {
            const std::string message =
                std::string("--dump-ir is incompatible with --backend=") +
                backend_kind_name(backend_kind);
            context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                       message);
            return PassResult::Failure(message);
        }
        return PassResult::Success();
    }

    if (context_.get_emit_object()) {
        const std::string message =
            "-c currently requires --backend=aarch64-native or --backend=riscv64-native";
        context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }

    if (context_.get_emit_asm()) {
        const std::string message =
            "-S currently requires --backend=aarch64-native or --backend=riscv64-native";
        context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }
    if (!target_triple.empty()) {
        const std::string message =
            "--target is only supported with --backend=aarch64-native or --backend=riscv64-native";
        context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }
    if (context_.get_stop_after_stage() == StopAfterStage::Asm) {
        const std::string message =
            "--stop-after=asm requires --backend=aarch64-native or --backend=riscv64-native together with -S";
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
            "linking is not supported yet; use -E, -fsyntax-only, -c, -S, or -S -emit-llvm";
        context_.get_diagnostic_engine().add_error(DiagnosticStage::Compiler,
                                                   message);
        return PassResult::Failure(message);
    }
    case DriverAction::CompileOnly:
        return PassResult::Success();
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
    context_.clear_object_result();
    context_.set_core_ir_dump_file_path("");
    context_.set_ir_dump_file_path("");
    context_.set_llvm_ir_text_artifact_file_path("");
    context_.set_llvm_ir_bitcode_artifact_file_path("");
    context_.set_asm_dump_file_path("");
    context_.set_object_dump_file_path("");
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
