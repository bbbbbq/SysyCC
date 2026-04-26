#pragma once
#include <stdint.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backend/asm_gen/asm_result.hpp"
#include "backend/asm_gen/backend_options.hpp"
#include "backend/asm_gen/object_result.hpp"
#include "backend/ir/shared/core/core_ir_builder.hpp"
#include "backend/ir/shared/ir_result.hpp"
#include "common/diagnostic/diagnostic_engine.hpp"
#include "common/source_line_map.hpp"
#include "common/source_location_service.hpp"
#include "common/source_mapping_view.hpp"
#include "common/source_manager.hpp"
#include "common/source_span.hpp"
#include "compiler/complier_option.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/dialects/core/dialect_manager.hpp"
#include "frontend/dialects/packs/c99/c99_dialect.hpp"
#include "frontend/dialects/packs/gnu/gnu_dialect.hpp"
#include "frontend/dialects/packs/clang/clang_dialect.hpp"
#include "frontend/dialects/packs/builtin_types/builtin_type_extension_pack.hpp"
#include "frontend/parser/parser_runtime.hpp"
#include "frontend/semantic/model/semantic_model.hpp"

namespace sysycc {

enum class TokenKind : uint8_t {
    Identifier,
    AnnotationIdentifier,
    IntLiteral,
    FloatLiteral,
    CharLiteral,
    StringLiteral,
    KwConst,
    KwVolatile,
    KwExtern,
    KwStatic,
    KwAttribute,
    KwAsm,
    KwInline,
    KwRestrict,
    KwNullability,
    KwLong,
    KwSigned,
    KwShort,
    KwUnsigned,
    KwInt,
    KwChar,
    KwVoid,
    KwFloat,
    KwDouble,
    KwFloat16,
    KwIf,
    KwElse,
    KwWhile,
    KwFor,
    KwDo,
    KwSwitch,
    KwCase,
    KwDefault,
    KwBreak,
    KwContinue,
    KwGoto,
    KwReturn,
    KwStruct,
    KwUnion,
    KwEnum,
    KwTypedef,
    KwSizeof,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Increment,
    Decrement,
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    ShiftLeft,
    ShiftRight,
    Arrow,
    Assign,
    AddAssign,
    SubAssign,
    MulAssign,
    DivAssign,
    ModAssign,
    ShlAssign,
    ShrAssign,
    AndAssign,
    XorAssign,
    OrAssign,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LogicalNot,
    LogicalAnd,
    LogicalOr,
    Ellipsis,
    Question,
    Semicolon,
    Comma,
    Colon,
    Dot,
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    EndOfFile,
    Invalid,
};

enum class TokenCategory : uint8_t {
    Identifier,
    Keyword,
    Literal,
    Operator,
    Punctuation,
    Special,
};

// Represents one token produced by lexical analysis.
class Token {
  public:
    Token(TokenKind kind, std::string text, SourceSpan source_span = {})
        : kind_(kind), text_(std::move(text)),
          source_span_(source_span) {}

    TokenKind get_kind() const noexcept { return kind_; }

    const std::string &get_text() const noexcept { return text_; }

    const SourceSpan &get_source_span() const noexcept { return source_span_; }

