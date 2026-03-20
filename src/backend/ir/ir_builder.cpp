#include "backend/ir/ir_builder.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "backend/ir/detail/symbol_value_map.hpp"
#include "backend/ir/ir_backend.hpp"
#include "backend/ir/ir_result.hpp"
#include "compiler/compiler_context/compiler_context.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc {

namespace {

struct EmissionResult {
    bool success = false;
    bool terminated = false;
};

struct EmissionState {
    detail::SymbolValueMap symbol_value_map;
    std::vector<std::string> loop_continue_labels;
    std::vector<std::string> break_labels;
};

const SemanticModel *get_semantic_model(const CompilerContext &context) {
    return context.get_semantic_model();
}

const SemanticType *get_node_type(const CompilerContext &context,
                                  const AstNode *node) {
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || node == nullptr) {
        return nullptr;
    }
    return semantic_model->get_node_type(node);
}

const SemanticSymbol *get_symbol_binding(const CompilerContext &context,
                                         const AstNode *node) {
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || node == nullptr) {
        return nullptr;
    }
    return semantic_model->get_symbol_binding(node);
}

bool is_builtin_type_named(const SemanticType *type, const char *name) {
    if (type == nullptr || type->get_kind() != SemanticTypeKind::Builtin) {
        return false;
    }
    return static_cast<const BuiltinSemanticType *>(type)->get_name() == name;
}

bool is_supported_integer_type(const SemanticType *type) {
    return is_builtin_type_named(type, "int");
}

bool is_supported_return_type(const SemanticType *type) {
    return is_builtin_type_named(type, "int") || is_builtin_type_named(type, "void");
}

std::optional<long long> get_integer_constant_value(const CompilerContext &context,
                                                    const AstNode *node) {
    const SemanticModel *semantic_model = get_semantic_model(context);
    if (semantic_model == nullptr || node == nullptr) {
        return std::nullopt;
    }
    return semantic_model->get_integer_constant_value(node);
}

bool is_supported_arithmetic_operator(const std::string &op) {
    return op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
}

bool is_supported_comparison_operator(const std::string &op) {
    return op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" ||
           op == "!=";
}

bool is_supported_logical_operator(const std::string &op) {
    return op == "&&" || op == "||";
}

const SemanticType *get_function_return_type(const CompilerContext &context,
                                             const FunctionDecl *function_decl) {
    const SemanticType *function_type = get_node_type(context, function_decl);
    if (function_type != nullptr &&
        function_type->get_kind() == SemanticTypeKind::Function) {
        return static_cast<const FunctionSemanticType *>(function_type)
            ->get_return_type();
    }

    const TypeNode *return_type_node = function_decl->get_return_type();
    if (return_type_node != nullptr &&
        return_type_node->get_kind() == AstKind::BuiltinType) {
        const auto *builtin_type =
            static_cast<const BuiltinTypeNode *>(return_type_node);
        if (builtin_type->get_name() == "int") {
            static BuiltinSemanticType int_type("int");
            return &int_type;
        }
        if (builtin_type->get_name() == "void") {
            static BuiltinSemanticType void_type("void");
            return &void_type;
        }
    }

    return nullptr;
}

bool is_supported_type_for_storage(const CompilerContext &context,
                                   const AstNode *node) {
    return is_supported_integer_type(get_node_type(context, node));
}

bool is_supported_expr(const CompilerContext &context, const Expr *expr);

bool is_supported_decl(const CompilerContext &context, const Decl *decl) {
    if (decl == nullptr) {
        return false;
    }

    if (decl->get_kind() == AstKind::VarDecl) {
        const auto *var_decl = static_cast<const VarDecl *>(decl);
        if (!var_decl->get_dimensions().empty() ||
            !is_supported_type_for_storage(context, var_decl)) {
            return false;
        }
        const Expr *initializer = var_decl->get_initializer();
        return initializer == nullptr || is_supported_expr(context, initializer);
    }

    if (decl->get_kind() == AstKind::ParamDecl) {
        const auto *param_decl = static_cast<const ParamDecl *>(decl);
        return param_decl->get_dimensions().empty() &&
               is_supported_type_for_storage(context, param_decl);
    }

    return false;
}

