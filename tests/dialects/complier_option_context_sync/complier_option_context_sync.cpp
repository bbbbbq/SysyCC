#include <cassert>
#include <string>
#include <vector>

#include "backend/ir/analysis/analysis_manager.hpp"
#include "backend/ir/analysis/cfg_analysis.hpp"
#include "backend/ir/build/build_core_ir_pass.hpp"
#include "backend/asm_gen/aarch64/aarch64_asm_gen_pass.hpp"
#include "backend/ir/canonicalize/core_ir_canonicalize_pass.hpp"
#include "backend/ir/copy_propagation/core_ir_copy_propagation_pass.hpp"
#include "backend/ir/const_fold/core_ir_const_fold_pass.hpp"
#include "backend/ir/dead_store_elimination/core_ir_dead_store_elimination_pass.hpp"
#include "backend/ir/dce/core_ir_dce_pass.hpp"
#include "backend/ir/gvn/core_ir_gvn_pass.hpp"
#include "backend/ir/instcombine/core_ir_instcombine_pass.hpp"
#include "backend/ir/licm/core_ir_licm_pass.hpp"
#include "backend/ir/local_cse/core_ir_local_cse_pass.hpp"
#include "backend/ir/lower/lower_ir_pass.hpp"
#include "backend/ir/loop_simplify/core_ir_loop_simplify_pass.hpp"
#include "backend/ir/mem2reg/core_ir_mem2reg_pass.hpp"
#include "backend/ir/pipeline/core_ir_pass_pipeline.hpp"
#include "backend/ir/sccp/core_ir_sccp_pass.hpp"
#include "backend/ir/simplify_cfg/core_ir_simplify_cfg_pass.hpp"
#include "backend/ir/stack_slot_forward/core_ir_stack_slot_forward_pass.hpp"
#include "backend/ir/verify/core_ir_verifier.hpp"
#include "compiler/complier.hpp"
#include "frontend/ast/ast_pass.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.hpp"
#include "frontend/preprocess/preprocess.hpp"
#include "frontend/semantic/semantic_pass.hpp"

using namespace sysycc;