    TokenCategory get_category() const noexcept {
        switch (kind_) {
        case TokenKind::Identifier:
        case TokenKind::AnnotationIdentifier:
            return TokenCategory::Identifier;
        case TokenKind::IntLiteral:
        case TokenKind::FloatLiteral:
        case TokenKind::CharLiteral:
        case TokenKind::StringLiteral:
            return TokenCategory::Literal;
        case TokenKind::KwConst:
        case TokenKind::KwVolatile:
        case TokenKind::KwExtern:
        case TokenKind::KwStatic:
        case TokenKind::KwAttribute:
        case TokenKind::KwAsm:
        case TokenKind::KwInline:
        case TokenKind::KwRestrict:
        case TokenKind::KwNullability:
        case TokenKind::KwLong:
        case TokenKind::KwSigned:
        case TokenKind::KwShort:
        case TokenKind::KwUnsigned:
        case TokenKind::KwInt:
        case TokenKind::KwChar:
        case TokenKind::KwVoid:
        case TokenKind::KwFloat:
        case TokenKind::KwDouble:
        case TokenKind::KwFloat16:
        case TokenKind::KwIf:
        case TokenKind::KwElse:
        case TokenKind::KwWhile:
        case TokenKind::KwFor:
        case TokenKind::KwDo:
        case TokenKind::KwSwitch:
        case TokenKind::KwCase:
        case TokenKind::KwDefault:
        case TokenKind::KwBreak:
        case TokenKind::KwContinue:
        case TokenKind::KwGoto:
        case TokenKind::KwReturn:
        case TokenKind::KwStruct:
        case TokenKind::KwUnion:
        case TokenKind::KwEnum:
        case TokenKind::KwTypedef:
        case TokenKind::KwSizeof:
            return TokenCategory::Keyword;
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
        case TokenKind::Increment:
        case TokenKind::Decrement:
        case TokenKind::BitAnd:
        case TokenKind::BitOr:
        case TokenKind::BitXor:
        case TokenKind::BitNot:
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
        case TokenKind::Arrow:
        case TokenKind::Assign:
        case TokenKind::AddAssign:
        case TokenKind::SubAssign:
        case TokenKind::MulAssign:
        case TokenKind::DivAssign:
        case TokenKind::ModAssign:
        case TokenKind::ShlAssign:
        case TokenKind::ShrAssign:
        case TokenKind::AndAssign:
        case TokenKind::XorAssign:
        case TokenKind::OrAssign:
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
        case TokenKind::LogicalNot:
        case TokenKind::LogicalAnd:
        case TokenKind::LogicalOr:
        case TokenKind::Question:
            return TokenCategory::Operator;
        case TokenKind::Ellipsis:
        case TokenKind::Semicolon:
        case TokenKind::Comma:
        case TokenKind::Colon:
        case TokenKind::Dot:
        case TokenKind::LParen:
        case TokenKind::RParen:
        case TokenKind::LBracket:
        case TokenKind::RBracket:
        case TokenKind::LBrace:
        case TokenKind::RBrace:
            return TokenCategory::Punctuation;
        case TokenKind::EndOfFile:
        case TokenKind::Invalid:
            return TokenCategory::Special;
        }

        return TokenCategory::Special;
    }

