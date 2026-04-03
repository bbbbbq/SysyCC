#include "complier.hpp"

#include <memory>
#include <utility>

#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
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
    context_.set_dump_tokens(option_.dump_tokens());
    context_.set_dump_parse(option_.dump_parse());
    context_.set_dump_ast(option_.dump_ast());
    context_.set_dump_ir(option_.dump_ir());
    context_.set_stop_after_stage(option_.get_stop_after_stage());
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
    pass_manager_.AddPass(std::make_unique<CoreIrConstFoldPass>());
    pass_manager_.AddPass(std::make_unique<CoreIrDcePass>());
    pass_manager_.AddPass(std::make_unique<LowerIrPass>());
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

PassResult Complier::Run() {
    context_.clear_diagnostic_engine();
    context_.clear_core_ir_build_result();
    context_.clear_ir_result();
    context_.set_ir_dump_file_path("");
    sync_context_from_option();
    for (auto &dialect : extra_dialects_) {
        context_.get_dialect_manager().register_dialect(std::move(dialect));
    }
    extra_dialects_.clear();
    PassResult dialect_validation_result = validate_dialect_configuration();
    if (!dialect_validation_result.ok) {
        return dialect_validation_result;
    }
    InitializePasses();
    return pass_manager_.Run(context_);
}

} // namespace sysycc