bool is_supported_stmt(const CompilerContext &context, const Stmt *stmt) {
    if (stmt == nullptr) {
        return false;
    }

    switch (stmt->get_kind()) {
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        for (const auto &child : block_stmt->get_statements()) {
            if (!is_supported_stmt(context, child.get())) {
                return false;
            }
        }
        return true;
    }
    case AstKind::DeclStmt: {
        const auto *decl_stmt = static_cast<const DeclStmt *>(stmt);
        for (const auto &decl : decl_stmt->get_declarations()) {
            if (!is_supported_decl(context, decl.get())) {
                return false;
            }
        }
        return true;
    }
    case AstKind::ExprStmt: {
        const auto *expr_stmt = static_cast<const ExprStmt *>(stmt);
        return expr_stmt->get_expression() != nullptr &&
               is_supported_expr(context, expr_stmt->get_expression());
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(stmt);
        return if_stmt->get_condition() != nullptr &&
               is_supported_expr(context, if_stmt->get_condition()) &&
               is_supported_stmt(context, if_stmt->get_then_branch()) &&
               (if_stmt->get_else_branch() == nullptr ||
                is_supported_stmt(context, if_stmt->get_else_branch()));
    }
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(stmt);
        return while_stmt->get_condition() != nullptr &&
               is_supported_expr(context, while_stmt->get_condition()) &&
               is_supported_stmt(context, while_stmt->get_body());
    }
    case AstKind::DoWhileStmt: {
        const auto *do_while_stmt = static_cast<const DoWhileStmt *>(stmt);
        return do_while_stmt->get_condition() != nullptr &&
               is_supported_expr(context, do_while_stmt->get_condition()) &&
               is_supported_stmt(context, do_while_stmt->get_body());
    }
    case AstKind::ForStmt: {
        const auto *for_stmt = static_cast<const ForStmt *>(stmt);
        return (for_stmt->get_init() == nullptr ||
                is_supported_expr(context, for_stmt->get_init())) &&
               (for_stmt->get_condition() == nullptr ||
                is_supported_expr(context, for_stmt->get_condition())) &&
               (for_stmt->get_step() == nullptr ||
                is_supported_expr(context, for_stmt->get_step())) &&
               is_supported_stmt(context, for_stmt->get_body());
    }
    case AstKind::SwitchStmt: {
        const auto *switch_stmt = static_cast<const SwitchStmt *>(stmt);
        if (switch_stmt->get_condition() == nullptr ||
            !is_supported_expr(context, switch_stmt->get_condition()) ||
            switch_stmt->get_body() == nullptr ||
            switch_stmt->get_body()->get_kind() != AstKind::BlockStmt) {
            return false;
        }

        const auto *body = static_cast<const BlockStmt *>(switch_stmt->get_body());
        for (const auto &child : body->get_statements()) {
            if (child == nullptr) {
                return false;
            }
            if (child->get_kind() == AstKind::CaseStmt) {
                const auto *case_stmt = static_cast<const CaseStmt *>(child.get());
                if (case_stmt->get_value() == nullptr ||
                    !is_supported_expr(context, case_stmt->get_value()) ||
                    !get_integer_constant_value(context, case_stmt->get_value())
                         .has_value() ||
                    !is_supported_stmt(context, case_stmt->get_body())) {
                    return false;
                }
                continue;
            }
            if (child->get_kind() == AstKind::DefaultStmt) {
                const auto *default_stmt =
                    static_cast<const DefaultStmt *>(child.get());
                if (!is_supported_stmt(context, default_stmt->get_body())) {
                    return false;
                }
                continue;
            }
            return false;
        }
        return true;
    }
    case AstKind::CaseStmt: {
        const auto *case_stmt = static_cast<const CaseStmt *>(stmt);
        return case_stmt->get_value() != nullptr &&
               is_supported_expr(context, case_stmt->get_value()) &&
               get_integer_constant_value(context, case_stmt->get_value()).has_value() &&
               is_supported_stmt(context, case_stmt->get_body());
    }
    case AstKind::DefaultStmt: {
        const auto *default_stmt = static_cast<const DefaultStmt *>(stmt);
        return is_supported_stmt(context, default_stmt->get_body());
    }
    case AstKind::BreakStmt:
    case AstKind::ContinueStmt:
        return true;
    case AstKind::ReturnStmt: {
        const auto *return_stmt = static_cast<const ReturnStmt *>(stmt);
        const Expr *return_value = return_stmt->get_value();
        return return_value == nullptr || is_supported_expr(context, return_value);
    }
    default:
        return false;
    }
}