    const char *get_kind_name() const noexcept {
        switch (kind_) {
        case TokenKind::Identifier:
            return "Identifier";
        case TokenKind::AnnotationIdentifier:
            return "AnnotationIdentifier";
        case TokenKind::IntLiteral:
            return "IntLiteral";
        case TokenKind::FloatLiteral:
            return "FloatLiteral";
        case TokenKind::CharLiteral:
            return "CharLiteral";
        case TokenKind::StringLiteral:
            return "StringLiteral";
        case TokenKind::KwConst:
            return "KwConst";
        case TokenKind::KwVolatile:
            return "KwVolatile";
        case TokenKind::KwExtern:
            return "KwExtern";
        case TokenKind::KwStatic:
            return "KwStatic";
        case TokenKind::KwAttribute:
            return "KwAttribute";
        case TokenKind::KwAsm:
            return "KwAsm";
        case TokenKind::KwInline:
            return "KwInline";
        case TokenKind::KwRestrict:
            return "KwRestrict";
        case TokenKind::KwNullability:
            return "KwNullability";
        case TokenKind::KwLong:
            return "KwLong";
        case TokenKind::KwSigned:
            return "KwSigned";
        case TokenKind::KwShort:
            return "KwShort";
        case TokenKind::KwUnsigned:
            return "KwUnsigned";
        case TokenKind::KwInt:
            return "KwInt";
        case TokenKind::KwChar:
            return "KwChar";
        case TokenKind::KwVoid:
            return "KwVoid";
        case TokenKind::KwFloat:
            return "KwFloat";
        case TokenKind::KwDouble:
            return "KwDouble";
        case TokenKind::KwFloat16:
            return "KwFloat16";
        case TokenKind::KwIf:
            return "KwIf";
        case TokenKind::KwElse:
            return "KwElse";
        case TokenKind::KwWhile:
            return "KwWhile";
        case TokenKind::KwFor:
            return "KwFor";
        case TokenKind::KwDo:
            return "KwDo";
        case TokenKind::KwSwitch:
            return "KwSwitch";
        case TokenKind::KwCase:
            return "KwCase";
        case TokenKind::KwDefault:
            return "KwDefault";
        case TokenKind::KwBreak:
            return "KwBreak";
        case TokenKind::KwContinue:
            return "KwContinue";
        case TokenKind::KwGoto:
            return "KwGoto";
        case TokenKind::KwReturn:
            return "KwReturn";
        case TokenKind::KwStruct:
            return "KwStruct";
        case TokenKind::KwUnion:
            return "KwUnion";
        case TokenKind::KwEnum:
            return "KwEnum";
        case TokenKind::KwTypedef:
            return "KwTypedef";
        case TokenKind::KwSizeof:
            return "KwSizeof";
        case TokenKind::Plus:
            return "Plus";
        case TokenKind::Minus:
            return "Minus";
        case TokenKind::Star:
            return "Star";
        case TokenKind::Slash:
            return "Slash";
        case TokenKind::Percent:
            return "Percent";
        case TokenKind::Increment:
            return "Increment";
        case TokenKind::Decrement:
            return "Decrement";
        case TokenKind::BitAnd:
            return "BitAnd";
        case TokenKind::BitOr:
            return "BitOr";
        case TokenKind::BitXor:
            return "BitXor";
        case TokenKind::BitNot:
            return "BitNot";
        case TokenKind::ShiftLeft:
            return "ShiftLeft";
        case TokenKind::ShiftRight:
            return "ShiftRight";
        case TokenKind::Arrow:
            return "Arrow";
        case TokenKind::Assign:
            return "Assign";
        case TokenKind::AddAssign:
            return "AddAssign";
        case TokenKind::SubAssign:
            return "SubAssign";
        case TokenKind::MulAssign:
            return "MulAssign";
        case TokenKind::DivAssign:
            return "DivAssign";
        case TokenKind::ModAssign:
            return "ModAssign";
        case TokenKind::ShlAssign:
            return "ShlAssign";
        case TokenKind::ShrAssign:
            return "ShrAssign";
        case TokenKind::AndAssign:
            return "AndAssign";
        case TokenKind::XorAssign:
            return "XorAssign";
        case TokenKind::OrAssign:
            return "OrAssign";
        case TokenKind::Equal:
            return "Equal";
        case TokenKind::NotEqual:
            return "NotEqual";
        case TokenKind::Less:
            return "Less";
        case TokenKind::LessEqual:
            return "LessEqual";
        case TokenKind::Greater:
            return "Greater";
        case TokenKind::GreaterEqual:
            return "GreaterEqual";
        case TokenKind::LogicalNot:
            return "LogicalNot";
        case TokenKind::LogicalAnd:
            return "LogicalAnd";
        case TokenKind::LogicalOr:
            return "LogicalOr";
        case TokenKind::Ellipsis:
            return "Ellipsis";
        case TokenKind::Question:
            return "Question";
        case TokenKind::Semicolon:
            return "Semicolon";
        case TokenKind::Comma:
            return "Comma";
        case TokenKind::Colon:
            return "Colon";
        case TokenKind::Dot:
            return "Dot";
        case TokenKind::LParen:
            return "LParen";
        case TokenKind::RParen:
            return "RParen";
        case TokenKind::LBracket:
            return "LBracket";
        case TokenKind::RBracket:
            return "RBracket";
        case TokenKind::LBrace:
            return "LBrace";
        case TokenKind::RBrace:
            return "RBrace";
        case TokenKind::EndOfFile:
            return "EndOfFile";
        case TokenKind::Invalid:
            return "Invalid";
        }

        return "Unknown";
    }

