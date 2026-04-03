#include <cassert>
#include <string>
#include <vector>

#include "backend/ir/ir_pass.hpp"
#include "compiler/complier.hpp"
#include "frontend/ast/ast_pass.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/preprocess/preprocess.hpp"
#include "frontend/semantic/semantic_pass.hpp"

using namespace sysycc;

namespace sysycc {

PassKind PreprocessPass::Kind() const { return PassKind::Preprocess; }

const char *PreprocessPass::Name() const { return "PreprocessPass"; }

PassResult PreprocessPass::Run(CompilerContext &) { return PassResult::Success(); }

PassKind LexerPass::Kind() const { return PassKind::Lex; }

const char *LexerPass::Name() const { return "LexerPass"; }

PassResult LexerPass::Run(CompilerContext &) { return PassResult::Success(); }

PassKind ParserPass::Kind() const { return PassKind::Parse; }

const char *ParserPass::Name() const { return "ParserPass"; }

PassResult ParserPass::Run(CompilerContext &) { return PassResult::Success(); }

PassKind AstPass::Kind() const { return PassKind::Ast; }

const char *AstPass::Name() const { return "AstPass"; }

PassResult AstPass::Run(CompilerContext &) { return PassResult::Success(); }

PassKind SemanticPass::Kind() const { return PassKind::Semantic; }

const char *SemanticPass::Name() const { return "SemanticPass"; }

PassResult SemanticPass::Run(CompilerContext &) { return PassResult::Success(); }

PassKind IRGenPass::Kind() const { return PassKind::IRGen; }

const char *IRGenPass::Name() const { return "IRGenPass"; }

PassResult IRGenPass::Run(CompilerContext &) { return PassResult::Success(); }

} // namespace sysycc

namespace {

ComplierOption make_option(std::string input_file,
                           std::vector<std::string> include_directories,
                           std::vector<std::string> system_include_directories,
                           bool dump_tokens, bool dump_parse, bool dump_ast,
                           bool dump_ir, StopAfterStage stop_after_stage,
                           bool enable_gnu_dialect,
                           bool enable_clang_dialect,
                           bool enable_builtin_type_extension_pack) {
    ComplierOption option(std::move(input_file));
    option.set_include_directories(std::move(include_directories));
    option.set_system_include_directories(std::move(system_include_directories));
    option.set_dump_tokens(dump_tokens);
    option.set_dump_parse(dump_parse);
    option.set_dump_ast(dump_ast);
    option.set_dump_ir(dump_ir);
    option.set_stop_after_stage(stop_after_stage);
    option.set_enable_gnu_dialect(enable_gnu_dialect);
    option.set_enable_clang_dialect(enable_clang_dialect);
    option.set_enable_builtin_type_extension_pack(
        enable_builtin_type_extension_pack);
    return option;
}

void assert_context_matches(const CompilerContext &context,
                            const ComplierOption &option,
                            const std::vector<std::string> &dialect_names) {
    assert(context.get_input_file() == option.get_input_file());
    assert(context.get_include_directories() == option.get_include_directories());
    assert(context.get_system_include_directories() ==
           option.get_system_include_directories());
    assert(context.get_dump_tokens() == option.dump_tokens());
    assert(context.get_dump_parse() == option.dump_parse());
    assert(context.get_dump_ast() == option.dump_ast());
    assert(context.get_dump_ir() == option.dump_ir());
    assert(context.get_stop_after_stage() == option.get_stop_after_stage());
    assert(context.get_dialect_manager().get_dialect_names() == dialect_names);
}

} // namespace

int main() {
    const ComplierOption constructor_option =
        make_option("constructor_input.sy", {"include/constructor"},
                    {"system/constructor"}, true, false, true, false,
                    StopAfterStage::Parse, false, false, false);
    Complier complier(constructor_option);
    assert_context_matches(complier.get_context(), constructor_option, {"c99"});

    const ComplierOption assigned_option =
        make_option("assigned_input.sy", {"include/assigned"},
                    {"system/assigned"}, false, true, false, true,
                    StopAfterStage::Semantic, true, false, true);
    complier.set_option(assigned_option);
    assert_context_matches(complier.get_context(), assigned_option,
                           {"c99", "gnu-c", "extended-builtin-types"});

    const PassResult run_result = complier.Run();
    assert(run_result.ok);
    assert_context_matches(complier.get_context(), assigned_option,
                           {"c99", "gnu-c", "extended-builtin-types"});

    return 0;
}