bool is_supported_expr(const CompilerContext &context, const Expr *expr) {
    if (expr == nullptr) {
        return false;
    }

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr:
        return true;
    case AstKind::IdentifierExpr: {
        const auto *symbol = get_symbol_binding(context, expr);
        if (symbol == nullptr) {
            return false;
        }
        if (symbol->get_kind() == SymbolKind::Function) {
            const SemanticType *type = symbol->get_type();
            return type != nullptr && type->get_kind() == SemanticTypeKind::Function;
        }
        return symbol->get_decl_node() != nullptr &&
               is_supported_integer_type(symbol->get_type());
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        return (is_supported_arithmetic_operator(binary_expr->get_operator_text()) ||
                is_supported_comparison_operator(binary_expr->get_operator_text()) ||
                is_supported_logical_operator(binary_expr->get_operator_text())) &&
               is_supported_integer_type(get_node_type(context, expr)) &&
               is_supported_expr(context, binary_expr->get_lhs()) &&
               is_supported_expr(context, binary_expr->get_rhs());
    }
    case AstKind::AssignExpr: {
        const auto *assign_expr = static_cast<const AssignExpr *>(expr);
        if (assign_expr->get_target() == nullptr ||
            assign_expr->get_target()->get_kind() != AstKind::IdentifierExpr) {
            return false;
        }
        return is_supported_integer_type(get_node_type(context, assign_expr)) &&
               is_supported_expr(context, assign_expr->get_value());
    }
    case AstKind::CallExpr: {
        const auto *call_expr = static_cast<const CallExpr *>(expr);
        if (call_expr->get_callee() == nullptr ||
            call_expr->get_callee()->get_kind() != AstKind::IdentifierExpr) {
            return false;
        }

        const SemanticType *callee_type =
            get_node_type(context, call_expr->get_callee());
        if (callee_type == nullptr ||
            callee_type->get_kind() != SemanticTypeKind::Function) {
            return false;
        }

        const auto *function_type =
            static_cast<const FunctionSemanticType *>(callee_type);
        if (!is_supported_return_type(function_type->get_return_type())) {
            return false;
        }

        const auto &parameter_types = function_type->get_parameter_types();
        if (parameter_types.size() != call_expr->get_arguments().size()) {
            return false;
        }

        for (std::size_t index = 0; index < call_expr->get_arguments().size();
             ++index) {
            if (!is_supported_integer_type(parameter_types[index]) ||
                !is_supported_expr(context,
                                   call_expr->get_arguments()[index].get())) {
                return false;
            }
        }
        return true;
    }
    default:
        return false;
    }
}

bool is_supported_function(const CompilerContext &context,
                           const FunctionDecl *function_decl) {
    if (function_decl == nullptr) {
        return false;
    }

    const SemanticType *return_type =
        get_function_return_type(context, function_decl);
    if (!is_supported_return_type(return_type)) {
        return false;
    }

    for (const auto &parameter : function_decl->get_parameters()) {
        if (!is_supported_decl(context, parameter.get())) {
            return false;
        }
    }

    return function_decl->get_body() != nullptr &&
           is_supported_stmt(context, function_decl->get_body());
}