  private:
    TokenKind kind_;
    std::string text_;
    SourceSpan source_span_;
};

// Acts as the shared data bus across compiler passes.
class CompilerContext {
  private:
    std::string input_file_;
    std::string preprocessed_file_path_;
    std::vector<std::string> include_directories_;
    std::vector<std::string> quote_include_directories_;
    std::vector<std::string> system_include_directories_;
    std::vector<CommandLineMacroOption> command_line_macro_options_;
    std::vector<std::string> forced_include_files_;
    bool no_stdinc_ = false;
    std::vector<Token> tokens_;
    SourceLineMap preprocessed_line_map_;
    bool dump_tokens_ = false;
    bool dump_parse_ = false;
    bool dump_ast_ = false;
    bool dump_ir_ = false;
    bool dump_core_ir_ = false;
    bool emit_asm_ = false;
    bool emit_object_ = false;
    StopAfterStage stop_after_stage_ = StopAfterStage::None;
    OptimizationLevel optimization_level_ = OptimizationLevel::O0;
    bool ast_complete_ = false;
    BackendOptions backend_options_;
    std::string token_dump_file_path_;
    std::string parse_dump_file_path_;
    std::string ast_dump_file_path_;
    std::string core_ir_dump_file_path_;
    std::string ir_dump_file_path_;
    std::string llvm_ir_text_artifact_file_path_;
    std::string llvm_ir_bitcode_artifact_file_path_;
    std::string asm_dump_file_path_;
    std::string object_dump_file_path_;
    std::unique_ptr<ParseTreeNode> parse_tree_root_;
    std::unique_ptr<AstNode> ast_root_;
    std::unique_ptr<SemanticModel> semantic_model_;
    std::unique_ptr<CoreIrBuildResult> core_ir_build_result_;
    std::unique_ptr<IRResult> ir_result_;
    std::unique_ptr<AsmResult> asm_result_;
    std::unique_ptr<ObjectResult> object_result_;
    DiagnosticEngine diagnostic_engine_;
    SourceManager source_manager_;
    SourceLocationService source_location_service_;
    DialectManager dialect_manager_;

    void initialize_default_dialects(bool enable_gnu_dialect,
                                     bool enable_clang_dialect,
                                     bool enable_builtin_type_extension_pack) {
        dialect_manager_ = DialectManager();
        dialect_manager_.register_dialect(std::make_unique<C99Dialect>());
        if (enable_gnu_dialect) {
            dialect_manager_.register_dialect(std::make_unique<GnuDialect>());
        }
        if (enable_clang_dialect) {
            dialect_manager_.register_dialect(std::make_unique<ClangDialect>());
        }
        if (enable_builtin_type_extension_pack) {
            dialect_manager_.register_dialect(
                std::make_unique<BuiltinTypeExtensionPack>());
        }
    }

  public:
    CompilerContext()
        : source_location_service_(source_manager_, preprocessed_line_map_) {
        initialize_default_dialects(true, true, true);
    }

    void configure_dialects(bool enable_gnu_dialect,
                            bool enable_clang_dialect,
                            bool enable_builtin_type_extension_pack) {
        initialize_default_dialects(enable_gnu_dialect, enable_clang_dialect,
                                    enable_builtin_type_extension_pack);
    }

    const std::string &get_input_file() const noexcept { return input_file_; }

    void set_input_file(std::string input_file) {
        input_file_ = std::move(input_file);
    }

    const std::string &get_preprocessed_file_path() const noexcept {
        return preprocessed_file_path_;
    }

    void set_preprocessed_file_path(std::string preprocessed_file_path) {
        preprocessed_file_path_ = std::move(preprocessed_file_path);
    }

    const std::vector<std::string> &get_include_directories() const noexcept {
        return include_directories_;
    }

    void set_include_directories(std::vector<std::string> include_directories) {
        include_directories_ = std::move(include_directories);
    }

    const std::vector<std::string> &
    get_quote_include_directories() const noexcept {
        return quote_include_directories_;
    }

    void set_quote_include_directories(
        std::vector<std::string> quote_include_directories) {
        quote_include_directories_ = std::move(quote_include_directories);
    }

    const std::vector<std::string> &
    get_system_include_directories() const noexcept {
        return system_include_directories_;
    }

    void set_system_include_directories(
        std::vector<std::string> system_include_directories) {
        system_include_directories_ = std::move(system_include_directories);
    }

    const std::vector<CommandLineMacroOption> &
    get_command_line_macro_options() const noexcept {
        return command_line_macro_options_;
    }

    void set_command_line_macro_options(
        std::vector<CommandLineMacroOption> command_line_macro_options) {
        command_line_macro_options_ = std::move(command_line_macro_options);
    }

    const std::vector<std::string> &get_forced_include_files() const noexcept {
        return forced_include_files_;
    }

    void set_forced_include_files(std::vector<std::string> forced_include_files) {
        forced_include_files_ = std::move(forced_include_files);
    }

    bool get_no_stdinc() const noexcept { return no_stdinc_; }

    void set_no_stdinc(bool no_stdinc) noexcept { no_stdinc_ = no_stdinc; }

    const std::vector<Token> &tokens() const { return tokens_; }

