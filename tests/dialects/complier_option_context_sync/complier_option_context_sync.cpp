#include <cassert>
#include <string>
#include <vector>

#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/asm_gen/aarch64/aarch64_asm_gen_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/lower/lower_ir_pass.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
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

PassKind BuildCoreIrPass::Kind() const { return PassKind::BuildCoreIr; }

const char *BuildCoreIrPass::Name() const { return "BuildCoreIrPass"; }

PassResult BuildCoreIrPass::Run(CompilerContext &) {
    return PassResult::Success();
}

PassKind CoreIrCanonicalizePass::Kind() const {
    return PassKind::CoreIrCanonicalize;
}

const char *CoreIrCanonicalizePass::Name() const {
    return "CoreIrCanonicalizePass";
}

PassResult CoreIrCanonicalizePass::Run(CompilerContext &) {
    return PassResult::Success();
}

PassKind CoreIrConstFoldPass::Kind() const {
    return PassKind::CoreIrConstFold;
}

const char *CoreIrConstFoldPass::Name() const {
    return "CoreIrConstFoldPass";
}

PassResult CoreIrConstFoldPass::Run(CompilerContext &) {
    return PassResult::Success();
}

PassKind CoreIrDcePass::Kind() const { return PassKind::CoreIrDce; }

const char *CoreIrDcePass::Name() const { return "CoreIrDcePass"; }

PassResult CoreIrDcePass::Run(CompilerContext &) {
    return PassResult::Success();
}

PassKind CoreIrSimplifyCfgPass::Kind() const {
    return PassKind::CoreIrSimplifyCfg;
}

const char *CoreIrSimplifyCfgPass::Name() const {
    return "CoreIrSimplifyCfgPass";
}

PassResult CoreIrSimplifyCfgPass::Run(CompilerContext &) {
    return PassResult::Success();
}

PassKind LowerIrPass::Kind() const { return PassKind::LowerIr; }

const char *LowerIrPass::Name() const { return "LowerIrPass"; }

PassResult LowerIrPass::Run(CompilerContext &) { return PassResult::Success(); }

PassKind AArch64AsmGenPass::Kind() const { return PassKind::CodeGen; }

const char *AArch64AsmGenPass::Name() const { return "AArch64AsmGenPass"; }

PassResult AArch64AsmGenPass::Run(CompilerContext &) {
    return PassResult::Success();
}

} // namespace sysycc

namespace {

ComplierOption make_option(std::string input_file,
                           std::vector<std::string> include_directories,
                           std::vector<std::string> system_include_directories,
                           bool dump_tokens, bool dump_parse, bool dump_ast,
                           bool dump_ir, bool dump_core_ir, bool emit_asm,
                           StopAfterStage stop_after_stage,
                           BackendKind backend_kind, std::string target_triple,
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
    option.set_dump_core_ir(dump_core_ir);
    option.set_emit_asm(emit_asm);
    option.set_stop_after_stage(stop_after_stage);
    option.set_enable_gnu_dialect(enable_gnu_dialect);
    option.set_enable_clang_dialect(enable_clang_dialect);
    option.set_enable_builtin_type_extension_pack(
        enable_builtin_type_extension_pack);
    BackendOptions backend_options;
    backend_options.set_backend_kind(backend_kind);
    backend_options.set_target_triple(std::move(target_triple));
    option.set_backend_options(std::move(backend_options));
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
    assert(context.get_dump_core_ir() == option.dump_core_ir());
    assert(context.get_emit_asm() == option.emit_asm());
    assert(context.get_stop_after_stage() == option.get_stop_after_stage());
    assert(context.get_backend_options().get_backend_kind() ==
           option.get_backend_options().get_backend_kind());
    assert(context.get_backend_options().get_target_triple() ==
           option.get_backend_options().get_target_triple());
    assert(context.get_backend_options().get_output_file() ==
           option.get_backend_options().get_output_file());
    assert(context.get_dialect_manager().get_dialect_names() == dialect_names);
}

} // namespace

int main() {
    const ComplierOption constructor_option =
        make_option("constructor_input.sy", {"include/constructor"},
                    {"system/constructor"}, true, false, true, false, false,
                    false, StopAfterStage::Parse, BackendKind::LlvmIr, "",
                    false, false, false);
    Complier complier(constructor_option);
    assert_context_matches(complier.get_context(), constructor_option, {"c99"});

    const ComplierOption assigned_option =
        make_option("assigned_input.sy", {"include/assigned"},
                    {"system/assigned"}, false, true, false, false, true,
                    true, StopAfterStage::Asm, BackendKind::AArch64Native,
                    "aarch64-unknown-linux-gnu", true, false, true);
    complier.set_option(assigned_option);
    assert_context_matches(complier.get_context(), assigned_option,
                           {"c99", "gnu-c", "extended-builtin-types"});

    const PassResult run_result = complier.Run();
    assert(run_result.ok);
    assert_context_matches(complier.get_context(), assigned_option,
                           {"c99", "gnu-c", "extended-builtin-types"});

    return 0;
}