std::optional<IRValue>
build_expr(IRBackend &backend, const CompilerContext &context,
           EmissionState &state, const Expr *expr) {
    if (expr == nullptr) {
        return std::nullopt;
    }

    switch (expr->get_kind()) {
    case AstKind::IntegerLiteralExpr: {
        const auto *integer_literal =
            static_cast<const IntegerLiteralExpr *>(expr);
        return backend.emit_integer_literal(
            std::stoi(integer_literal->get_value_text()));
    }
    case AstKind::IdentifierExpr: {
        const auto *symbol = get_symbol_binding(context, expr);
        if (symbol == nullptr) {
            return std::nullopt;
        }

        if (symbol->get_kind() == SymbolKind::Function) {
            return IRValue{"@" + symbol->get_name(), symbol->get_type()};
        }

        const AstNode *decl_node = symbol->get_decl_node();
        if (decl_node == nullptr) {
            return std::nullopt;
        }

        const std::string *address = state.symbol_value_map.get_value(decl_node);
        if (address == nullptr) {
            return std::nullopt;
        }
        return backend.emit_load(*address, symbol->get_type());
    }
    case AstKind::BinaryExpr: {
        const auto *binary_expr = static_cast<const BinaryExpr *>(expr);
        const SemanticType *result_type = get_node_type(context, expr);
        if (result_type == nullptr) {
            return std::nullopt;
        }

        if (is_supported_logical_operator(binary_expr->get_operator_text())) {
            std::optional<IRValue> lhs =
                build_expr(backend, context, state, binary_expr->get_lhs());
            if (!lhs.has_value()) {
                return std::nullopt;
            }

            const std::string result_address =
                backend.emit_alloca("", result_type);
            backend.emit_store(result_address, backend.emit_integer_literal(0));

            const std::string rhs_label = backend.create_label("logic.rhs");
            const std::string true_label = backend.create_label("logic.true");
            const std::string end_label = backend.create_label("logic.end");

            if (binary_expr->get_operator_text() == "&&") {
                backend.emit_cond_branch(*lhs, rhs_label, end_label);
            } else {
                backend.emit_cond_branch(*lhs, true_label, rhs_label);
            }

            backend.emit_label(rhs_label);
            std::optional<IRValue> rhs =
                build_expr(backend, context, state, binary_expr->get_rhs());
            if (!rhs.has_value()) {
                return std::nullopt;
            }
            backend.emit_cond_branch(*rhs, true_label, end_label);

            backend.emit_label(true_label);
            backend.emit_store(result_address, backend.emit_integer_literal(1));
            backend.emit_branch(end_label);

            backend.emit_label(end_label);
            return backend.emit_load(result_address, result_type);
        }

        std::optional<IRValue> lhs =
            build_expr(backend, context, state, binary_expr->get_lhs());
        std::optional<IRValue> rhs =
            build_expr(backend, context, state, binary_expr->get_rhs());
        if (!lhs.has_value() || !rhs.has_value() || result_type == nullptr) {
            return std::nullopt;
        }
        IRValue result = backend.emit_binary(binary_expr->get_operator_text(),
                                             *lhs, *rhs, result_type);
        if (result.text.empty()) {
            return std::nullopt;
        }
        return result;
    }
    case AstKind::AssignExpr: {
        const auto *assign_expr = static_cast<const AssignExpr *>(expr);
        const auto *target =
            static_cast<const IdentifierExpr *>(assign_expr->get_target());
        const SemanticSymbol *symbol = get_symbol_binding(context, target);
        if (symbol == nullptr || symbol->get_decl_node() == nullptr) {
            return std::nullopt;
        }

        const std::string *address =
            state.symbol_value_map.get_value(symbol->get_decl_node());
        if (address == nullptr) {
            return std::nullopt;
        }

        std::optional<IRValue> value =
            build_expr(backend, context, state, assign_expr->get_value());
        if (!value.has_value()) {
            return std::nullopt;
        }

        backend.emit_store(*address, *value);
        return value;
    }
    case AstKind::CallExpr: {
        const auto *call_expr = static_cast<const CallExpr *>(expr);
        const auto *callee_identifier = static_cast<const IdentifierExpr *>(
            call_expr->get_callee());
        const SemanticSymbol *callee_symbol =
            get_symbol_binding(context, callee_identifier);
        std::optional<IRValue> callee =
            build_expr(backend, context, state, call_expr->get_callee());
        const SemanticType *return_type = get_node_type(context, expr);
        if (!callee.has_value() || return_type == nullptr ||
            callee_symbol == nullptr) {
            return std::nullopt;
        }

        const auto *function_type =
            static_cast<const FunctionSemanticType *>(callee_symbol->get_type());
        if (callee_symbol->get_decl_node() == nullptr) {
            backend.declare_function(callee_symbol->get_name(),
                                     function_type->get_return_type(),
                                     function_type->get_parameter_types());
        }

        std::vector<IRValue> arguments;
        arguments.reserve(call_expr->get_arguments().size());
        for (const auto &argument : call_expr->get_arguments()) {
            std::optional<IRValue> lowered_argument =
                build_expr(backend, context, state, argument.get());
            if (!lowered_argument.has_value()) {
                return std::nullopt;
            }
            arguments.push_back(*lowered_argument);
        }

        return backend.emit_call(callee->text, arguments, return_type);
    }
    default:
        return std::nullopt;
    }
}

