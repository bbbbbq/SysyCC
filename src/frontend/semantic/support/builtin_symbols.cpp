#include "frontend/semantic/support/builtin_symbols.hpp"

#include <memory>
#include <vector>

#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"

namespace sysycc::detail {

namespace {

const SemanticType *register_builtin_typedef(SemanticModel &semantic_model,
                                             ScopeStack &scope_stack,
                                             const std::string &name,
                                             const SemanticType *aliased_type) {
    const auto *symbol = semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
        SymbolKind::TypedefName, name, aliased_type, nullptr));
    scope_stack.define(symbol);
    return aliased_type;
}

const SemanticSymbol *register_builtin_function(
    SemanticModel &semantic_model, ScopeStack &scope_stack,
    const std::string &name, const SemanticType *return_type,
    std::vector<const SemanticType *> parameter_types) {
    const auto *function_type = semantic_model.own_type(
        std::make_unique<FunctionSemanticType>(return_type,
                                               std::move(parameter_types),
                                               false));
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
    const auto *double_type =
        semantic_model.own_type(std::make_unique<BuiltinSemanticType>("double"));
    const auto *long_double_type = semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("long double"));
    const auto *void_type =
        semantic_model.own_type(std::make_unique<BuiltinSemanticType>("void"));
    const auto *char_type =
        semantic_model.own_type(std::make_unique<BuiltinSemanticType>("char"));
    const auto *signed_char_type = semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("signed char"));
    const auto *unsigned_char_type = semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("unsigned char"));
    const auto *short_type =
        semantic_model.own_type(std::make_unique<BuiltinSemanticType>("short"));
    const auto *unsigned_short_type = semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("unsigned short"));
    const auto *int_type_alias =
        semantic_model.own_type(std::make_unique<BuiltinSemanticType>("int"));
    const auto *unsigned_int_type = semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("unsigned int"));
    const auto *long_long_type = semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("long long int"));
    const auto *unsigned_long_type = semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("unsigned long"));
    const auto *unsigned_long_long_type = semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("unsigned long long"));
    const auto *long_type = semantic_model.own_type(
        std::make_unique<BuiltinSemanticType>("long int"));
    const auto *va_list_type = semantic_model.own_type(
        std::make_unique<PointerSemanticType>(char_type));

    register_builtin_typedef(semantic_model, scope_stack, "int8_t",
                             signed_char_type);
    register_builtin_typedef(semantic_model, scope_stack, "uint8_t",
                             unsigned_char_type);
    register_builtin_typedef(semantic_model, scope_stack, "int16_t",
                             short_type);
    register_builtin_typedef(semantic_model, scope_stack, "uint16_t",
                             unsigned_short_type);
    register_builtin_typedef(semantic_model, scope_stack, "int32_t",
                             int_type_alias);
    register_builtin_typedef(semantic_model, scope_stack, "uint32_t",
                             unsigned_int_type);
    register_builtin_typedef(semantic_model, scope_stack, "int64_t",
                             long_long_type);
    register_builtin_typedef(semantic_model, scope_stack, "uint64_t",
                             unsigned_long_long_type);
    register_builtin_typedef(semantic_model, scope_stack, "intptr_t",
                             long_type);
    register_builtin_typedef(semantic_model, scope_stack, "uintptr_t",
                             unsigned_long_type);
    register_builtin_typedef(semantic_model, scope_stack, "ptrdiff_t",
                             long_type);
    register_builtin_typedef(semantic_model, scope_stack, "size_t",
                             unsigned_long_type);
    register_builtin_typedef(semantic_model, scope_stack, "va_list",
                             va_list_type);
    register_builtin_typedef(semantic_model, scope_stack, "__builtin_va_list",
                             va_list_type);
    register_builtin_typedef(semantic_model, scope_stack, "wchar_t",
                             int_type_alias);

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

    register_builtin_function(semantic_model, scope_stack, "__builtin_fabsf",
                              float_type, {float_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_fabs",
                              double_type, {double_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_fabsl",
                              long_double_type, {long_double_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_inff",
                              float_type, {});
    register_builtin_function(semantic_model, scope_stack, "__builtin_inf",
                              double_type, {});
    register_builtin_function(semantic_model, scope_stack, "__builtin_infl",
                              long_double_type, {});
}

} // namespace sysycc::detail
