#include "frontend/semantic/support/builtin_symbols.hpp"

#include <memory>
#include <string>
#include <vector>

#include "frontend/semantic/model/semantic_model.hpp"
#include "frontend/semantic/model/semantic_symbol.hpp"
#include "frontend/semantic/model/semantic_type.hpp"
#include "frontend/semantic/support/scope_stack.hpp"
#include "frontend/support/builtin_typedef_inventory.hpp"

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
    std::vector<const SemanticType *> parameter_types,
    bool is_variadic = false) {
    const auto *function_type = semantic_model.own_type(
        std::make_unique<FunctionSemanticType>(return_type,
                                               std::move(parameter_types),
                                               is_variadic));
    const auto *symbol = semantic_model.own_symbol(std::make_unique<SemanticSymbol>(
        SymbolKind::Function, name, function_type, nullptr));
    scope_stack.define(symbol);
    return symbol;
}

const SemanticType *get_builtin_typedef_group_type(
    BuiltinTypedefGroup group, const SemanticType *signed_char_type,
    const SemanticType *unsigned_char_type, const SemanticType *short_type,
    const SemanticType *unsigned_short_type, const SemanticType *int_type,
    const SemanticType *unsigned_int_type, const SemanticType *long_type,
    const SemanticType *unsigned_long_type, const SemanticType *long_long_type,
    const SemanticType *unsigned_long_long_type, const SemanticType *va_list_type) {
    switch (group) {
    case BuiltinTypedefGroup::SignedChar:
        return signed_char_type;
    case BuiltinTypedefGroup::UnsignedChar:
        return unsigned_char_type;
    case BuiltinTypedefGroup::Short:
        return short_type;
    case BuiltinTypedefGroup::UnsignedShort:
        return unsigned_short_type;
    case BuiltinTypedefGroup::Int:
        return int_type;
    case BuiltinTypedefGroup::UnsignedInt:
        return unsigned_int_type;
    case BuiltinTypedefGroup::Long:
        return long_type;
    case BuiltinTypedefGroup::UnsignedLong:
        return unsigned_long_type;
    case BuiltinTypedefGroup::LongLong:
        return long_long_type;
    case BuiltinTypedefGroup::UnsignedLongLong:
        return unsigned_long_long_type;
    case BuiltinTypedefGroup::VaList:
        return va_list_type;
    }
    return nullptr;
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
    const auto *void_ptr_type = semantic_model.own_type(
        std::make_unique<PointerSemanticType>(void_type));
    const auto *va_list_struct_type = semantic_model.own_type(
        std::make_unique<StructSemanticType>(
            "__sysycc_va_list",
            std::vector<SemanticFieldInfo>{
                SemanticFieldInfo("__stack", void_ptr_type),
                SemanticFieldInfo("__gr_top", void_ptr_type),
                SemanticFieldInfo("__vr_top", void_ptr_type),
                SemanticFieldInfo("__gr_offs", int_type),
                SemanticFieldInfo("__vr_offs", int_type)}));
    const auto *va_list_type = semantic_model.own_type(
        std::make_unique<ArraySemanticType>(va_list_struct_type,
                                            std::vector<int>{1}));
    const auto *va_list_pointer_type = semantic_model.own_type(
        std::make_unique<PointerSemanticType>(va_list_struct_type));
    const auto *char_ptr_type = semantic_model.own_type(
        std::make_unique<PointerSemanticType>(char_type));
    const auto *int_ptr_type = semantic_model.own_type(
        std::make_unique<PointerSemanticType>(int_type));
    const auto *float_ptr_type = semantic_model.own_type(
        std::make_unique<PointerSemanticType>(float_type));

    for_each_builtin_typedef_inventory_entry(
        [&](const BuiltinTypedefInventoryEntry &entry) {
            register_builtin_typedef(
                semantic_model, scope_stack, std::string(entry.name),
                get_builtin_typedef_group_type(
                    entry.group, signed_char_type, unsigned_char_type, short_type,
                    unsigned_short_type, int_type_alias, unsigned_int_type,
                    long_type, unsigned_long_type, long_long_type,
                    unsigned_long_long_type, va_list_type));
        });

    register_builtin_function(semantic_model, scope_stack, "getint", int_type,
                              {});
    register_builtin_function(semantic_model, scope_stack, "getch", int_type,
                              {});
    register_builtin_function(semantic_model, scope_stack, "getfloat",
                              float_type, {});
    register_builtin_function(semantic_model, scope_stack, "getarray", int_type,
                              {int_ptr_type});
    register_builtin_function(semantic_model, scope_stack, "getfarray", int_type,
                              {float_ptr_type});
    register_builtin_function(semantic_model, scope_stack, "putint", void_type,
                              {int_type});
    register_builtin_function(semantic_model, scope_stack, "putfloat",
                              void_type, {float_type});
    register_builtin_function(semantic_model, scope_stack, "putch", void_type,
                              {int_type});
    register_builtin_function(semantic_model, scope_stack, "putarray", void_type,
                              {int_type, int_ptr_type});
    register_builtin_function(semantic_model, scope_stack, "putfarray", void_type,
                              {int_type, float_ptr_type});
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
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_huge_valf", float_type, {});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_huge_val", double_type, {});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_huge_vall", long_double_type, {});
    register_builtin_function(semantic_model, scope_stack, "__builtin_nanf",
                              float_type, {char_ptr_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_nan",
                              double_type, {char_ptr_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_nanl",
                              long_double_type, {char_ptr_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_isnan",
                              int_type, {double_type});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_isfinite", int_type, {double_type});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_isinf_sign", int_type, {double_type});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_signbit", int_type, {double_type});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_bswap16", unsigned_short_type,
                              {unsigned_short_type});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_bswap32", unsigned_int_type,
                              {unsigned_int_type});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_bswap64", unsigned_long_long_type,
                              {unsigned_long_long_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_clzll",
                              int_type, {unsigned_long_long_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_ctzll",
                              int_type, {unsigned_long_long_type});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_add_overflow", int_type, {}, true);
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_sub_overflow", int_type, {}, true);
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_mul_overflow", int_type, {}, true);
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin_object_size", unsigned_long_type,
                              {void_ptr_type, int_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_alloca",
                              void_ptr_type, {unsigned_long_type});
    register_builtin_function(semantic_model, scope_stack,
                              "__builtin___memcpy_chk", void_ptr_type,
                              {void_ptr_type, void_ptr_type, unsigned_long_type,
                               unsigned_long_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_va_start",
                              void_type, {va_list_pointer_type}, true);
    register_builtin_function(semantic_model, scope_stack, "__builtin_va_end",
                              void_type, {va_list_pointer_type});
    register_builtin_function(semantic_model, scope_stack, "__builtin_va_copy",
                              void_type,
                              {va_list_pointer_type, va_list_pointer_type});
}

} // namespace sysycc::detail
