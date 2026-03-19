#include "complier.hpp"

#include <memory>
#include <utility>

#include "frontend/ast/ast_pass.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/preprocess/preprocess.hpp"
#include "frontend/semantic/semantic_pass.hpp"

namespace sysycc {

Complier::Complier(ComplierOption option) : option_(std::move(option)) {
    context_.set_input_file(option_.get_input_file());
    context_.set_include_directories(option_.get_include_directories());
    context_.set_dump_tokens(option_.dump_tokens());
    context_.set_dump_parse(option_.dump_parse());
    context_.set_dump_ast(option_.dump_ast());
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
    pipeline_initialized_ = true;
}

void Complier::set_option(ComplierOption option) {
    option_ = std::move(option);
    context_.set_input_file(option_.get_input_file());
    context_.set_include_directories(option_.get_include_directories());
    context_.set_dump_tokens(option_.dump_tokens());
    context_.set_dump_parse(option_.dump_parse());
    context_.set_dump_ast(option_.dump_ast());
}

const ComplierOption &Complier::get_option() const noexcept { return option_; }

CompilerContext &Complier::get_context() noexcept { return context_; }

const CompilerContext &Complier::get_context() const noexcept {
    return context_;
}

void Complier::AddPass(std::unique_ptr<Pass> pass) {
    pass_manager_.AddPass(std::move(pass));
}

PassResult Complier::Run() {
    context_.set_input_file(option_.get_input_file());
    context_.set_include_directories(option_.get_include_directories());
    context_.set_dump_tokens(option_.dump_tokens());
    context_.set_dump_parse(option_.dump_parse());
    context_.set_dump_ast(option_.dump_ast());
    InitializePasses();
    return pass_manager_.Run(context_);
}

} // namespace sysycc