bool emit_decl(IRBackend &backend, const CompilerContext &context,
               EmissionState &state, const Decl *decl) {
    if (decl == nullptr || decl->get_kind() != AstKind::VarDecl) {
        return false;
    }

    const auto *var_decl = static_cast<const VarDecl *>(decl);
    const SemanticType *declared_type = get_node_type(context, var_decl);
    if (!is_supported_integer_type(declared_type)) {
        return false;
    }

    const std::string address =
        backend.emit_alloca(var_decl->get_name(), declared_type);
    state.symbol_value_map.bind_value(var_decl, address);

    if (var_decl->get_initializer() != nullptr) {
        std::optional<IRValue> initializer =
            build_expr(backend, context, state,
                       var_decl->get_initializer());
        if (!initializer.has_value()) {
            return false;
        }
        backend.emit_store(address, *initializer);
    }

    return true;
}

EmissionResult emit_stmt(IRBackend &backend, const CompilerContext &context,
                         EmissionState &state, const Stmt *stmt) {
    if (stmt == nullptr) {
        return {};
    }

    switch (stmt->get_kind()) {
    case AstKind::BlockStmt: {
        const auto *block_stmt = static_cast<const BlockStmt *>(stmt);
        for (const auto &child : block_stmt->get_statements()) {
            EmissionResult child_result =
                emit_stmt(backend, context, state, child.get());
            if (!child_result.success) {
                return {};
            }
            if (child_result.terminated) {
                return {true, true};
            }
        }
        return {true, false};
    }
    case AstKind::DeclStmt: {
        const auto *decl_stmt = static_cast<const DeclStmt *>(stmt);
        for (const auto &decl : decl_stmt->get_declarations()) {
            if (!emit_decl(backend, context, state, decl.get())) {
                return {};
            }
        }
        return {true, false};
    }
    case AstKind::ExprStmt: {
        const auto *expr_stmt = static_cast<const ExprStmt *>(stmt);
        return {build_expr(backend, context, state,
                           expr_stmt->get_expression())
                        .has_value(),
                false};
    }
    case AstKind::IfStmt: {
        const auto *if_stmt = static_cast<const IfStmt *>(stmt);
        std::optional<IRValue> condition =
            build_expr(backend, context, state, if_stmt->get_condition());
        if (!condition.has_value()) {
            return {};
        }

        const std::string then_label = backend.create_label("if.then");
        const std::string end_label = backend.create_label("if.end");
        const std::string else_label =
            if_stmt->get_else_branch() != nullptr ? backend.create_label("if.else")
                                                  : end_label;

        backend.emit_cond_branch(*condition, then_label, else_label);

        backend.emit_label(then_label);
        EmissionResult then_result =
            emit_stmt(backend, context, state, if_stmt->get_then_branch());
        if (!then_result.success) {
            return {};
        }
        if (!then_result.terminated) {
            backend.emit_branch(end_label);
        }

        bool else_terminated = false;
        if (if_stmt->get_else_branch() != nullptr) {
            backend.emit_label(else_label);
            EmissionResult else_result = emit_stmt(backend, context, state,
                                                   if_stmt->get_else_branch());
            if (!else_result.success) {
                return {};
            }
            else_terminated = else_result.terminated;
            if (!else_result.terminated) {
                backend.emit_branch(end_label);
            }
        }

        if (!then_result.terminated || !else_terminated ||
            if_stmt->get_else_branch() == nullptr) {
            backend.emit_label(end_label);
        }
        return {true, then_result.terminated &&
                          if_stmt->get_else_branch() != nullptr && else_terminated};
    }
    case AstKind::WhileStmt: {
        const auto *while_stmt = static_cast<const WhileStmt *>(stmt);
        const std::string cond_label = backend.create_label("while.cond");
        const std::string body_label = backend.create_label("while.body");
        const std::string end_label = backend.create_label("while.end");

        backend.emit_branch(cond_label);
        backend.emit_label(cond_label);
        state.loop_continue_labels.push_back(cond_label);
        state.break_labels.push_back(end_label);
        std::optional<IRValue> condition =
            build_expr(backend, context, state, while_stmt->get_condition());
        if (!condition.has_value()) {
            state.loop_continue_labels.pop_back();
            state.break_labels.pop_back();
            return {};
        }
        backend.emit_cond_branch(*condition, body_label, end_label);

        backend.emit_label(body_label);
        EmissionResult body_result =
            emit_stmt(backend, context, state, while_stmt->get_body());
        state.loop_continue_labels.pop_back();
        state.break_labels.pop_back();
        if (!body_result.success) {
            return {};
        }
        if (!body_result.terminated) {
            backend.emit_branch(cond_label);
        }

        backend.emit_label(end_label);
        return {true, false};
    }
    case AstKind::DoWhileStmt: {
        const auto *do_while_stmt = static_cast<const DoWhileStmt *>(stmt);
        const std::string body_label = backend.create_label("dowhile.body");
        const std::string cond_label = backend.create_label("dowhile.cond");
        const std::string end_label = backend.create_label("dowhile.end");

        backend.emit_branch(body_label);
        backend.emit_label(body_label);
        state.loop_continue_labels.push_back(cond_label);
        state.break_labels.push_back(end_label);
        EmissionResult body_result =
            emit_stmt(backend, context, state, do_while_stmt->get_body());
        state.loop_continue_labels.pop_back();
        state.break_labels.pop_back();
        if (!body_result.success) {
            return {};
        }
        if (!body_result.terminated) {
            backend.emit_branch(cond_label);
        }

        backend.emit_label(cond_label);
        std::optional<IRValue> condition =
            build_expr(backend, context, state, do_while_stmt->get_condition());
        if (!condition.has_value()) {
            return {};
        }
        backend.emit_cond_branch(*condition, body_label, end_label);
        backend.emit_label(end_label);
        return {true, false};
    }
    case AstKind::ForStmt: {
        const auto *for_stmt = static_cast<const ForStmt *>(stmt);
        if (for_stmt->get_init() != nullptr &&
            !build_expr(backend, context, state, for_stmt->get_init()).has_value()) {
            return {};
        }

        const std::string cond_label = backend.create_label("for.cond");
        const std::string body_label = backend.create_label("for.body");
        const std::string step_label = backend.create_label("for.step");
        const std::string end_label = backend.create_label("for.end");

        backend.emit_branch(cond_label);
        backend.emit_label(cond_label);
        if (for_stmt->get_condition() != nullptr) {
            std::optional<IRValue> condition =
                build_expr(backend, context, state, for_stmt->get_condition());
            if (!condition.has_value()) {
                return {};
            }
            backend.emit_cond_branch(*condition, body_label, end_label);
        } else {
            backend.emit_branch(body_label);
        }

        backend.emit_label(body_label);
        state.loop_continue_labels.push_back(step_label);
        state.break_labels.push_back(end_label);
        EmissionResult body_result =
            emit_stmt(backend, context, state, for_stmt->get_body());
        state.loop_continue_labels.pop_back();
        state.break_labels.pop_back();
        if (!body_result.success) {
            return {};
        }
        if (!body_result.terminated) {
            backend.emit_branch(step_label);
        }

        backend.emit_label(step_label);
        if (for_stmt->get_step() != nullptr &&
            !build_expr(backend, context, state, for_stmt->get_step()).has_value()) {
            return {};
        }
        backend.emit_branch(cond_label);
        backend.emit_label(end_label);
        return {true, false};
    }
    case AstKind::SwitchStmt: {
        const auto *switch_stmt = static_cast<const SwitchStmt *>(stmt);
        const auto *body = static_cast<const BlockStmt *>(switch_stmt->get_body());

        std::optional<IRValue> switch_value =
            build_expr(backend, context, state, switch_stmt->get_condition());
        if (!switch_value.has_value()) {
            return {};
        }

        struct SwitchEntry {
            const Stmt *stmt = nullptr;
            std::string label;
        };

        std::vector<SwitchEntry> entries;
        entries.reserve(body->get_statements().size());
        std::vector<const CaseStmt *> case_entries;
        std::vector<std::string> case_labels;
        case_entries.reserve(body->get_statements().size());
        case_labels.reserve(body->get_statements().size());
        const DefaultStmt *default_entry = nullptr;
        std::string default_label;
        for (const auto &child : body->get_statements()) {
            if (child->get_kind() == AstKind::CaseStmt) {
                const std::string case_label = backend.create_label("switch.case");
                entries.push_back({child.get(), case_label});
                case_entries.push_back(static_cast<const CaseStmt *>(child.get()));
                case_labels.push_back(case_label);
            } else {
                default_label = backend.create_label("switch.default");
                entries.push_back({child.get(), default_label});
                default_entry = static_cast<const DefaultStmt *>(child.get());
            }
        }

        const std::string end_label = backend.create_label("switch.end");
        if (!case_entries.empty()) {
            std::vector<std::string> test_labels;
            test_labels.reserve(case_entries.size());
            for (std::size_t index = 0; index < case_entries.size(); ++index) {
                test_labels.push_back(backend.create_label("switch.test"));
            }

            backend.emit_branch(test_labels.front());
            for (std::size_t index = 0; index < case_entries.size(); ++index) {
                backend.emit_label(test_labels[index]);

                const auto *case_stmt = case_entries[index];
                std::optional<long long> case_value =
                    get_integer_constant_value(context, case_stmt->get_value());
                if (!case_value.has_value()) {
                    return {};
                }

                IRValue case_ir_value =
                    backend.emit_integer_literal(static_cast<int>(*case_value));
                const SemanticType *comparison_type =
                    get_node_type(context, switch_stmt->get_condition());
                if (comparison_type == nullptr) {
                    return {};
                }

                IRValue comparison = backend.emit_binary(
                    "==", *switch_value, case_ir_value, comparison_type);
                const std::string false_label =
                    index + 1 < case_entries.size()
                        ? test_labels[index + 1]
                        : (default_entry != nullptr ? default_label : end_label);
                backend.emit_cond_branch(comparison, case_labels[index],
                                         false_label);
            }
        } else {
            backend.emit_branch(default_entry != nullptr ? default_label
                                                         : end_label);
        }

        state.break_labels.push_back(end_label);
        for (std::size_t index = 0; index < entries.size(); ++index) {
            backend.emit_label(entries[index].label);
            const Stmt *body_stmt = nullptr;
            if (entries[index].stmt->get_kind() == AstKind::CaseStmt) {
                body_stmt =
                    static_cast<const CaseStmt *>(entries[index].stmt)->get_body();
            } else {
                body_stmt =
                    static_cast<const DefaultStmt *>(entries[index].stmt)->get_body();
            }

            EmissionResult entry_result =
                emit_stmt(backend, context, state, body_stmt);
            if (!entry_result.success) {
                state.break_labels.pop_back();
                return {};
            }
            if (!entry_result.terminated) {
                const std::string fallthrough_label =
                    index + 1 < entries.size() ? entries[index + 1].label : end_label;
                backend.emit_branch(fallthrough_label);
            }
        }
        state.break_labels.pop_back();
        backend.emit_label(end_label);
        return {true, false};
    }
    case AstKind::BreakStmt:
        if (state.break_labels.empty()) {
            return {};
        }
        backend.emit_branch(state.break_labels.back());
        return {true, true};
    case AstKind::ContinueStmt:
        if (state.loop_continue_labels.empty()) {
            return {};
        }
        backend.emit_branch(state.loop_continue_labels.back());
        return {true, true};
    case AstKind::ReturnStmt: {
        const auto *return_stmt = static_cast<const ReturnStmt *>(stmt);
        if (return_stmt->get_value() == nullptr) {
            backend.emit_return_void();
            return {true, true};
        }

        std::optional<IRValue> return_value =
            build_expr(backend, context, state, return_stmt->get_value());
        if (!return_value.has_value()) {
            return {};
        }
        backend.emit_return(*return_value);
        return {true, true};
    }
    default:
        return {};
    }
}