    std::vector<Token> &get_tokens() noexcept { return tokens_; }

    void clear_tokens() { tokens_.clear(); }

    const SourceLineMap &get_preprocessed_line_map() const noexcept {
        return preprocessed_line_map_;
    }

    void set_preprocessed_line_map(SourceLineMap preprocessed_line_map) {
        preprocessed_line_map_ = std::move(preprocessed_line_map);
    }

    void clear_preprocessed_line_map() {
        preprocessed_line_map_.clear();
    }

    void add_token(Token token) { tokens_.push_back(std::move(token)); }

    bool get_dump_tokens() const noexcept { return dump_tokens_; }

    void set_dump_tokens(bool dump_tokens) noexcept {
        dump_tokens_ = dump_tokens;
    }

    bool get_dump_ast() const noexcept { return dump_ast_; }

    void set_dump_ast(bool dump_ast) noexcept { dump_ast_ = dump_ast; }

    bool get_dump_ir() const noexcept { return dump_ir_; }

    void set_dump_ir(bool dump_ir) noexcept { dump_ir_ = dump_ir; }

    bool get_dump_core_ir() const noexcept { return dump_core_ir_; }

    void set_dump_core_ir(bool dump_core_ir) noexcept {
        dump_core_ir_ = dump_core_ir;
    }

    bool get_emit_asm() const noexcept { return emit_asm_; }

    void set_emit_asm(bool emit_asm) noexcept { emit_asm_ = emit_asm; }

    bool get_emit_object() const noexcept { return emit_object_; }

    void set_emit_object(bool emit_object) noexcept { emit_object_ = emit_object; }

    StopAfterStage get_stop_after_stage() const noexcept {
        return stop_after_stage_;
    }

    void set_stop_after_stage(StopAfterStage stop_after_stage) noexcept {
        stop_after_stage_ = stop_after_stage;
    }

    OptimizationLevel get_optimization_level() const noexcept {
        return optimization_level_;
    }

    void set_optimization_level(OptimizationLevel optimization_level) noexcept {
        optimization_level_ = optimization_level;
    }

    bool get_ast_complete() const noexcept { return ast_complete_; }

    void set_ast_complete(bool ast_complete) noexcept {
        ast_complete_ = ast_complete;
    }

    bool get_dump_parse() const noexcept { return dump_parse_; }

    void set_dump_parse(bool dump_parse) noexcept { dump_parse_ = dump_parse; }

    const std::string &get_token_dump_file_path() const noexcept {
        return token_dump_file_path_;
    }

    void set_token_dump_file_path(std::string token_dump_file_path) {
        token_dump_file_path_ = std::move(token_dump_file_path);
    }

    const std::string &get_parse_dump_file_path() const noexcept {
        return parse_dump_file_path_;
    }

    void set_parse_dump_file_path(std::string parse_dump_file_path) {
        parse_dump_file_path_ = std::move(parse_dump_file_path);
    }

    const ParseTreeNode *get_parse_tree_root() const noexcept {
        return parse_tree_root_.get();
    }

    void set_parse_tree_root(std::unique_ptr<ParseTreeNode> parse_tree_root) {
        parse_tree_root_ = std::move(parse_tree_root);
    }

    const std::string &get_ast_dump_file_path() const noexcept {
        return ast_dump_file_path_;
    }

    void set_ast_dump_file_path(std::string ast_dump_file_path) {
        ast_dump_file_path_ = std::move(ast_dump_file_path);
    }

    const std::string &get_core_ir_dump_file_path() const noexcept {
        return core_ir_dump_file_path_;
    }

    void set_core_ir_dump_file_path(std::string core_ir_dump_file_path) {
        core_ir_dump_file_path_ = std::move(core_ir_dump_file_path);
    }

    const std::string &get_ir_dump_file_path() const noexcept {
        return ir_dump_file_path_;
    }

    void set_ir_dump_file_path(std::string ir_dump_file_path) {
        ir_dump_file_path_ = std::move(ir_dump_file_path);
    }

    const std::string &get_llvm_ir_text_artifact_file_path() const noexcept {
        return llvm_ir_text_artifact_file_path_;
    }

    void set_llvm_ir_text_artifact_file_path(
        std::string llvm_ir_text_artifact_file_path) {
        llvm_ir_text_artifact_file_path_ =
            std::move(llvm_ir_text_artifact_file_path);
    }