namespace sysycc {

namespace {

PassResult no_op_core_ir_transform_result() {
    CoreIrPassEffects effects;
    effects.preserved_analyses = CoreIrPreservedAnalyses::preserve_all();
    return PassResult::Success(std::move(effects));
}

} // namespace

void PassManager::AddPass(std::unique_ptr<Pass> pass) {
    if (pass == nullptr) {
        return;
    }

    PipelineEntry entry;
    entry.pass = std::move(pass);
    entries_.push_back(std::move(entry));
}

Pass *PassManager::get_pass_by_kind(PassKind kind) const {
    for (const PipelineEntry &entry : entries_) {
        if (entry.pass != nullptr && entry.pass->Kind() == kind) {
            return entry.pass.get();
        }
    }
    return nullptr;
}

PassResult PassManager::Run(CompilerContext &) { return PassResult::Success(); }

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

CoreIrPassMetadata BuildCoreIrPass::Metadata() const noexcept {
    return CoreIrPassMetadata::core_ir_build();
}

PassResult BuildCoreIrPass::Run(CompilerContext &context) {
    auto ir_context = std::make_unique<CoreIrContext>();
    auto *module = ir_context->create_module<CoreIrModule>("stub");
    context.set_core_ir_build_result(
        std::make_unique<CoreIrBuildResult>(std::move(ir_context), module));
    return PassResult::Success();
}

CoreIrBuildResult::CoreIrBuildResult(std::unique_ptr<CoreIrContext> context,
                                     CoreIrModule *module) noexcept
    : context_(std::move(context)), module_(module),
      analysis_manager_(std::make_unique<CoreIrAnalysisManager>()) {}

const CoreIrContext *CoreIrBuildResult::get_context() const noexcept {
    return context_.get();
}

CoreIrContext *CoreIrBuildResult::get_context() noexcept {
    return context_.get();
}

const CoreIrModule *CoreIrBuildResult::get_module() const noexcept { return module_; }

CoreIrModule *CoreIrBuildResult::get_module() noexcept { return module_; }

const CoreIrAnalysisManager *
CoreIrBuildResult::get_analysis_manager() const noexcept {
    return analysis_manager_.get();
}

CoreIrAnalysisManager *CoreIrBuildResult::get_analysis_manager() noexcept {
    return analysis_manager_.get();
}

void CoreIrBuildResult::invalidate_core_ir_analyses(CoreIrFunction &) noexcept {}

void CoreIrBuildResult::invalidate_all_core_ir_analyses() noexcept {}

// Test-only compatibility stubs: this dialect sync fixture does not exercise
// the real analysis cache, but it must stay link-complete as the surface grows.
void CoreIrAnalysisManager::invalidate_all() noexcept {}

void CoreIrAnalysisManager::invalidate(CoreIrAnalysisKind) noexcept {}

void CoreIrAnalysisManager::invalidate(CoreIrFunction &,
                                       CoreIrAnalysisKind) noexcept {}

CoreIrCfgAnalysisResult CoreIrCfgAnalysis::Run(const CoreIrFunction &) const {
    return {};
}

const std::vector<CoreIrBasicBlock *> &
CoreIrCfgAnalysisResult::get_predecessors(const CoreIrBasicBlock *) const {
    static const std::vector<CoreIrBasicBlock *> empty;
    return empty;
}

CoreIrVerifyResult CoreIrVerifier::verify_module(const CoreIrModule &) const {
    return {};
}

CoreIrVerifyResult CoreIrVerifier::verify_function(
    const CoreIrFunction &, const CoreIrCfgAnalysisResult *) const {
    return {};
}

bool emit_core_ir_verify_result(CompilerContext &, const CoreIrVerifyResult &,
                                const char *) {
    return true;
}

void append_default_core_ir_pipeline(PassManager &pass_manager) {
    pass_manager.AddPass(std::make_unique<BuildCoreIrPass>());
}

void append_default_core_ir_pipeline(PassManager &pass_manager, BackendKind) {
    append_default_core_ir_pipeline(pass_manager);
}

PassKind CoreIrCanonicalizePass::Kind() const {
    return PassKind::CoreIrCanonicalize;
}

const char *CoreIrCanonicalizePass::Name() const {
    return "CoreIrCanonicalizePass";
}

PassResult CoreIrCanonicalizePass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrConstFoldPass::Kind() const {
    return PassKind::CoreIrConstFold;
}

const char *CoreIrConstFoldPass::Name() const {
    return "CoreIrConstFoldPass";
}

PassResult CoreIrConstFoldPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrDcePass::Kind() const { return PassKind::CoreIrDce; }

const char *CoreIrDcePass::Name() const { return "CoreIrDcePass"; }

PassResult CoreIrDcePass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrSimplifyCfgPass::Kind() const {
    return PassKind::CoreIrSimplifyCfg;
}

const char *CoreIrSimplifyCfgPass::Name() const {
    return "CoreIrSimplifyCfgPass";
}

PassResult CoreIrSimplifyCfgPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrLoopSimplifyPass::Kind() const {
    return PassKind::CoreIrLoopSimplify;
}

const char *CoreIrLoopSimplifyPass::Name() const {
    return "CoreIrLoopSimplifyPass";
}

PassResult CoreIrLoopSimplifyPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrStackSlotForwardPass::Kind() const {
    return PassKind::CoreIrStackSlotForward;
}

const char *CoreIrStackSlotForwardPass::Name() const {
    return "CoreIrStackSlotForwardPass";
}

PassResult CoreIrStackSlotForwardPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrCopyPropagationPass::Kind() const {
    return PassKind::CoreIrCopyPropagation;
}

const char *CoreIrCopyPropagationPass::Name() const {
    return "CoreIrCopyPropagationPass";
}

PassResult CoreIrCopyPropagationPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrSccpPass::Kind() const { return PassKind::CoreIrSccp; }

const char *CoreIrSccpPass::Name() const { return "CoreIrSccpPass"; }

PassResult CoreIrSccpPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrInstCombinePass::Kind() const {
    return PassKind::CoreIrInstCombine;
}

const char *CoreIrInstCombinePass::Name() const {
    return "CoreIrInstCombinePass";
}

PassResult CoreIrInstCombinePass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrLicmPass::Kind() const { return PassKind::CoreIrLicm; }

const char *CoreIrLicmPass::Name() const { return "CoreIrLicmPass"; }

PassResult CoreIrLicmPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrLocalCsePass::Kind() const {
    return PassKind::CoreIrLocalCse;
}

const char *CoreIrLocalCsePass::Name() const {
    return "CoreIrLocalCsePass";
}

PassResult CoreIrLocalCsePass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrGvnPass::Kind() const { return PassKind::CoreIrGvn; }

const char *CoreIrGvnPass::Name() const { return "CoreIrGvnPass"; }

PassResult CoreIrGvnPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrDeadStoreEliminationPass::Kind() const {
    return PassKind::CoreIrDeadStoreElimination;
}

const char *CoreIrDeadStoreEliminationPass::Name() const {
    return "CoreIrDeadStoreEliminationPass";
}

PassResult CoreIrDeadStoreEliminationPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
}

PassKind CoreIrMem2RegPass::Kind() const { return PassKind::CoreIrMem2Reg; }

const char *CoreIrMem2RegPass::Name() const { return "CoreIrMem2RegPass"; }

PassResult CoreIrMem2RegPass::Run(CompilerContext &) {
    return no_op_core_ir_transform_result();
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
                           OptimizationLevel optimization_level,
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
    option.set_optimization_level(optimization_level);
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
    assert(context.get_optimization_level() == option.get_optimization_level());
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
                    false, StopAfterStage::Parse, OptimizationLevel::O0,
                    BackendKind::LlvmIr, "",
                    false, false, false);
    Complier complier(constructor_option);
    assert_context_matches(complier.get_context(), constructor_option, {"c99"});

    const ComplierOption assigned_option =
        make_option("assigned_input.sy", {"include/assigned"},
                    {"system/assigned"}, false, true, false, false, true,
                    true, StopAfterStage::Asm, OptimizationLevel::O1,
                    BackendKind::AArch64Native,
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
