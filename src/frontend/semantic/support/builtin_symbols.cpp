#include "frontend/semantic/support/builtin_symbols.hpp"

#include <memory>
#include <vector>

#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

namespace {

const SemanticSymbol *register_builtin_function(
    SemanticModel &semantic_model, ScopeStack &scope_stack,
    const std::string &name, const SemanticType *return_type,
    std::vector<const SemanticType *> parameter_types) {
    const auto *function_type = semantic_model.own_type(
        std::make_unique<FunctionSemanticType>(return_type,
                                               std::move(parameter_types)));
    const auto *symbol = semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
        SymbolKind::Function, name, function_type, nullptr));
    scope_stack.define(symbol);
    return symbol;
}

} // namespace

void BuiltinSymbols::install(SemanticModel &semantic_model,
                             ScopeStack &scope_stack) const {
    const auto *int_type =
        semantic_model.own_type(std::make_unique<BuiltinSemanticType>("int"));
    const auto *float_type =
        semantic_model.own_type(std::make_unique<BuiltinSemanticType>("float"));
    const auto *void_type =
        semantic_model.own_type(std::make_unique<BuiltinSemanticType>("void"));

    register_builtin_function(semantic_model, scope_stack, "getint", int_type,
                              {});
    register_builtin_function(semantic_model, scope_stack, "getfloat",
                              float_type, {});
    register_builtin_function(semantic_model, scope_stack, "putint", void_type,
                              {int_type});
    register_builtin_function(semantic_model, scope_stack, "putfloat",
                              void_type, {float_type});
    register_builtin_function(semantic_model, scope_stack, "putch", void_type,
                              {int_type});
    register_builtin_function(semantic_model, scope_stack, "starttime",
                              void_type, {});
    register_builtin_function(semantic_model, scope_stack, "stoptime",
                              void_type, {});
}

} // namespace sysycc::detail