    const std::string &get_llvm_ir_bitcode_artifact_file_path() const noexcept {
        return llvm_ir_bitcode_artifact_file_path_;
    }

    void set_llvm_ir_bitcode_artifact_file_path(
        std::string llvm_ir_bitcode_artifact_file_path) {
        llvm_ir_bitcode_artifact_file_path_ =
            std::move(llvm_ir_bitcode_artifact_file_path);
    }

    const std::string &get_asm_dump_file_path() const noexcept {
        return asm_dump_file_path_;
    }

    void set_asm_dump_file_path(std::string asm_dump_file_path) {
        asm_dump_file_path_ = std::move(asm_dump_file_path);
    }

    const std::string &get_object_dump_file_path() const noexcept {
        return object_dump_file_path_;
    }

    void set_object_dump_file_path(std::string object_dump_file_path) {
        object_dump_file_path_ = std::move(object_dump_file_path);
    }

    const AstNode *get_ast_root() const noexcept { return ast_root_.get(); }

    void set_ast_root(std::unique_ptr<AstNode> ast_root) {
        ast_root_ = std::move(ast_root);
    }

    void clear_ast_root() {
        ast_root_.reset();
        ast_complete_ = false;
    }

    const SemanticModel *get_semantic_model() const noexcept {
        return semantic_model_.get();
    }

    SemanticModel *get_semantic_model() noexcept { return semantic_model_.get(); }

    void set_semantic_model(std::unique_ptr<SemanticModel> semantic_model) {
        semantic_model_ = std::move(semantic_model);
    }

    void clear_semantic_model() { semantic_model_.reset(); }

    const CoreIrBuildResult *get_core_ir_build_result() const noexcept {
        return core_ir_build_result_.get();
    }

    CoreIrBuildResult *get_core_ir_build_result() noexcept {
        return core_ir_build_result_.get();
    }

    void set_core_ir_build_result(
        std::unique_ptr<CoreIrBuildResult> core_ir_build_result) {
        core_ir_build_result_ = std::move(core_ir_build_result);
    }

    void clear_core_ir_build_result() { core_ir_build_result_.reset(); }

    const IRResult *get_ir_result() const noexcept { return ir_result_.get(); }

    IRResult *get_ir_result() noexcept { return ir_result_.get(); }

    void set_ir_result(std::unique_ptr<IRResult> ir_result) {
        ir_result_ = std::move(ir_result);
    }

    void clear_ir_result() { ir_result_.reset(); }

    const AsmResult *get_asm_result() const noexcept { return asm_result_.get(); }

    AsmResult *get_asm_result() noexcept { return asm_result_.get(); }

    void set_asm_result(std::unique_ptr<AsmResult> asm_result) {
        asm_result_ = std::move(asm_result);
    }

    void clear_asm_result() { asm_result_.reset(); }

    const ObjectResult *get_object_result() const noexcept {
        return object_result_.get();
    }

    ObjectResult *get_object_result() noexcept { return object_result_.get(); }

    void set_object_result(std::unique_ptr<ObjectResult> object_result) {
        object_result_ = std::move(object_result);
    }

    void clear_object_result() { object_result_.reset(); }

    const BackendOptions &get_backend_options() const noexcept {
        return backend_options_;
    }

    BackendOptions &get_backend_options() noexcept { return backend_options_; }

    void set_backend_options(BackendOptions backend_options) {
        backend_options_ = std::move(backend_options);
    }

    const DiagnosticEngine &get_diagnostic_engine() const noexcept {
        return diagnostic_engine_;
    }

    DiagnosticEngine &get_diagnostic_engine() noexcept {
        return diagnostic_engine_;
    }

    void clear_diagnostic_engine() { diagnostic_engine_.clear(); }

    SourceManager &get_source_manager() noexcept { return source_manager_; }

    const SourceManager &get_source_manager() const noexcept {
        return source_manager_;
    }

    SourceLocationService &get_source_location_service() noexcept {
        return source_location_service_;
    }

    const SourceLocationService &get_source_location_service() const noexcept {
        return source_location_service_;
    }

    DialectManager &get_dialect_manager() noexcept { return dialect_manager_; }

    const DialectManager &get_dialect_manager() const noexcept {
        return dialect_manager_;
    }
};
} // namespace sysycc