bool emit_supported_function(IRBackend &backend, const CompilerContext &context,
                             const FunctionDecl *function_decl) {
    if (!is_supported_function(context, function_decl)) {
        return false;
    }

    std::vector<IRFunctionParameter> parameters;
    parameters.reserve(function_decl->get_parameters().size());
    for (const auto &parameter_decl : function_decl->get_parameters()) {
        const auto *param_decl = static_cast<const ParamDecl *>(parameter_decl.get());
        parameters.push_back(
            IRFunctionParameter{param_decl->get_name(),
                                get_node_type(context, param_decl)});
    }

    const SemanticType *return_type =
        get_function_return_type(context, function_decl);
    backend.begin_function(function_decl->get_name(), return_type, parameters);

    EmissionState state;
    for (const auto &parameter_decl : function_decl->get_parameters()) {
        const auto *param_decl = static_cast<const ParamDecl *>(parameter_decl.get());
        const SemanticType *parameter_type = get_node_type(context, param_decl);
        const std::string address =
            backend.emit_alloca(param_decl->get_name(), parameter_type);
        state.symbol_value_map.bind_value(param_decl, address);
        backend.emit_store(address,
                           IRValue{"%" + param_decl->get_name(), parameter_type});
    }

    const EmissionResult emitted =
        emit_stmt(backend, context, state, function_decl->get_body());
    backend.end_function();
    return emitted.success;
}

} // namespace

std::unique_ptr<IRResult> IRBuilder::Build(const CompilerContext &context) {
    backend_.begin_module();
    const AstNode *ast_root = context.get_ast_root();
    if (ast_root != nullptr &&
        ast_root->get_kind() == AstKind::TranslationUnit) {
        const auto *translation_unit = static_cast<const TranslationUnit *>(ast_root);
        for (const auto &decl : translation_unit->get_top_level_decls()) {
            if (decl != nullptr && decl->get_kind() == AstKind::FunctionDecl) {
                emit_supported_function(
                    backend_, context,
                    static_cast<const FunctionDecl *>(decl.get()));
            }
        }
    }
    backend_.end_module();
    return std::make_unique<IRResult>(backend_.get_kind(),
                                      backend_.get_output_text());
}

} // namespace sysycc
