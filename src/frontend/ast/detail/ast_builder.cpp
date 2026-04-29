#include "frontend/ast/detail/ast_builder.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "frontend/ast/detail/parse_tree_matcher.hpp"
#include "frontend/attribute/attribute_parser.hpp"

namespace sysycc::detail {

namespace {

struct TypeQualifierFlags {
    bool is_const = false;
    bool is_volatile = false;
};

struct StorageSpecifierFlags {
    bool is_extern = false;
    bool is_static = false;
};

std::string builtin_name_from_type_name_token(const ParseTreeNode *node) {
    if (node == nullptr ||
        !ParseTreeMatcher::label_starts_with(node, "TYPE_NAME")) {
        return {};
    }
    const std::string suffix =
        ParseTreeMatcher::extract_terminal_suffix(node, "TYPE_NAME");
    if (suffix == "void" || suffix == "char" || suffix == "int" ||
        suffix == "float" || suffix == "double") {
        return suffix;
    }
    return {};
}

bool is_int_type_token(const ParseTreeNode *node) {
    return ParseTreeMatcher::label_starts_with(node, "INT") ||
           builtin_name_from_type_name_token(node) == "int";
}

std::string decode_string_literal_token(std::string token_text) {
    if (token_text.size() >= 2 && token_text.front() == '"' &&
        token_text.back() == '"') {
        token_text = token_text.substr(1, token_text.size() - 2);
    }

    auto hex_digit_value = [](char ch) noexcept -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };
    auto is_octal_digit = [](char ch) noexcept {
        return ch >= '0' && ch <= '7';
    };

    std::string decoded;
    decoded.reserve(token_text.size());
    for (std::size_t index = 0; index < token_text.size(); ++index) {
        char ch = token_text[index];
        if (ch == '\\' && index + 1 < token_text.size()) {
            const char escaped = token_text[++index];
            switch (escaped) {
            case '\\':
            case '"':
            case '\'':
                decoded.push_back(escaped);
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 'a':
                decoded.push_back('\a');
                break;
            case 'b':
                decoded.push_back('\b');
                break;
            case 'f':
                decoded.push_back('\f');
                break;
            case 'v':
                decoded.push_back('\v');
                break;
            case 'x': {
                unsigned int value = 0;
                bool consumed_digit = false;
                while (index + 1 < token_text.size()) {
                    const int digit = hex_digit_value(token_text[index + 1]);
                    if (digit < 0) {
                        break;
                    }
                    consumed_digit = true;
                    value = (value << 4) | static_cast<unsigned int>(digit);
                    ++index;
                }
                decoded.push_back(consumed_digit
                                      ? static_cast<char>(value & 0xffU)
                                      : escaped);
                break;
            }
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7': {
                unsigned int value = static_cast<unsigned int>(escaped - '0');
                int digits = 1;
                while (digits < 3 && index + 1 < token_text.size() &&
                       is_octal_digit(token_text[index + 1])) {
                    value = (value << 3) | static_cast<unsigned int>(
                                               token_text[index + 1] - '0');
                    ++index;
                    ++digits;
                }
                decoded.push_back(static_cast<char>(value & 0xffU));
                break;
            }
            default:
                decoded.push_back(escaped);
                break;
            }
            continue;
        }
        decoded.push_back(ch);
    }
    return decoded;
}

void append_attributes(const ParsedAttributeList &source,
                       std::vector<ParsedAttribute> &destination) {
    const auto &attributes = source.get_attributes();
    destination.insert(destination.end(), attributes.begin(), attributes.end());
}

void collect_type_qualifiers(const ParseTreeNode *node, bool &is_const,
                             bool &is_volatile);

const ParseTreeNode *find_parameter_list_node(const ParseTreeNode *node) {
    if (node == nullptr) {
        return nullptr;
    }

    const ParseTreeNode *parameter_list =
        ParseTreeMatcher::find_first_child_with_label(node,
                                                      "parameter_list_opt");
    if (parameter_list != nullptr) {
        return parameter_list;
    }

    return ParseTreeMatcher::find_first_child_with_label(
        node, "function_parameter_list_opt");
}

void collect_declaration_type_qualifiers(const ParseTreeNode *node,
                                         bool &is_const, bool &is_volatile) {
    if (node == nullptr) {
        return;
    }

    for (const auto &child : node->children) {
        if (ParseTreeMatcher::label_equals(child.get(),
                                           "type_qualifier_seq_opt") ||
            ParseTreeMatcher::label_equals(child.get(), "type_qualifier_seq")) {
            collect_type_qualifiers(child.get(), is_const, is_volatile);
        }
    }
}

bool is_parameter_list_node(const ParseTreeNode *node) {
    return node != nullptr &&
           (ParseTreeMatcher::label_equals(node, "parameter_list_opt") ||
            ParseTreeMatcher::label_equals(node,
                                           "function_parameter_list_opt"));
}

bool extract_tag_identifier_name(const ParseTreeNode *node, std::string &name) {
    if (node == nullptr) {
        return false;
    }
    if (ParseTreeMatcher::label_starts_with(node, "IDENTIFIER")) {
        name = ParseTreeMatcher::extract_terminal_suffix(node, "IDENTIFIER");
        return !name.empty();
    }
    if (ParseTreeMatcher::label_starts_with(node, "TYPE_NAME")) {
        name = ParseTreeMatcher::extract_terminal_suffix(node, "TYPE_NAME");
        return !name.empty();
    }
    if (ParseTreeMatcher::label_equals(node, "tag_identifier")) {
        for (const auto &child : node->children) {
            if (extract_tag_identifier_name(child.get(), name)) {
                return true;
            }
        }
    }
    return false;
}

PointerNullabilityKind get_pointer_nullability_kind(const ParseTreeNode *node) {
    if (node == nullptr) {
        return PointerNullabilityKind::None;
    }

    if (ParseTreeMatcher::label_starts_with(node, "NULLABILITY")) {
        const std::string token_text =
            ParseTreeMatcher::extract_terminal_suffix(node, "NULLABILITY");
        if (token_text == "_Nullable") {
            return PointerNullabilityKind::Nullable;
        }
        if (token_text == "_Nonnull") {
            return PointerNullabilityKind::Nonnull;
        }
        if (token_text == "_Null_unspecified") {
            return PointerNullabilityKind::NullUnspecified;
        }
    }

    for (const auto &child : node->children) {
        if (ParseTreeMatcher::label_equals(child.get(), "pointer")) {
            continue;
        }
        const PointerNullabilityKind nested_kind =
            get_pointer_nullability_kind(child.get());
        if (nested_kind != PointerNullabilityKind::None) {
            return nested_kind;
        }
    }
    return PointerNullabilityKind::None;
}

void collect_type_qualifiers(const ParseTreeNode *node, bool &is_const,
                             bool &is_volatile) {
    if (node == nullptr) {
        return;
    }
    if (ParseTreeMatcher::label_starts_with(node, "CONST")) {
        is_const = true;
        return;
    }
    if (ParseTreeMatcher::label_starts_with(node, "VOLATILE")) {
        is_volatile = true;
        return;
    }
    for (const auto &child : node->children) {
        collect_type_qualifiers(child.get(), is_const, is_volatile);
    }
}

void collect_storage_specifiers(const ParseTreeNode *node,
                                StorageSpecifierFlags &flags) {
    if (node == nullptr) {
        return;
    }

    if (ParseTreeMatcher::label_starts_with(node, "EXTERN")) {
        flags.is_extern = true;
        return;
    }
    if (ParseTreeMatcher::label_starts_with(node, "STATIC")) {
        flags.is_static = true;
        return;
    }

    for (const auto &child : node->children) {
        collect_storage_specifiers(child.get(), flags);
    }
}

void collect_pointer_qualifiers(const ParseTreeNode *node, bool &is_const,
                                bool &is_volatile, bool &is_restrict) {
    if (node == nullptr) {
        return;
    }
    if (ParseTreeMatcher::label_starts_with(node, "CONST")) {
        is_const = true;
        return;
    }
    if (ParseTreeMatcher::label_starts_with(node, "RESTRICT")) {
        is_restrict = true;
        return;
    }
    if (ParseTreeMatcher::label_starts_with(node, "VOLATILE")) {
        is_volatile = true;
        return;
    }
    for (const auto &child : node->children) {
        if (ParseTreeMatcher::label_equals(child.get(), "pointer")) {
            continue;
        }
        collect_pointer_qualifiers(child.get(), is_const, is_volatile,
                                   is_restrict);
    }
}

bool has_struct_field_list(const ParseTreeNode *node) {
    const ParseTreeNode *struct_specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "struct_specifier");
    if (struct_specifier == nullptr) {
        return false;
    }
    const ParseTreeNode *field_list =
        ParseTreeMatcher::find_first_child_with_label(struct_specifier,
                                                      "struct_field_list_opt");
    return field_list != nullptr && !field_list->children.empty();
}

bool has_struct_tag_name(const ParseTreeNode *node) {
    const ParseTreeNode *struct_specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "struct_specifier");
    if (struct_specifier == nullptr) {
        return false;
    }
    for (const auto &child : struct_specifier->children) {
        std::string tag_name;
        if (extract_tag_identifier_name(child.get(), tag_name)) {
            return true;
        }
    }
    return false;
}

bool has_union_field_list(const ParseTreeNode *node) {
    const ParseTreeNode *union_specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "union_specifier");
    if (union_specifier == nullptr) {
        return false;
    }
    const ParseTreeNode *field_list =
        ParseTreeMatcher::find_first_child_with_label(union_specifier,
                                                      "union_field_list_opt");
    return field_list != nullptr && !field_list->children.empty();
}

bool has_union_tag_name(const ParseTreeNode *node) {
    const ParseTreeNode *union_specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "union_specifier");
    if (union_specifier == nullptr) {
        return false;
    }
    for (const auto &child : union_specifier->children) {
        std::string tag_name;
        if (extract_tag_identifier_name(child.get(), tag_name)) {
            return true;
        }
    }
    return false;
}

bool has_enumerator_list(const ParseTreeNode *node) {
    const ParseTreeNode *enum_specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "enum_specifier");
    if (enum_specifier == nullptr) {
        return false;
    }
    const ParseTreeNode *enumerator_list =
        ParseTreeMatcher::find_first_child_with_label(enum_specifier,
                                                      "enumerator_list_opt");
    return enumerator_list != nullptr && !enumerator_list->children.empty();
}

bool has_enum_tag_name(const ParseTreeNode *node) {
    const ParseTreeNode *enum_specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "enum_specifier");
    if (enum_specifier == nullptr) {
        return false;
    }
    for (const auto &child : enum_specifier->children) {
        std::string tag_name;
        if (extract_tag_identifier_name(child.get(), tag_name)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::unique_ptr<TranslationUnit>
AstBuilder::build(const AstBuilderContext &context) const {
    const ParseTreeNode *parse_tree_root = context.get_parse_tree_root();
    auto translation_unit = std::make_unique<TranslationUnit>(
        get_node_source_span(parse_tree_root));
    if (parse_tree_root == nullptr) {
        return translation_unit;
    }

    std::vector<const ParseTreeNode *> top_level_items;
    collect_top_level_items(parse_tree_root, top_level_items);
    for (const ParseTreeNode *item : top_level_items) {
        add_top_level_decls(item, *translation_unit);
    }
    return translation_unit;
}

void AstBuilder::collect_top_level_items(
    const ParseTreeNode *node,
    std::vector<const ParseTreeNode *> &items) const {
    if (node == nullptr) {
        return;
    }
    if (ParseTreeMatcher::label_equals(node, "comp_unit_item")) {
        items.push_back(node);
        return;
    }
    for (const auto &child : node->children) {
        collect_top_level_items(child.get(), items);
    }
}

void AstBuilder::add_top_level_decls(const ParseTreeNode *node,
                                     TranslationUnit &translation_unit) const {
    if (node == nullptr) {
        translation_unit.add_top_level_decl(
            std::make_unique<UnknownDecl>("null top-level item"));
        return;
    }

    if (ParseTreeMatcher::find_first_child_with_label(node, "empty_decl")) {
        return;
    }

    if (const ParseTreeNode *function_node =
            ParseTreeMatcher::find_first_child_with_label(node, "func_def")) {
        translation_unit.add_top_level_decl(build_function_decl(function_node));
        return;
    }

    if (const ParseTreeNode *function_node =
            ParseTreeMatcher::find_first_child_with_label(node, "func_decl")) {
        translation_unit.add_top_level_decl(build_function_decl(function_node));
        return;
    }

    if (const ParseTreeNode *decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "decl")) {
        for (auto &decl : build_decl_group(decl_node)) {
            translation_unit.add_top_level_decl(std::move(decl));
        }
        return;
    }

    translation_unit.add_top_level_decl(
        std::make_unique<UnknownDecl>(node->label));
}

std::unique_ptr<FunctionDecl>
AstBuilder::build_function_decl(const ParseTreeNode *node) const {
    std::string function_name = "<anonymous>";
    std::unique_ptr<TypeNode> return_type =
        std::make_unique<UnknownTypeNode>("unknown");
    std::vector<std::unique_ptr<Decl>> parameters;
    bool is_variadic = false;
    std::vector<ParsedAttribute> merged_attributes;
    std::string asm_label;
    std::unique_ptr<Stmt> body;
    TypeQualifierFlags leading_qualifiers;
    StorageSpecifierFlags storage_specifiers;

    if (node != nullptr) {
        const ParseTreeNode *type_specifier = nullptr;
        const ParseTreeNode *function_declarator = nullptr;
        for (const auto &child : node->children) {
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "attribute_specifier_seq_opt")) {
                append_attributes(build_decl_attributes(child.get()),
                                  merged_attributes);
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "type_qualifier_seq_opt") ||
                ParseTreeMatcher::label_equals(child.get(),
                                               "type_qualifier_seq")) {
                collect_type_qualifiers(child.get(),
                                        leading_qualifiers.is_const,
                                        leading_qualifiers.is_volatile);
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "storage_specifier_opt")) {
                collect_storage_specifiers(child.get(), storage_specifiers);
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(), "type_specifier")) {
                type_specifier = child.get();
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "function_declarator")) {
                function_declarator = child.get();
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(), "asm_label_opt")) {
                asm_label = build_asm_label_text(child.get());
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(), "block")) {
                body = build_block_stmt(child.get());
            }
        }

        if (function_declarator != nullptr) {
            function_name = extract_declarator_name(function_declarator);
            const ParseTreeNode *parameter_list_like_node = nullptr;
            if (function_declarator->children.size() == 7 &&
                ParseTreeMatcher::label_starts_with(
                    function_declarator->children[0].get(), "LPAREN") &&
                ParseTreeMatcher::label_equals(
                    function_declarator->children[1].get(), "pointer") &&
                ParseTreeMatcher::label_equals(
                    function_declarator->children[2].get(),
                    "function_declarator") &&
                ParseTreeMatcher::label_starts_with(
                    function_declarator->children[3].get(), "RPAREN") &&
                ParseTreeMatcher::label_starts_with(
                    function_declarator->children[4].get(), "LPAREN") &&
                is_parameter_list_node(
                    function_declarator->children[5].get()) &&
                ParseTreeMatcher::label_starts_with(
                    function_declarator->children[6].get(), "RPAREN")) {
                auto return_function_type = std::make_unique<FunctionTypeNode>(
                    build_declared_type(type_specifier, nullptr,
                                        leading_qualifiers.is_const,
                                        leading_qualifiers.is_volatile, true),
                    build_function_parameter_types(
                        function_declarator->children[5].get()),
                    has_variadic_marker(function_declarator->children[5].get()),
                    get_node_source_span(function_declarator));
                return_type =
                    build_pointer_type(std::move(return_function_type),
                                       function_declarator->children[1].get());
                parameter_list_like_node = find_parameter_list_node(
                    function_declarator->children[2].get());
            } else {
                const ParseTreeNode *pointer =
                    ParseTreeMatcher::find_first_child_with_label(
                        function_declarator, "pointer");
                return_type = build_declared_type(
                    type_specifier, pointer, leading_qualifiers.is_const,
                    leading_qualifiers.is_volatile, true);
                parameter_list_like_node =
                    find_parameter_list_node(function_declarator);
            }
            parameters = build_parameters(parameter_list_like_node);
            is_variadic = has_variadic_marker(parameter_list_like_node);
        } else if (type_specifier != nullptr) {
            return_type = build_declared_type(
                type_specifier, nullptr, leading_qualifiers.is_const,
                leading_qualifiers.is_volatile, true);
        }
    }

    ParsedAttributeList attributes;
    if (!merged_attributes.empty()) {
        attributes = ParsedAttributeList(AttributeAttachmentSite::DeclSpecifier,
                                         std::move(merged_attributes),
                                         get_node_source_span(node));
    }

    return std::make_unique<FunctionDecl>(
        function_name, std::move(return_type), std::move(parameters),
        storage_specifiers.is_static, is_variadic, std::move(attributes),
        std::move(asm_label), std::move(body), get_node_source_span(node));
}

ParsedAttributeList
AstBuilder::build_decl_attributes(const ParseTreeNode *node) const {
    AttributeParser attribute_parser;
    return attribute_parser.parse_gnu_attribute_specifier_seq(
        node, AttributeAttachmentSite::DeclSpecifier);
}

std::vector<std::unique_ptr<Decl>>
AstBuilder::build_parameters(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Decl>> parameters;
    std::vector<const ParseTreeNode *> stack;
    if (node != nullptr) {
        stack.push_back(node);
    }

    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(current, "parameter_decl")) {
            const ParseTreeNode *type_specifier =
                ParseTreeMatcher::find_first_child_with_label(current,
                                                              "type_specifier");
            if (type_specifier == nullptr) {
                type_specifier = ParseTreeMatcher::find_first_child_with_label(
                    current, "nonvoid_type_specifier");
            }
            if (type_specifier == nullptr) {
                for (const auto &child : current->children) {
                    if (ParseTreeMatcher::label_starts_with(child.get(),
                                                            "TYPE_NAME")) {
                        type_specifier = child.get();
                        break;
                    }
                }
            }
            const ParseTreeNode *declarator =
                ParseTreeMatcher::find_first_child_with_label(current,
                                                              "declarator");
            const ParseTreeNode *pointer =
                ParseTreeMatcher::find_first_child_with_label(current,
                                                              "pointer");
            TypeQualifierFlags pointee_qualifiers;
            for (const auto &child : current->children) {
                if (ParseTreeMatcher::label_equals(child.get(),
                                                   "type_qualifier_seq_opt") ||
                    ParseTreeMatcher::label_equals(child.get(),
                                                   "type_qualifier_seq")) {
                    collect_type_qualifiers(child.get(),
                                            pointee_qualifiers.is_const,
                                            pointee_qualifiers.is_volatile);
                }
            }
            std::string parameter_name = extract_declarator_name(declarator);
            if (parameter_name == "<unnamed>") {
                parameter_name.clear();
            }
            parameters.push_back(std::make_unique<ParamDecl>(
                std::move(parameter_name),
                build_declared_type(type_specifier,
                                    declarator == nullptr ? pointer
                                                          : declarator,
                                    pointee_qualifiers.is_const,
                                    pointee_qualifiers.is_volatile, true),
                collect_declarator_dimensions(declarator),
                get_node_source_span(current)));
            continue;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }

    return parameters;
}

std::vector<std::unique_ptr<Decl>>
AstBuilder::build_decl_group(const ParseTreeNode *node) const {
    if (node == nullptr) {
        std::vector<std::unique_ptr<Decl>> decls;
        decls.push_back(std::make_unique<UnknownDecl>("null decl"));
        return decls;
    }
    if (ParseTreeMatcher::label_equals(node, "const_decl")) {
        return build_const_decls(node);
    }
    if (ParseTreeMatcher::label_equals(node, "var_decl")) {
        return build_var_decls(node);
    }
    if (ParseTreeMatcher::label_equals(node, "typedef_decl")) {
        return build_typedef_decls(node);
    }
    if (ParseTreeMatcher::label_equals(node, "struct_decl")) {
        std::vector<std::unique_ptr<Decl>> decls;
        decls.push_back(build_struct_decl(node));
        return decls;
    }
    if (ParseTreeMatcher::label_equals(node, "union_decl")) {
        std::vector<std::unique_ptr<Decl>> decls;
        decls.push_back(build_union_decl(node));
        return decls;
    }
    if (ParseTreeMatcher::label_equals(node, "enum_decl")) {
        std::vector<std::unique_ptr<Decl>> decls;
        decls.push_back(build_enum_decl(node));
        return decls;
    }
    if (const ParseTreeNode *const_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "const_decl")) {
        return build_const_decls(const_decl_node);
    }
    if (const ParseTreeNode *var_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "var_decl")) {
        return build_var_decls(var_decl_node);
    }
    if (const ParseTreeNode *typedef_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node,
                                                          "typedef_decl")) {
        return build_typedef_decls(typedef_decl_node);
    }
    if (const ParseTreeNode *struct_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node,
                                                          "struct_decl")) {
        std::vector<std::unique_ptr<Decl>> decls;
        decls.push_back(build_struct_decl(struct_decl_node));
        return decls;
    }
    if (const ParseTreeNode *union_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "union_decl")) {
        std::vector<std::unique_ptr<Decl>> decls;
        decls.push_back(build_union_decl(union_decl_node));
        return decls;
    }
    if (const ParseTreeNode *enum_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "enum_decl")) {
        std::vector<std::unique_ptr<Decl>> decls;
        decls.push_back(build_enum_decl(enum_decl_node));
        return decls;
    }
    std::vector<std::unique_ptr<Decl>> decls;
    decls.push_back(std::make_unique<UnknownDecl>(node->label));
    return decls;
}

std::vector<std::unique_ptr<Decl>>
AstBuilder::build_const_decls(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Decl>> decls;
    std::unique_ptr<TypeNode> shared_type =
        std::make_unique<UnknownTypeNode>("unknown");
    const ParseTreeNode *list_node = nullptr;
    const ParseTreeNode *type_specifier = nullptr;

    if (node != nullptr) {
        for (const auto &child : node->children) {
            if (ParseTreeMatcher::label_equals(child.get(), "type_specifier")) {
                type_specifier = child.get();
                shared_type = build_return_type(child.get());
            } else if (ParseTreeMatcher::label_equals(
                           child.get(), "const_init_declarator_list")) {
                list_node = child.get();
            }
        }
    }

    if (has_struct_field_list(type_specifier) &&
        has_struct_tag_name(type_specifier)) {
        decls.push_back(build_struct_decl(type_specifier));
    }
    if (has_union_field_list(type_specifier) &&
        has_union_tag_name(type_specifier)) {
        decls.push_back(build_union_decl(type_specifier));
    }
    if (has_enumerator_list(type_specifier) &&
        has_enum_tag_name(type_specifier)) {
        decls.push_back(build_enum_decl(type_specifier));
    }

    std::vector<const ParseTreeNode *> declarators;
    std::vector<const ParseTreeNode *> stack;
    if (list_node != nullptr) {
        stack.push_back(list_node);
    }
    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(current, "const_init_declarator")) {
            declarators.push_back(current);
            continue;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }

    for (const ParseTreeNode *declarator_node : declarators) {
        const ParseTreeNode *declarator =
            ParseTreeMatcher::find_first_child_with_label(declarator_node,
                                                          "declarator");
        const ParseTreeNode *initializer =
            ParseTreeMatcher::find_first_child_with_label(declarator_node,
                                                          "init_val");
        decls.push_back(std::make_unique<ConstDecl>(
            extract_declarator_name(declarator),
            build_declared_type(type_specifier, declarator, true, false,
                                !has_enum_tag_name(type_specifier)),
            collect_declarator_dimensions(declarator), build_expr(initializer),
            get_node_source_span(declarator_node)));
    }

    if (decls.empty()) {
        decls.push_back(std::make_unique<UnknownDecl>("const_decl"));
    }
    return decls;
}

std::vector<std::unique_ptr<Decl>>
AstBuilder::build_var_decls(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Decl>> decls;
    const ParseTreeNode *list_node = nullptr;
    const ParseTreeNode *type_specifier = nullptr;
    StorageSpecifierFlags storage_specifiers;
    TypeQualifierFlags leading_qualifiers;

    if (node != nullptr) {
        for (const auto &child : node->children) {
            if (ParseTreeMatcher::label_equals(child.get(), "type_specifier")) {
                type_specifier = child.get();
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "init_declarator_list")) {
                list_node = child.get();
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "storage_specifier_opt")) {
                collect_storage_specifiers(child.get(), storage_specifiers);
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "type_qualifier_seq_opt") ||
                ParseTreeMatcher::label_equals(child.get(),
                                               "type_qualifier_seq")) {
                collect_type_qualifiers(child.get(),
                                        leading_qualifiers.is_const,
                                        leading_qualifiers.is_volatile);
            }
        }
    }

    if (has_struct_field_list(type_specifier) &&
        has_struct_tag_name(type_specifier)) {
        decls.push_back(build_struct_decl(type_specifier));
    }
    if (has_union_field_list(type_specifier) &&
        has_union_tag_name(type_specifier)) {
        decls.push_back(build_union_decl(type_specifier));
    }
    if (has_enumerator_list(type_specifier) &&
        has_enum_tag_name(type_specifier)) {
        decls.push_back(build_enum_decl(type_specifier));
    }

    std::vector<const ParseTreeNode *> declarators;
    std::vector<const ParseTreeNode *> stack;
    if (list_node != nullptr) {
        stack.push_back(list_node);
    }
    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(current, "init_declarator")) {
            declarators.push_back(current);
            continue;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }

    for (const ParseTreeNode *declarator_node : declarators) {
        const ParseTreeNode *declarator =
            ParseTreeMatcher::find_first_child_with_label(declarator_node,
                                                          "declarator");
        const ParseTreeNode *initializer =
            ParseTreeMatcher::find_first_child_with_label(declarator_node,
                                                          "init_val");
        decls.push_back(std::make_unique<VarDecl>(
            extract_declarator_name(declarator),
            build_declared_type(type_specifier, declarator,
                                leading_qualifiers.is_const,
                                leading_qualifiers.is_volatile,
                                !has_enum_tag_name(type_specifier)),
            collect_declarator_dimensions(declarator),
            initializer == nullptr ? nullptr : build_expr(initializer),
            storage_specifiers.is_extern, storage_specifiers.is_static,
            get_node_source_span(declarator_node)));
    }

    if (decls.empty()) {
        decls.push_back(std::make_unique<UnknownDecl>("var_decl"));
    }
    return decls;
}

std::vector<std::unique_ptr<Decl>>
AstBuilder::build_typedef_decls(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Decl>> decls;
    TypeQualifierFlags leading_qualifiers;
    const ParseTreeNode *type_specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "type_specifier");
    const ParseTreeNode *declarator_list =
        ParseTreeMatcher::find_first_child_with_label(
            node, "typedef_declarator_list");
    const ParseTreeNode *function_declarator =
        ParseTreeMatcher::find_first_child_with_label(node,
                                                      "function_declarator");
    if (node != nullptr) {
        for (const auto &child : node->children) {
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "type_qualifier_seq_opt") ||
                ParseTreeMatcher::label_equals(child.get(),
                                               "type_qualifier_seq")) {
                collect_type_qualifiers(child.get(),
                                        leading_qualifiers.is_const,
                                        leading_qualifiers.is_volatile);
            }
        }
    }
    if (has_struct_field_list(type_specifier) &&
        has_struct_tag_name(type_specifier)) {
        decls.push_back(build_struct_decl(type_specifier));
    }
    if (has_union_field_list(type_specifier) &&
        has_union_tag_name(type_specifier)) {
        decls.push_back(build_union_decl(type_specifier));
    }
    if (has_enumerator_list(type_specifier) &&
        has_enum_tag_name(type_specifier)) {
        decls.push_back(build_enum_decl(type_specifier));
    }
    std::vector<const ParseTreeNode *> declarators;
    std::vector<const ParseTreeNode *> stack;
    if (declarator_list != nullptr) {
        stack.push_back(declarator_list);
    }
    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(current, "declarator")) {
            declarators.push_back(current);
            continue;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }

    decls.reserve(decls.size() + declarators.size());
    for (const ParseTreeNode *declarator : declarators) {
        decls.push_back(std::make_unique<TypedefDecl>(
            extract_declarator_name(declarator),
            build_declared_type(type_specifier, declarator,
                                leading_qualifiers.is_const,
                                leading_qualifiers.is_volatile,
                                !has_enum_tag_name(type_specifier)),
            collect_declarator_dimensions(declarator),
            get_node_source_span(declarator)));
    }

    if (declarators.empty() && function_declarator != nullptr) {
        TypeQualifierFlags return_qualifiers = leading_qualifiers;
        const ParseTreeNode *return_pointer =
            ParseTreeMatcher::find_first_child_with_label(function_declarator,
                                                          "pointer");
        auto return_type = build_declared_type(type_specifier, return_pointer,
                                               return_qualifiers.is_const,
                                               return_qualifiers.is_volatile);
        const ParseTreeNode *parameter_list_like_node =
            find_parameter_list_node(function_declarator);
        decls.push_back(std::make_unique<TypedefDecl>(
            extract_declarator_name(function_declarator),
            std::make_unique<FunctionTypeNode>(
                std::move(return_type),
                build_function_parameter_types(parameter_list_like_node),
                has_variadic_marker(parameter_list_like_node),
                get_node_source_span(function_declarator)),
            std::vector<std::unique_ptr<Expr>>(),
            get_node_source_span(function_declarator)));
    }

    if (decls.empty()) {
        decls.push_back(std::make_unique<UnknownDecl>(
            "typedef_decl", get_node_source_span(node)));
    }
    return decls;
}

std::unique_ptr<StructDecl>
AstBuilder::build_struct_decl(const ParseTreeNode *node) const {
    const ParseTreeNode *specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "struct_specifier");
    std::string name = "<anonymous>";
    if (specifier != nullptr) {
        for (const auto &child : specifier->children) {
            std::string tag_name;
            if (extract_tag_identifier_name(child.get(), tag_name)) {
                name = tag_name;
                break;
            }
        }
    }

    auto struct_decl =
        std::make_unique<StructDecl>(name, get_node_source_span(specifier));
    if (specifier != nullptr) {
        for (const auto &child : specifier->children) {
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "struct_field_list_opt")) {
                for (auto &field : build_struct_fields(child.get())) {
                    struct_decl->add_field(std::move(field));
                }
            }
        }
    }
    return struct_decl;
}

std::unique_ptr<UnionDecl>
AstBuilder::build_union_decl(const ParseTreeNode *node) const {
    const ParseTreeNode *specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "union_specifier");
    std::string name = "<anonymous>";
    if (specifier != nullptr) {
        for (const auto &child : specifier->children) {
            std::string tag_name;
            if (extract_tag_identifier_name(child.get(), tag_name)) {
                name = tag_name;
                break;
            }
        }
    }

    auto union_decl =
        std::make_unique<UnionDecl>(name, get_node_source_span(specifier));
    if (specifier != nullptr) {
        for (const auto &child : specifier->children) {
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "union_field_list_opt")) {
                for (auto &field : build_union_fields(child.get())) {
                    union_decl->add_field(std::move(field));
                }
            }
        }
    }
    return union_decl;
}

std::unique_ptr<EnumDecl>
AstBuilder::build_enum_decl(const ParseTreeNode *node) const {
    const ParseTreeNode *specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "enum_specifier");
    std::string name = "<anonymous>";
    if (specifier != nullptr) {
        for (const auto &child : specifier->children) {
            std::string tag_name;
            if (extract_tag_identifier_name(child.get(), tag_name)) {
                name = tag_name;
                break;
            }
        }
    }

    auto enum_decl =
        std::make_unique<EnumDecl>(name, get_node_source_span(specifier));
    if (specifier != nullptr) {
        for (const auto &child : specifier->children) {
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "enumerator_list_opt")) {
                for (auto &enumerator : build_enumerators(child.get())) {
                    enum_decl->add_enumerator(std::move(enumerator));
                }
            }
        }
    }
    return enum_decl;
}

std::unique_ptr<TypeNode>
AstBuilder::build_return_type(const ParseTreeNode *node,
                              bool include_inline_enum_enumerators) const {
    if (node == nullptr) {
        return std::make_unique<UnknownTypeNode>("null type_specifier");
    }

    if (const ParseTreeNode *typeof_type =
            ParseTreeMatcher::find_first_child_with_label(
                node, "gnu_typeof_type_specifier")) {
        if (!typeof_type->children.empty() &&
            ParseTreeMatcher::label_starts_with(typeof_type->children[0].get(),
                                                "TYPEOF")) {
            std::unique_ptr<Expr> operand =
                typeof_type->children.size() >= 3
                    ? build_expr(typeof_type->children[2].get())
                    : std::unique_ptr<Expr>(std::make_unique<UnknownExpr>(
                          "missing typeof operand",
                          get_node_source_span(typeof_type)));
            return std::make_unique<TypeofTypeNode>(
                std::move(operand), get_node_source_span(typeof_type));
        }
        return std::make_unique<UnknownTypeNode>(
            "unsupported typeof-like type specifier",
            get_node_source_span(typeof_type));
    }

    if (ParseTreeMatcher::label_starts_with(node, "TYPE_NAME")) {
        const std::string suffix =
            ParseTreeMatcher::extract_terminal_suffix(node, "TYPE_NAME");
        const std::string builtin_name =
            builtin_name_from_type_name_token(node);
        if (!builtin_name.empty()) {
            return std::make_unique<BuiltinTypeNode>(
                builtin_name, get_node_source_span(node));
        }
        if (!suffix.empty()) {
            return std::make_unique<NamedTypeNode>(suffix,
                                                   get_node_source_span(node));
        }
    }

    if (const ParseTreeNode *basic_type =
            ParseTreeMatcher::find_first_child_with_label(node, "basic_type")) {
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "LONG") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "DOUBLE")) {
            return std::make_unique<BuiltinTypeNode>(
                "long double", get_node_source_span(node));
        }
        if (basic_type->children.size() == 1 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SIGNED")) {
            return std::make_unique<BuiltinTypeNode>(
                "int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SIGNED") &&
            is_int_type_token(basic_type->children[1].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "CHAR")) {
            return std::make_unique<BuiltinTypeNode>(
                "signed char", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG")) {
            return std::make_unique<BuiltinTypeNode>(
                "long int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 3 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG") &&
            is_int_type_token(basic_type->children[2].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "long int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 3 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[2].get(),
                                                "LONG")) {
            return std::make_unique<BuiltinTypeNode>(
                "long long int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 4 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[2].get(),
                                                "LONG") &&
            is_int_type_token(basic_type->children[3].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "long long int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 1 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SHORT")) {
            return std::make_unique<BuiltinTypeNode>(
                "short", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SHORT") &&
            is_int_type_token(basic_type->children[1].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "short", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "SHORT")) {
            return std::make_unique<BuiltinTypeNode>(
                "short", get_node_source_span(node));
        }
        if (basic_type->children.size() == 3 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "SIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "SHORT") &&
            is_int_type_token(basic_type->children[2].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "short", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "CHAR")) {
            return std::make_unique<BuiltinTypeNode>(
                "unsigned char", get_node_source_span(node));
        }
        if (basic_type->children.size() == 3 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED") &&
            ParseTreeMatcher::label_equals(basic_type->children[1].get(),
                                           "type_qualifier_seq") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[2].get(),
                                                "CHAR")) {
            TypeQualifierFlags qualifiers;
            collect_type_qualifiers(basic_type->children[1].get(),
                                    qualifiers.is_const,
                                    qualifiers.is_volatile);
            auto base_type = std::make_unique<BuiltinTypeNode>(
                "unsigned char", get_node_source_span(node));
            if (qualifiers.is_const || qualifiers.is_volatile) {
                return std::make_unique<QualifiedTypeNode>(
                    qualifiers.is_const, qualifiers.is_volatile,
                    std::move(base_type), get_node_source_span(node));
            }
            return base_type;
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "SHORT")) {
            return std::make_unique<BuiltinTypeNode>(
                "unsigned short", get_node_source_span(node));
        }
        if (basic_type->children.size() == 3 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "SHORT") &&
            is_int_type_token(basic_type->children[2].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "unsigned short", get_node_source_span(node));
        }
        if (basic_type->children.size() == 1 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "LONG")) {
            return std::make_unique<BuiltinTypeNode>(
                "long int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "LONG") &&
            is_int_type_token(basic_type->children[1].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "long int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "LONG") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG")) {
            return std::make_unique<BuiltinTypeNode>(
                "long long int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 3 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "LONG") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG") &&
            is_int_type_token(basic_type->children[2].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "long long int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 1 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED")) {
            return std::make_unique<BuiltinTypeNode>(
                "unsigned int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG")) {
            return std::make_unique<BuiltinTypeNode>(
                "unsigned long", get_node_source_span(node));
        }
        if (basic_type->children.size() == 3 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG") &&
            is_int_type_token(basic_type->children[2].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "unsigned long", get_node_source_span(node));
        }
        if (basic_type->children.size() == 2 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED") &&
            is_int_type_token(basic_type->children[1].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "unsigned int", get_node_source_span(node));
        }
        if (basic_type->children.size() == 3 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[2].get(),
                                                "LONG")) {
            return std::make_unique<BuiltinTypeNode>(
                "unsigned long long", get_node_source_span(node));
        }
        if (basic_type->children.size() == 4 &&
            ParseTreeMatcher::label_starts_with(basic_type->children[0].get(),
                                                "UNSIGNED") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[1].get(),
                                                "LONG") &&
            ParseTreeMatcher::label_starts_with(basic_type->children[2].get(),
                                                "LONG") &&
            is_int_type_token(basic_type->children[3].get())) {
            return std::make_unique<BuiltinTypeNode>(
                "unsigned long long", get_node_source_span(node));
        }
        for (const auto &child : basic_type->children) {
            if (ParseTreeMatcher::label_starts_with(child.get(), "INT")) {
                return std::make_unique<BuiltinTypeNode>(
                    "int", get_node_source_span(node));
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "CHAR")) {
                return std::make_unique<BuiltinTypeNode>(
                    "char", get_node_source_span(node));
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "FLOAT16")) {
                return std::make_unique<BuiltinTypeNode>(
                    "_Float16", get_node_source_span(node));
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "FLOAT")) {
                return std::make_unique<BuiltinTypeNode>(
                    "float", get_node_source_span(node));
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "DOUBLE")) {
                return std::make_unique<BuiltinTypeNode>(
                    "double", get_node_source_span(node));
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "VOID")) {
                return std::make_unique<BuiltinTypeNode>(
                    "void", get_node_source_span(node));
            }
        }
    }

    if (const ParseTreeNode *struct_specifier =
            ParseTreeMatcher::find_first_child_with_label(node,
                                                          "struct_specifier")) {
        std::string name = "<anonymous>";
        std::vector<std::unique_ptr<Decl>> fields;
        for (const auto &child : struct_specifier->children) {
            std::string tag_name;
            if (extract_tag_identifier_name(child.get(), tag_name)) {
                name = tag_name;
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "struct_field_list_opt")) {
                fields = build_struct_fields(child.get());
            }
        }
        return std::make_unique<StructTypeNode>(
            name, std::move(fields), get_node_source_span(struct_specifier));
    }

    for (const auto &child : node->children) {
        if (ParseTreeMatcher::label_starts_with(child.get(), "TYPE_NAME")) {
            const std::string suffix =
                ParseTreeMatcher::extract_terminal_suffix(child.get(),
                                                          "TYPE_NAME");
            const std::string builtin_name =
                builtin_name_from_type_name_token(child.get());
            if (!builtin_name.empty()) {
                return std::make_unique<BuiltinTypeNode>(
                    builtin_name, get_node_source_span(node));
            }
            if (!suffix.empty()) {
                return std::make_unique<NamedTypeNode>(
                    suffix, get_node_source_span(node));
            }
        }
    }

    if (const ParseTreeNode *union_specifier =
            ParseTreeMatcher::find_first_child_with_label(node,
                                                          "union_specifier")) {
        std::string name = "<anonymous>";
        std::vector<std::unique_ptr<Decl>> fields;
        for (const auto &child : union_specifier->children) {
            std::string tag_name;
            if (extract_tag_identifier_name(child.get(), tag_name)) {
                name = tag_name;
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "union_field_list_opt")) {
                fields = build_union_fields(child.get());
            }
        }
        return std::make_unique<UnionTypeNode>(
            name, std::move(fields), get_node_source_span(union_specifier));
    }

    if (const ParseTreeNode *enum_specifier =
            ParseTreeMatcher::find_first_child_with_label(node,
                                                          "enum_specifier")) {
        std::string name = "<anonymous>";
        std::vector<std::unique_ptr<Decl>> enumerators;
        for (const auto &child : enum_specifier->children) {
            std::string tag_name;
            if (extract_tag_identifier_name(child.get(), tag_name)) {
                name = tag_name;
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "enumerator_list_opt") &&
                include_inline_enum_enumerators) {
                enumerators = build_enumerators(child.get());
            }
        }
        return std::make_unique<EnumTypeNode>(
            name, std::move(enumerators), get_node_source_span(enum_specifier));
    }

    return std::make_unique<UnknownTypeNode>(node->label,
                                             get_node_source_span(node));
}

// `type_specifier` and `declarator` are distinct parse-tree roles despite
// sharing the same pointer type.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::unique_ptr<TypeNode>
AstBuilder::build_declared_type(const ParseTreeNode *type_specifier,
                                const ParseTreeNode *declarator,
                                bool pointee_is_const, bool pointee_is_volatile,
                                bool include_inline_enum_enumerators)
    const { // NOLINT(bugprone-easily-swappable-parameters)
    std::unique_ptr<TypeNode> declared_type =
        build_return_type(type_specifier, include_inline_enum_enumerators);
    if (pointee_is_const || pointee_is_volatile) {
        declared_type = std::make_unique<QualifiedTypeNode>(
            pointee_is_const, pointee_is_volatile, std::move(declared_type),
            type_specifier == nullptr ? get_node_source_span(declarator)
                                      : get_node_source_span(type_specifier));
    }
    if (declarator == nullptr) {
        return declared_type;
    }

    return build_declarator_type(std::move(declared_type), declarator);
}

std::unique_ptr<TypeNode>
AstBuilder::build_declarator_type(std::unique_ptr<TypeNode> base_type,
                                  const ParseTreeNode *declarator) const {
    if (declarator == nullptr) {
        return base_type;
    }

    if (ParseTreeMatcher::label_equals(declarator, "pointer")) {
        return build_pointer_type(std::move(base_type), declarator);
    }

    if (ParseTreeMatcher::label_equals(declarator, "direct_declarator")) {
        return build_direct_declarator_type(std::move(base_type), declarator);
    }

    if (!ParseTreeMatcher::label_equals(declarator, "declarator")) {
        return base_type;
    }

    const ParseTreeNode *pointer_node = nullptr;
    const ParseTreeNode *direct_declarator = nullptr;
    for (const auto &child : declarator->children) {
        if (ParseTreeMatcher::label_equals(child.get(), "pointer")) {
            pointer_node = child.get();
        } else if (ParseTreeMatcher::label_equals(child.get(),
                                                  "direct_declarator")) {
            direct_declarator = child.get();
        }
    }

    std::unique_ptr<TypeNode> declared_type = std::move(base_type);
    if (pointer_node != nullptr && direct_declarator != nullptr &&
        direct_declarator->children.size() == 6 &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[0].get(), "LPAREN") &&
        ParseTreeMatcher::label_equals(direct_declarator->children[1].get(),
                                       "pointer") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[2].get(), "RPAREN") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[3].get(), "LPAREN") &&
        is_parameter_list_node(direct_declarator->children[4].get()) &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[5].get(), "RPAREN")) {
        declared_type =
            build_pointer_type(std::move(declared_type), pointer_node);
        return build_direct_declarator_type(std::move(declared_type),
                                            direct_declarator);
    }

    if (pointer_node != nullptr && direct_declarator != nullptr &&
        direct_declarator->children.size() == 7 &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[0].get(), "LPAREN") &&
        ParseTreeMatcher::label_equals(direct_declarator->children[1].get(),
                                       "pointer") &&
        ParseTreeMatcher::label_equals(direct_declarator->children[2].get(),
                                       "direct_declarator") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[3].get(), "RPAREN") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[4].get(), "LPAREN") &&
        is_parameter_list_node(direct_declarator->children[5].get()) &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[6].get(), "RPAREN")) {
        declared_type =
            build_pointer_type(std::move(declared_type), pointer_node);
        return build_direct_declarator_type(std::move(declared_type),
                                            direct_declarator);
    }

    if (direct_declarator != nullptr) {
        declared_type = build_direct_declarator_type(std::move(declared_type),
                                                     direct_declarator);
    }

    if (pointer_node != nullptr) {
        declared_type =
            build_pointer_type(std::move(declared_type), pointer_node);
    }

    return declared_type;
}

std::unique_ptr<TypeNode>
AstBuilder::build_pointer_type(std::unique_ptr<TypeNode> base_type,
                               const ParseTreeNode *pointer) const {
    if (pointer == nullptr ||
        !ParseTreeMatcher::label_equals(pointer, "pointer")) {
        return base_type;
    }

    const ParseTreeNode *nested_pointer = nullptr;
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;
    PointerNullabilityKind nullability_kind = PointerNullabilityKind::None;
    for (const auto &child : pointer->children) {
        if (ParseTreeMatcher::label_equals(child.get(), "pointer")) {
            nested_pointer = child.get();
            continue;
        }
        if (ParseTreeMatcher::label_equals(child.get(), "pointer_level")) {
            collect_pointer_qualifiers(child.get(), is_const, is_volatile,
                                       is_restrict);
            const PointerNullabilityKind nested_kind =
                get_pointer_nullability_kind(child.get());
            if (nested_kind != PointerNullabilityKind::None) {
                nullability_kind = nested_kind;
            }
            continue;
        }
        collect_pointer_qualifiers(child.get(), is_const, is_volatile,
                                   is_restrict);
        const PointerNullabilityKind nested_kind =
            get_pointer_nullability_kind(child.get());
        if (nested_kind != PointerNullabilityKind::None) {
            nullability_kind = nested_kind;
        }
    }

    std::unique_ptr<TypeNode> pointer_type = std::make_unique<PointerTypeNode>(
        std::move(base_type), get_node_source_span(pointer), is_const,
        is_volatile, is_restrict, nullability_kind);
    if (nested_pointer != nullptr) {
        return build_pointer_type(std::move(pointer_type), nested_pointer);
    }

    return pointer_type;
}

std::unique_ptr<TypeNode> AstBuilder::build_direct_declarator_type(
    std::unique_ptr<TypeNode> base_type,
    const ParseTreeNode *direct_declarator) const {
    if (direct_declarator == nullptr ||
        !ParseTreeMatcher::label_equals(direct_declarator,
                                        "direct_declarator")) {
        return base_type;
    }

    if (direct_declarator->children.size() == 1 &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[0].get(), "IDENTIFIER")) {
        return base_type;
    }

    if (direct_declarator->children.size() == 3 &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[0].get(), "LPAREN") &&
        ParseTreeMatcher::label_equals(direct_declarator->children[1].get(),
                                       "declarator") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[2].get(), "RPAREN")) {
        return build_declarator_type(std::move(base_type),
                                     direct_declarator->children[1].get());
    }

    if (direct_declarator->children.size() >= 4 &&
        ParseTreeMatcher::label_equals(direct_declarator->children[0].get(),
                                       "direct_declarator") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[1].get(), "LBRACKET")) {
        return build_direct_declarator_type(
            std::move(base_type), direct_declarator->children[0].get());
    }

    if (direct_declarator->children.size() == 6 &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[0].get(), "LPAREN") &&
        ParseTreeMatcher::label_equals(direct_declarator->children[1].get(),
                                       "pointer") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[2].get(), "RPAREN") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[3].get(), "LPAREN") &&
        is_parameter_list_node(direct_declarator->children[4].get()) &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[5].get(), "RPAREN")) {
        std::unique_ptr<TypeNode> function_type =
            std::make_unique<FunctionTypeNode>(
                std::move(base_type),
                build_function_parameter_types(
                    direct_declarator->children[4].get()),
                has_variadic_marker(direct_declarator->children[4].get()),
                get_node_source_span(direct_declarator));
        return build_pointer_type(std::move(function_type),
                                  direct_declarator->children[1].get());
    }

    if (direct_declarator->children.size() == 6 &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[0].get(), "LPAREN") &&
        ParseTreeMatcher::label_equals(direct_declarator->children[1].get(),
                                       "declarator") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[2].get(), "RPAREN") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[3].get(), "LPAREN") &&
        is_parameter_list_node(direct_declarator->children[4].get()) &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[5].get(), "RPAREN")) {
        std::unique_ptr<TypeNode> function_type =
            std::make_unique<FunctionTypeNode>(
                std::move(base_type),
                build_function_parameter_types(
                    direct_declarator->children[4].get()),
                has_variadic_marker(direct_declarator->children[4].get()),
                get_node_source_span(direct_declarator));
        return build_declarator_type(std::move(function_type),
                                     direct_declarator->children[1].get());
    }

    if (direct_declarator->children.size() == 7 &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[0].get(), "LPAREN") &&
        ParseTreeMatcher::label_equals(direct_declarator->children[1].get(),
                                       "pointer") &&
        ParseTreeMatcher::label_equals(direct_declarator->children[2].get(),
                                       "direct_declarator") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[3].get(), "RPAREN") &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[4].get(), "LPAREN") &&
        is_parameter_list_node(direct_declarator->children[5].get()) &&
        ParseTreeMatcher::label_starts_with(
            direct_declarator->children[6].get(), "RPAREN")) {
        std::unique_ptr<TypeNode> function_type =
            std::make_unique<FunctionTypeNode>(
                std::move(base_type),
                build_function_parameter_types(
                    direct_declarator->children[5].get()),
                has_variadic_marker(direct_declarator->children[5].get()),
                get_node_source_span(direct_declarator));
        std::unique_ptr<TypeNode> pointer_to_function = build_pointer_type(
            std::move(function_type), direct_declarator->children[1].get());
        return build_direct_declarator_type(
            std::move(pointer_to_function),
            direct_declarator->children[2].get());
    }

    return base_type;
}

std::vector<std::unique_ptr<TypeNode>>
AstBuilder::build_function_parameter_types(
    const ParseTreeNode *parameter_list_opt) const {
    std::vector<std::unique_ptr<TypeNode>> parameter_types;
    std::vector<const ParseTreeNode *> stack;
    if (parameter_list_opt != nullptr) {
        stack.push_back(parameter_list_opt);
    }

    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(current, "parameter_decl")) {
            const ParseTreeNode *type_specifier =
                ParseTreeMatcher::find_first_child_with_label(current,
                                                              "type_specifier");
            if (type_specifier == nullptr) {
                type_specifier = ParseTreeMatcher::find_first_child_with_label(
                    current, "nonvoid_type_specifier");
            }
            if (type_specifier == nullptr) {
                for (const auto &child : current->children) {
                    if (ParseTreeMatcher::label_starts_with(child.get(),
                                                            "TYPE_NAME")) {
                        type_specifier = child.get();
                        break;
                    }
                }
            }
            const ParseTreeNode *declarator =
                ParseTreeMatcher::find_first_child_with_label(current,
                                                              "declarator");
            const ParseTreeNode *pointer =
                ParseTreeMatcher::find_first_child_with_label(current,
                                                              "pointer");
            TypeQualifierFlags pointee_qualifiers;
            for (const auto &child : current->children) {
                if (ParseTreeMatcher::label_equals(child.get(),
                                                   "type_qualifier_seq_opt") ||
                    ParseTreeMatcher::label_equals(child.get(),
                                                   "type_qualifier_seq")) {
                    collect_type_qualifiers(child.get(),
                                            pointee_qualifiers.is_const,
                                            pointee_qualifiers.is_volatile);
                }
            }
            parameter_types.push_back(build_declared_type(
                type_specifier, declarator == nullptr ? pointer : declarator,
                pointee_qualifiers.is_const, pointee_qualifiers.is_volatile,
                true));
            continue;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }

    return parameter_types;
}

bool AstBuilder::has_variadic_marker(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return false;
    }

    std::vector<const ParseTreeNode *> stack;
    stack.push_back(node);
    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(current, "variadic_marker") ||
            ParseTreeMatcher::label_starts_with(current, "ELLIPSIS")) {
            return true;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }
    return false;
}

std::string AstBuilder::build_asm_label_text(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return {};
    }

    const ParseTreeNode *asm_label =
        ParseTreeMatcher::find_first_child_with_label(node, "asm_label");
    if (asm_label == nullptr) {
        return {};
    }

    const ParseTreeNode *string_literal_seq =
        ParseTreeMatcher::find_first_child_with_label(asm_label,
                                                      "string_literal_seq");
    std::string text = build_string_literal_sequence_text(string_literal_seq);
    constexpr const char *kUserLabelPrefix = "__USER_LABEL_PREFIX__";
    if (text.rfind(kUserLabelPrefix, 0) == 0) {
        text.erase(0, std::string(kUserLabelPrefix).size());
    }
    return text;
}

std::string AstBuilder::build_string_literal_sequence_text(
    const ParseTreeNode *node) const {
    if (node == nullptr) {
        return {};
    }

    std::string text;
    std::vector<const ParseTreeNode *> stack;
    stack.push_back(node);
    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_starts_with(current, "STRING_LITERAL")) {
            text += decode_string_literal_token(
                ParseTreeMatcher::extract_terminal_suffix(current,
                                                          "STRING_LITERAL"));
            continue;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }
    return text;
}

std::vector<std::unique_ptr<Decl>>
AstBuilder::build_struct_fields(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Decl>> fields;
    std::vector<const ParseTreeNode *> field_nodes;
    std::vector<const ParseTreeNode *> stack;
    if (node != nullptr) {
        stack.push_back(node);
    }
    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(current, "struct_field_decl")) {
            field_nodes.push_back(current);
            continue;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }

    for (const ParseTreeNode *field_node : field_nodes) {
        const ParseTreeNode *type_specifier =
            ParseTreeMatcher::find_first_child_with_label(field_node,
                                                          "type_specifier");
        const ParseTreeNode *declarator_list =
            ParseTreeMatcher::find_first_child_with_label(
                field_node, "struct_field_declarator_list");
        TypeQualifierFlags leading_qualifiers;
        collect_declaration_type_qualifiers(field_node,
                                            leading_qualifiers.is_const,
                                            leading_qualifiers.is_volatile);
        if (has_enumerator_list(type_specifier)) {
            fields.push_back(build_enum_decl(type_specifier));
        }
        std::vector<const ParseTreeNode *> declarators;
        std::vector<const ParseTreeNode *> declarator_stack;
        if (declarator_list != nullptr) {
            declarator_stack.push_back(declarator_list);
        } else {
            fields.push_back(std::make_unique<FieldDecl>(
                "",
                build_declared_type(type_specifier, nullptr,
                                    leading_qualifiers.is_const,
                                    leading_qualifiers.is_volatile),
                std::vector<std::unique_ptr<Expr>>(), nullptr,
                get_node_source_span(field_node)));
            continue;
        }
        while (!declarator_stack.empty()) {
            const ParseTreeNode *current = declarator_stack.back();
            declarator_stack.pop_back();
            if (current == nullptr) {
                continue;
            }
            if (ParseTreeMatcher::label_equals(current,
                                               "struct_field_declarator")) {
                declarators.push_back(current);
                continue;
            }
            for (auto it = current->children.rbegin();
                 it != current->children.rend(); ++it) {
                declarator_stack.push_back(it->get());
            }
        }

        for (const ParseTreeNode *field_declarator : declarators) {
            const ParseTreeNode *declarator =
                ParseTreeMatcher::find_first_child_with_label(field_declarator,
                                                              "declarator");
            const ParseTreeNode *bit_width_opt =
                ParseTreeMatcher::find_first_child_with_label(
                    field_declarator, "field_bit_width_opt");
            const ParseTreeNode *bit_width_expr = nullptr;
            if (bit_width_opt != nullptr) {
                bit_width_expr = ParseTreeMatcher::find_first_child_with_label(
                    bit_width_opt, "const_expr");
            }
            if (bit_width_expr == nullptr &&
                ParseTreeMatcher::find_first_child_with_label(
                    field_declarator, "COLON") != nullptr) {
                bit_width_expr = ParseTreeMatcher::find_first_child_with_label(
                    field_declarator, "const_expr");
            }
            fields.push_back(std::make_unique<FieldDecl>(
                declarator == nullptr ? ""
                                      : extract_declarator_name(declarator),
                build_declared_type(type_specifier, declarator,
                                    leading_qualifiers.is_const,
                                    leading_qualifiers.is_volatile),
                declarator == nullptr
                    ? std::vector<std::unique_ptr<Expr>>()
                    : collect_declarator_dimensions(declarator),
                bit_width_expr == nullptr ? nullptr
                                          : build_expr(bit_width_expr),
                get_node_source_span(field_declarator)));
        }
    }

    return fields;
}

std::vector<std::unique_ptr<Decl>>
AstBuilder::build_union_fields(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Decl>> fields;
    std::vector<const ParseTreeNode *> field_nodes;
    std::vector<const ParseTreeNode *> stack;
    if (node != nullptr) {
        stack.push_back(node);
    }
    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(current, "union_field_decl")) {
            field_nodes.push_back(current);
            continue;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }

    for (const ParseTreeNode *field_node : field_nodes) {
        const ParseTreeNode *type_specifier =
            ParseTreeMatcher::find_first_child_with_label(field_node,
                                                          "type_specifier");
        const ParseTreeNode *declarator_list =
            ParseTreeMatcher::find_first_child_with_label(
                field_node, "union_field_declarator_list");
        TypeQualifierFlags leading_qualifiers;
        collect_declaration_type_qualifiers(field_node,
                                            leading_qualifiers.is_const,
                                            leading_qualifiers.is_volatile);
        if (has_enumerator_list(type_specifier)) {
            fields.push_back(build_enum_decl(type_specifier));
        }
        std::vector<const ParseTreeNode *> declarators;
        std::vector<const ParseTreeNode *> declarator_stack;
        if (declarator_list != nullptr) {
            declarator_stack.push_back(declarator_list);
        } else {
            fields.push_back(std::make_unique<FieldDecl>(
                "",
                build_declared_type(type_specifier, nullptr,
                                    leading_qualifiers.is_const,
                                    leading_qualifiers.is_volatile),
                std::vector<std::unique_ptr<Expr>>(), nullptr,
                get_node_source_span(field_node)));
            continue;
        }
        while (!declarator_stack.empty()) {
            const ParseTreeNode *current = declarator_stack.back();
            declarator_stack.pop_back();
            if (current == nullptr) {
                continue;
            }
            if (ParseTreeMatcher::label_equals(current,
                                               "union_field_declarator")) {
                declarators.push_back(current);
                continue;
            }
            for (auto it = current->children.rbegin();
                 it != current->children.rend(); ++it) {
                declarator_stack.push_back(it->get());
            }
        }

        for (const ParseTreeNode *field_declarator : declarators) {
            const ParseTreeNode *declarator =
                ParseTreeMatcher::find_first_child_with_label(field_declarator,
                                                              "declarator");
            const ParseTreeNode *bit_width_opt =
                ParseTreeMatcher::find_first_child_with_label(
                    field_declarator, "field_bit_width_opt");
            const ParseTreeNode *bit_width_expr = nullptr;
            if (bit_width_opt != nullptr) {
                bit_width_expr = ParseTreeMatcher::find_first_child_with_label(
                    bit_width_opt, "const_expr");
            }
            if (bit_width_expr == nullptr &&
                ParseTreeMatcher::find_first_child_with_label(
                    field_declarator, "COLON") != nullptr) {
                bit_width_expr = ParseTreeMatcher::find_first_child_with_label(
                    field_declarator, "const_expr");
            }
            fields.push_back(std::make_unique<FieldDecl>(
                declarator == nullptr ? ""
                                      : extract_declarator_name(declarator),
                build_declared_type(type_specifier, declarator,
                                    leading_qualifiers.is_const,
                                    leading_qualifiers.is_volatile),
                declarator == nullptr
                    ? std::vector<std::unique_ptr<Expr>>()
                    : collect_declarator_dimensions(declarator),
                bit_width_expr == nullptr ? nullptr
                                          : build_expr(bit_width_expr),
                get_node_source_span(field_declarator)));
        }
    }

    return fields;
}

std::vector<std::unique_ptr<Decl>>
AstBuilder::build_enumerators(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Decl>> enumerators;
    std::vector<const ParseTreeNode *> enumerator_nodes;
    std::vector<const ParseTreeNode *> stack;
    if (node != nullptr) {
        stack.push_back(node);
    }
    while (!stack.empty()) {
        const ParseTreeNode *current = stack.back();
        stack.pop_back();
        if (current == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(current, "enumerator")) {
            enumerator_nodes.push_back(current);
            continue;
        }
        for (auto it = current->children.rbegin();
             it != current->children.rend(); ++it) {
            stack.push_back(it->get());
        }
    }

    for (const ParseTreeNode *enumerator_node : enumerator_nodes) {
        std::string name = "<anonymous>";
        std::unique_ptr<Expr> value = nullptr;
        for (const auto &child : enumerator_node->children) {
            if (ParseTreeMatcher::label_starts_with(child.get(),
                                                    "IDENTIFIER")) {
                const std::string suffix =
                    ParseTreeMatcher::extract_terminal_suffix(child.get(),
                                                              "IDENTIFIER");
                if (!suffix.empty()) {
                    name = suffix;
                }
            } else if (ParseTreeMatcher::label_equals(child.get(),
                                                      "const_expr")) {
                value = build_expr(child.get());
            }
        }
        enumerators.push_back(std::make_unique<EnumeratorDecl>(
            name, std::move(value), get_node_source_span(enumerator_node)));
    }

    return enumerators;
}

std::unique_ptr<BlockStmt>
AstBuilder::build_block_stmt(const ParseTreeNode *node) const {
    auto block = std::make_unique<BlockStmt>(get_node_source_span(node));
    std::vector<const ParseTreeNode *> block_items;
    collect_block_items(node, block_items);
    for (const ParseTreeNode *block_item : block_items) {
        block->add_statement(build_block_item(block_item));
    }
    return block;
}

void AstBuilder::collect_block_items(
    const ParseTreeNode *node,
    std::vector<const ParseTreeNode *> &items) const {
    if (node == nullptr) {
        return;
    }
    if (ParseTreeMatcher::label_equals(node, "block_item")) {
        items.push_back(node);
        return;
    }
    for (const auto &child : node->children) {
        collect_block_items(child.get(), items);
    }
}

std::unique_ptr<Stmt>
AstBuilder::build_block_item(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return std::make_unique<UnknownStmt>("null block_item");
    }
    if (const ParseTreeNode *decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "decl")) {
        auto decl_stmt = std::make_unique<DeclStmt>(get_node_source_span(node));
        for (auto &decl : build_decl_group(decl_node)) {
            decl_stmt->add_declaration(std::move(decl));
        }
        return decl_stmt;
    }
    return build_stmt(node);
}

std::unique_ptr<Stmt> AstBuilder::build_stmt(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return std::make_unique<UnknownStmt>("null stmt");
    }

    const ParseTreeNode *stmt_node = node;
    if (!ParseTreeMatcher::label_equals(stmt_node, "stmt")) {
        stmt_node = ParseTreeMatcher::find_first_child_with_label(node, "stmt");
    }
    if (stmt_node == nullptr) {
        return std::make_unique<UnknownStmt>(node->label);
    }

    const ParseTreeNode *direct_block = nullptr;
    const ParseTreeNode *direct_expr_opt = nullptr;
    for (const auto &child : stmt_node->children) {
        if (direct_block == nullptr &&
            ParseTreeMatcher::label_equals(child.get(), "block")) {
            direct_block = child.get();
        }
        if (direct_expr_opt == nullptr &&
            ParseTreeMatcher::label_equals(child.get(), "expr_opt")) {
            direct_expr_opt = child.get();
        }
    }

    if (direct_block != nullptr) {
        return build_block_stmt(direct_block);
    }

    if (direct_expr_opt != nullptr && stmt_node->children.size() == 2 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[1].get(),
                                            "SEMICOLON")) {
        if (direct_expr_opt->children.empty()) {
            return std::make_unique<ExprStmt>(nullptr,
                                              get_node_source_span(stmt_node));
        }
        return std::make_unique<ExprStmt>(
            build_expr(direct_expr_opt->children[0].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() >= 5 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "IF")) {
        std::unique_ptr<Stmt> else_branch = nullptr;
        if (stmt_node->children.size() == 7) {
            else_branch = build_stmt(stmt_node->children[6].get());
        }
        return std::make_unique<IfStmt>(
            build_expr(stmt_node->children[2].get()),
            build_stmt(stmt_node->children[4].get()), std::move(else_branch),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 5 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "WHILE")) {
        return std::make_unique<WhileStmt>(
            build_expr(stmt_node->children[2].get()),
            build_stmt(stmt_node->children[4].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 7 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "DO")) {
        return std::make_unique<DoWhileStmt>(
            build_stmt(stmt_node->children[1].get()),
            build_expr(stmt_node->children[4].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 9 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "FOR")) {
        std::unique_ptr<Expr> init = nullptr;
        std::unique_ptr<Expr> condition = nullptr;
        std::unique_ptr<Expr> step = nullptr;
        if (!stmt_node->children[2]->children.empty()) {
            init = build_expr(stmt_node->children[2]->children[0].get());
        }
        if (!stmt_node->children[4]->children.empty()) {
            condition = build_expr(stmt_node->children[4]->children[0].get());
        }
        if (!stmt_node->children[6]->children.empty()) {
            step = build_expr(stmt_node->children[6]->children[0].get());
        }
        return std::make_unique<ForStmt>(
            std::move(init), std::move(condition), std::move(step),
            build_stmt(stmt_node->children[8].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 8 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "FOR") &&
        ParseTreeMatcher::label_equals(stmt_node->children[2].get(),
                                       "var_decl")) {
        auto init_decl = std::make_unique<DeclStmt>(
            get_node_source_span(stmt_node->children[2].get()));
        for (auto &decl : build_decl_group(stmt_node->children[2].get())) {
            init_decl->add_declaration(std::move(decl));
        }
        std::unique_ptr<Expr> condition = nullptr;
        std::unique_ptr<Expr> step = nullptr;
        if (!stmt_node->children[3]->children.empty()) {
            condition = build_expr(stmt_node->children[3]->children[0].get());
        }
        if (!stmt_node->children[5]->children.empty()) {
            step = build_expr(stmt_node->children[5]->children[0].get());
        }
        return std::make_unique<ForStmt>(
            std::move(init_decl), std::move(condition), std::move(step),
            build_stmt(stmt_node->children[7].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 5 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "SWITCH")) {
        return std::make_unique<SwitchStmt>(
            build_expr(stmt_node->children[2].get()),
            build_stmt(stmt_node->children[4].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 4 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "CASE")) {
        return std::make_unique<CaseStmt>(
            build_expr(stmt_node->children[1].get()),
            build_stmt(stmt_node->children[3].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 3 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "DEFAULT")) {
        return std::make_unique<DefaultStmt>(
            build_stmt(stmt_node->children[2].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 3 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "IDENTIFIER") &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[1].get(),
                                            "COLON")) {
        return std::make_unique<LabelStmt>(
            ParseTreeMatcher::extract_terminal_suffix(
                stmt_node->children[0].get(), "IDENTIFIER"),
            build_stmt(stmt_node->children[2].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 2 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "BREAK")) {
        return std::make_unique<BreakStmt>(get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 2 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "CONTINUE")) {
        return std::make_unique<ContinueStmt>(get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 3 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "GOTO")) {
        return std::make_unique<GotoStmt>(
            ParseTreeMatcher::extract_terminal_suffix(
                stmt_node->children[1].get(), "IDENTIFIER"),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 4 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(),
                                            "GOTO") &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[1].get(),
                                            "MUL")) {
        return std::make_unique<GotoStmt>(
            build_expr(stmt_node->children[2].get()),
            get_node_source_span(stmt_node));
    }

    for (const auto &child : stmt_node->children) {
        if (ParseTreeMatcher::label_starts_with(child.get(), "RETURN")) {
            const ParseTreeNode *expr_opt =
                ParseTreeMatcher::find_first_child_with_label(stmt_node,
                                                              "expr_opt");
            if (expr_opt == nullptr || expr_opt->children.empty()) {
                return std::make_unique<ReturnStmt>(
                    nullptr, get_node_source_span(stmt_node));
            }
            return std::make_unique<ReturnStmt>(
                build_expr(expr_opt->children[0].get()),
                get_node_source_span(stmt_node));
        }
    }

    return std::make_unique<UnknownStmt>(stmt_node->label);
}

std::unique_ptr<Expr> AstBuilder::build_expr(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return std::make_unique<UnknownExpr>("null expr");
    }

    if (ParseTreeMatcher::label_starts_with(node, "INT_LITERAL")) {
        const std::string value_text =
            ParseTreeMatcher::extract_terminal_suffix(node, "INT_LITERAL");
        return std::make_unique<IntegerLiteralExpr>(
            value_text.empty() ? node->label : value_text,
            get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_starts_with(node, "FLOAT_LITERAL")) {
        const std::string value_text =
            ParseTreeMatcher::extract_terminal_suffix(node, "FLOAT_LITERAL");
        return std::make_unique<FloatLiteralExpr>(
            value_text.empty() ? node->label : value_text,
            get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_starts_with(node, "CHAR_LITERAL")) {
        const std::string value_text =
            ParseTreeMatcher::extract_terminal_suffix(node, "CHAR_LITERAL");
        return std::make_unique<CharLiteralExpr>(
            value_text.empty() ? node->label : value_text,
            get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_starts_with(node, "STRING_LITERAL")) {
        const std::string value_text =
            ParseTreeMatcher::extract_terminal_suffix(node, "STRING_LITERAL");
        return std::make_unique<StringLiteralExpr>(
            value_text.empty() ? node->label : value_text,
            get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "string_literal_seq")) {
        return std::make_unique<StringLiteralExpr>(
            build_string_literal_sequence_text(node),
            get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node,
                                       "gnu_builtin_types_compatible_expr") &&
        node->children.size() == 6) {
        const std::string callee_name =
            ParseTreeMatcher::extract_terminal_suffix(node->children[0].get(),
                                                      "IDENTIFIER");
        if (callee_name != "__builtin_types_compatible_p") {
            return std::make_unique<UnknownExpr>(
                "unsupported GNU type-compatible builtin",
                get_node_source_span(node));
        }

        auto build_typeof_argument =
            [this](const ParseTreeNode *typeof_type) -> std::unique_ptr<Expr> {
            std::vector<std::unique_ptr<Expr>> typeof_arguments;
            typeof_arguments.push_back(
                typeof_type != nullptr && typeof_type->children.size() >= 3
                    ? build_expr(typeof_type->children[2].get())
                    : std::unique_ptr<Expr>(std::make_unique<UnknownExpr>(
                          "missing typeof operand",
                          get_node_source_span(typeof_type))));
            return std::make_unique<CallExpr>(
                std::make_unique<IdentifierExpr>(
                    "__typeof__",
                    typeof_type != nullptr && !typeof_type->children.empty()
                        ? get_node_source_span(typeof_type->children[0].get())
                        : get_node_source_span(typeof_type)),
                std::move(typeof_arguments), get_node_source_span(typeof_type));
        };

        std::vector<std::unique_ptr<Expr>> arguments;
        arguments.push_back(build_typeof_argument(node->children[2].get()));
        arguments.push_back(build_typeof_argument(node->children[4].get()));
        return std::make_unique<CallExpr>(
            std::make_unique<IdentifierExpr>(
                callee_name, get_node_source_span(node->children[0].get())),
            std::move(arguments), get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_starts_with(node, "IDENTIFIER")) {
        const std::string name =
            ParseTreeMatcher::extract_terminal_suffix(node, "IDENTIFIER");
        return std::make_unique<IdentifierExpr>(
            name.empty() ? node->label : name, get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "unary_expr") &&
        node->children.size() == 2) {
        if (ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "INC") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "DEC")) {
            return std::make_unique<PrefixExpr>(
                get_operator_text(node->children[0].get()),
                build_expr(node->children[1].get()),
                get_node_source_span(node));
        }

        if (ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "PLUS") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "MINUS") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "NOT") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "BITNOT") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "BITAND") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "AND") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "MUL") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                                "SIZEOF")) {
            return std::make_unique<UnaryExpr>(
                get_operator_text(node->children[0].get()),
                build_expr(node->children[1].get()),
                get_node_source_span(node));
        }
    }

    if (ParseTreeMatcher::label_equals(node, "sizeof_type_expr") &&
        node->children.size() == 4) {
        return std::make_unique<SizeofTypeExpr>(
            build_cast_target_type(node->children[2].get()),
            get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "cast_expr") &&
        node->children.size() == 4 &&
        ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                            "LPAREN") &&
        ParseTreeMatcher::label_starts_with(node->children[2].get(),
                                            "RPAREN")) {
        return std::make_unique<CastExpr>(
            build_cast_target_type(node->children[1].get()),
            build_expr(node->children[3].get()), get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "assignment_expr") &&
        node->children.size() == 3 &&
        (ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "ADD_ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "SUB_ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "MUL_ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "DIV_ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "MOD_ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "SHL_ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "SHR_ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "AND_ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "XOR_ASSIGN") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "OR_ASSIGN"))) {
        return std::make_unique<AssignExpr>(
            get_operator_text(node->children[1].get()),
            build_expr(node->children[0].get()),
            build_expr(node->children[2].get()), get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "conditional_expr") &&
        node->children.size() == 5 &&
        ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                            "QUESTION") &&
        ParseTreeMatcher::label_starts_with(node->children[3].get(), "COLON")) {
        return std::make_unique<ConditionalExpr>(
            build_expr(node->children[0].get()),
            build_expr(node->children[2].get()),
            build_expr(node->children[4].get()), get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "init_val")) {
        if (node->children.size() == 1) {
            return build_expr(node->children[0].get());
        }
        if (!node->children.empty() && ParseTreeMatcher::label_starts_with(
                                           node->children[0].get(), "LBRACE")) {
            return build_init_list_expr(node);
        }
    }

    if (ParseTreeMatcher::label_equals(node, "designated_init_val")) {
        if (const ParseTreeNode *init_node =
                ParseTreeMatcher::find_first_child_with_label(node,
                                                              "init_val")) {
            return build_expr(init_node);
        }
    }

    if ((ParseTreeMatcher::label_equals(node, "postfix_expr") ||
         ParseTreeMatcher::label_equals(node, "expr") ||
         ParseTreeMatcher::label_equals(node, "assignment_expr") ||
         ParseTreeMatcher::label_equals(node, "logical_or_expr") ||
         ParseTreeMatcher::label_equals(node, "logical_and_expr") ||
         ParseTreeMatcher::label_equals(node, "bit_or_expr") ||
         ParseTreeMatcher::label_equals(node, "bit_xor_expr") ||
         ParseTreeMatcher::label_equals(node, "bit_and_expr") ||
         ParseTreeMatcher::label_equals(node, "eq_expr") ||
         ParseTreeMatcher::label_equals(node, "rel_expr") ||
         ParseTreeMatcher::label_equals(node, "shift_expr") ||
         ParseTreeMatcher::label_equals(node, "add_expr") ||
         ParseTreeMatcher::label_equals(node, "mul_expr")) &&
        node->children.size() == 3 &&
        is_binary_operator_label(node->children[1].get())) {
        return std::make_unique<BinaryExpr>(
            get_operator_text(node->children[1].get()),
            build_expr(node->children[0].get()),
            build_expr(node->children[2].get()), get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "postfix_expr") &&
        node->children.size() == 1 &&
        ParseTreeMatcher::label_equals(node->children[0].get(),
                                       "builtin_va_arg_expr")) {
        return build_expr(node->children[0].get());
    }

    if (ParseTreeMatcher::label_equals(node, "builtin_va_arg_expr") &&
        node->children.size() == 6) {
        const std::string callee_name =
            ParseTreeMatcher::extract_terminal_suffix(node->children[0].get(),
                                                      "IDENTIFIER");
        if (callee_name == "__builtin_va_arg") {
            return std::make_unique<BuiltinVaArgExpr>(
                build_expr(node->children[2].get()),
                build_cast_target_type(node->children[4].get()),
                get_node_source_span(node));
        }
    }

    if (ParseTreeMatcher::label_equals(node, "extension_expr") &&
        node->children.size() == 2) {
        return build_expr(node->children[1].get());
    }

    if (ParseTreeMatcher::label_equals(node, "statement_expr") &&
        node->children.size() == 3) {
        return std::make_unique<StatementExpr>(
            build_block_stmt(node->children[1].get()),
            get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "primary_expr") &&
        node->children.size() == 1 &&
        ParseTreeMatcher::label_equals(node->children[0].get(),
                                       "statement_expr")) {
        return build_expr(node->children[0].get());
    }

    if (ParseTreeMatcher::label_equals(node, "postfix_expr") &&
        node->children.size() == 3 &&
        ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                            "LPAREN") &&
        ParseTreeMatcher::label_starts_with(node->children[2].get(),
                                            "RPAREN")) {
        return std::make_unique<CallExpr>(build_expr(node->children[0].get()),
                                          std::vector<std::unique_ptr<Expr>>{},
                                          get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "postfix_expr") &&
        node->children.size() == 4) {
        if (ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                                "LPAREN")) {
            std::vector<std::unique_ptr<Expr>> arguments =
                build_argument_exprs(node->children[2].get());
            return std::make_unique<CallExpr>(
                build_expr(node->children[0].get()), std::move(arguments),
                get_node_source_span(node));
        }
        if (ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                                "LBRACKET")) {
            return std::make_unique<IndexExpr>(
                build_expr(node->children[0].get()),
                build_expr(node->children[2].get()),
                get_node_source_span(node));
        }
    }

    if (ParseTreeMatcher::label_equals(node, "postfix_expr") &&
        node->children.size() == 3 &&
        (ParseTreeMatcher::label_starts_with(node->children[1].get(),
                                             "ARROW") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(), "DOT"))) {
        const std::string member_name =
            ParseTreeMatcher::extract_terminal_suffix(node->children[2].get(),
                                                      "IDENTIFIER");
        const std::string type_name_member =
            ParseTreeMatcher::extract_terminal_suffix(node->children[2].get(),
                                                      "TYPE_NAME");
        return std::make_unique<MemberExpr>(
            get_operator_text(node->children[1].get()),
            build_expr(node->children[0].get()),
            !member_name.empty() ? member_name : type_name_member,
            get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "postfix_expr") &&
        node->children.size() == 2 &&
        (ParseTreeMatcher::label_starts_with(node->children[1].get(), "INC") ||
         ParseTreeMatcher::label_starts_with(node->children[1].get(), "DEC"))) {
        return std::make_unique<PostfixExpr>(
            get_operator_text(node->children[1].get()),
            build_expr(node->children[0].get()), get_node_source_span(node));
    }

    if ((ParseTreeMatcher::label_equals(node, "expr_opt") ||
         ParseTreeMatcher::label_equals(node, "expr") ||
         ParseTreeMatcher::label_equals(node, "const_expr") ||
         ParseTreeMatcher::label_equals(node, "cond") ||
         ParseTreeMatcher::label_equals(node, "assignment_expr") ||
         ParseTreeMatcher::label_equals(node, "conditional_expr") ||
         ParseTreeMatcher::label_equals(node, "logical_or_expr") ||
         ParseTreeMatcher::label_equals(node, "logical_and_expr") ||
         ParseTreeMatcher::label_equals(node, "bit_or_expr") ||
         ParseTreeMatcher::label_equals(node, "bit_xor_expr") ||
         ParseTreeMatcher::label_equals(node, "bit_and_expr") ||
         ParseTreeMatcher::label_equals(node, "eq_expr") ||
         ParseTreeMatcher::label_equals(node, "rel_expr") ||
         ParseTreeMatcher::label_equals(node, "shift_expr") ||
         ParseTreeMatcher::label_equals(node, "add_expr") ||
         ParseTreeMatcher::label_equals(node, "mul_expr") ||
         ParseTreeMatcher::label_equals(node, "cast_expr") ||
         ParseTreeMatcher::label_equals(node, "extension_expr") ||
         ParseTreeMatcher::label_equals(node, "primary_expr") ||
         ParseTreeMatcher::label_equals(node, "unary_expr") ||
         ParseTreeMatcher::label_equals(node, "postfix_expr")) &&
        node->children.size() == 1) {
        return build_expr(node->children[0].get());
    }

    if (ParseTreeMatcher::label_equals(node, "primary_expr") &&
        node->children.size() == 3 &&
        ParseTreeMatcher::label_starts_with(node->children[0].get(),
                                            "LPAREN")) {
        return build_expr(node->children[1].get());
    }

    return std::make_unique<UnknownExpr>(node->label,
                                         get_node_source_span(node));
}

std::unique_ptr<TypeNode>
AstBuilder::build_cast_target_type(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return std::make_unique<UnknownTypeNode>("null cast_target_type");
    }

    const ParseTreeNode *type_specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "type_specifier");
    const ParseTreeNode *sizeof_suffix =
        ParseTreeMatcher::find_first_child_with_label(node,
                                                      "sizeof_type_suffix_opt");
    TypeQualifierFlags pointee_qualifiers;
    collect_declaration_type_qualifiers(node, pointee_qualifiers.is_const,
                                        pointee_qualifiers.is_volatile);

    if (type_specifier == nullptr) {
        return std::make_unique<UnknownTypeNode>("missing cast target type",
                                                 get_node_source_span(node));
    }

    if (const ParseTreeNode *declarator =
            ParseTreeMatcher::find_first_child_with_label(node, "declarator");
        declarator != nullptr) {
        std::unique_ptr<TypeNode> base_type = build_declared_type(
            type_specifier, nullptr, pointee_qualifiers.is_const,
            pointee_qualifiers.is_volatile);
        return build_declarator_type(std::move(base_type), declarator);
    }

    const ParseTreeNode *pointer =
        ParseTreeMatcher::find_first_child_with_label(node, "pointer");
    if (pointer == nullptr && sizeof_suffix != nullptr) {
        pointer = ParseTreeMatcher::find_first_child_with_label(sizeof_suffix,
                                                                "pointer");
    }
    std::unique_ptr<TypeNode> declared_type = build_declared_type(
        type_specifier, pointer, pointee_qualifiers.is_const,
        pointee_qualifiers.is_volatile);
    if (const ParseTreeNode *array_suffix_list =
            ParseTreeMatcher::find_first_child_with_label(
                node, "abstract_array_suffix_list");
        array_suffix_list != nullptr) {
        declared_type = std::make_unique<ArrayTypeNode>(
            std::move(declared_type),
            collect_declarator_dimensions(array_suffix_list),
            get_node_source_span(node));
    } else if (sizeof_suffix != nullptr) {
        if (const ParseTreeNode *nested_array_suffix_list =
                ParseTreeMatcher::find_first_child_with_label(
                    sizeof_suffix, "abstract_array_suffix_list");
            nested_array_suffix_list != nullptr) {
            declared_type = std::make_unique<ArrayTypeNode>(
                std::move(declared_type),
                collect_declarator_dimensions(nested_array_suffix_list),
                get_node_source_span(node));
        }
    }
    return declared_type;
}

std::vector<std::unique_ptr<Expr>>
AstBuilder::collect_declarator_dimensions(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Expr>> dimensions;
    if (node == nullptr) {
        return dimensions;
    }

    for (const auto &child : node->children) {
        if (ParseTreeMatcher::label_equals(child.get(), "expr_opt")) {
            if (child->children.empty()) {
                dimensions.push_back(nullptr);
            } else {
                dimensions.push_back(build_expr(child->children[0].get()));
            }
            continue;
        }
        auto nested_dimensions = collect_declarator_dimensions(child.get());
        for (auto &dimension : nested_dimensions) {
            dimensions.push_back(std::move(dimension));
        }
    }
    return dimensions;
}

std::string
AstBuilder::extract_declarator_name(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return "<unnamed>";
    }
    if (ParseTreeMatcher::label_starts_with(node, "IDENTIFIER")) {
        const std::string name =
            ParseTreeMatcher::extract_terminal_suffix(node, "IDENTIFIER");
        return name.empty() ? "<unnamed>" : name;
    }
    if (ParseTreeMatcher::label_starts_with(node, "TYPE_NAME")) {
        const std::string name =
            ParseTreeMatcher::extract_terminal_suffix(node, "TYPE_NAME");
        return name.empty() ? "<unnamed>" : name;
    }
    for (const auto &child : node->children) {
        const std::string nested = extract_declarator_name(child.get());
        if (nested != "<unnamed>") {
            return nested;
        }
    }
    return "<unnamed>";
}

std::vector<std::unique_ptr<Expr>>
AstBuilder::build_argument_exprs(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Expr>> arguments;
    std::vector<const ParseTreeNode *> expr_nodes;
    collect_argument_expr_nodes(node, expr_nodes);
    arguments.reserve(expr_nodes.size());
    for (const ParseTreeNode *expr_node : expr_nodes) {
        arguments.push_back(build_expr(expr_node));
    }
    return arguments;
}

void AstBuilder::collect_argument_expr_nodes(
    const ParseTreeNode *node,
    std::vector<const ParseTreeNode *> &expr_nodes) const {
    if (node == nullptr) {
        return;
    }
    if (ParseTreeMatcher::label_equals(node, "assignment_expr")) {
        expr_nodes.push_back(node);
        return;
    }
    for (const auto &child : node->children) {
        collect_argument_expr_nodes(child.get(), expr_nodes);
    }
}

std::unique_ptr<InitListExpr>
AstBuilder::build_init_list_expr(const ParseTreeNode *node) const {
    auto init_list = std::make_unique<InitListExpr>(get_node_source_span(node));
    if (node == nullptr) {
        return init_list;
    }

    std::vector<const ParseTreeNode *> init_value_nodes;
    for (const auto &child : node->children) {
        if (child != nullptr &&
            ParseTreeMatcher::label_equals(child.get(), "init_val_list")) {
            collect_direct_init_value_nodes(child.get(), init_value_nodes);
        }
    }

    for (const ParseTreeNode *init_value_node : init_value_nodes) {
        if (ParseTreeMatcher::label_equals(init_value_node,
                                           "designated_init_val")) {
            init_list->add_element(
                build_expr(init_value_node),
                build_initializer_designator_path(init_value_node));
            continue;
        }
        init_list->add_element(build_expr(init_value_node));
    }
    return init_list;
}

InitListExpr::DesignatorPath
AstBuilder::build_initializer_designator_path(const ParseTreeNode *node) const {
    InitListExpr::DesignatorPath path;
    const ParseTreeNode *designator_seq =
        ParseTreeMatcher::find_first_child_with_label(node, "designator_seq");
    if (designator_seq == nullptr) {
        return path;
    }

    std::vector<const ParseTreeNode *> designators;
    std::function<void(const ParseTreeNode *)> collect_designators =
        [&](const ParseTreeNode *current) {
            if (current == nullptr) {
                return;
            }
            if (ParseTreeMatcher::label_equals(current, "designator")) {
                designators.push_back(current);
                return;
            }
            for (const auto &child : current->children) {
                collect_designators(child.get());
            }
        };
    collect_designators(designator_seq);

    for (const ParseTreeNode *designator : designators) {
        for (const auto &child : designator->children) {
            if (child == nullptr) {
                continue;
            }
            if (ParseTreeMatcher::label_starts_with(child.get(),
                                                    "IDENTIFIER")) {
                path.push_back({InitListExpr::Designator::Kind::Field,
                                ParseTreeMatcher::extract_terminal_suffix(
                                    child.get(), "IDENTIFIER")});
                break;
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "TYPE_NAME")) {
                path.push_back({InitListExpr::Designator::Kind::Field,
                                ParseTreeMatcher::extract_terminal_suffix(
                                    child.get(), "TYPE_NAME")});
                break;
            }
            if (ParseTreeMatcher::label_equals(child.get(), "const_expr")) {
                path.push_back(
                    {InitListExpr::Designator::Kind::Index, child->label});
                break;
            }
        }
    }
    return path;
}

void AstBuilder::collect_direct_init_value_nodes(
    const ParseTreeNode *node,
    std::vector<const ParseTreeNode *> &nodes) const {
    if (node == nullptr) {
        return;
    }

    for (const auto &child : node->children) {
        if (child == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(child.get(), "init_val")) {
            nodes.push_back(child.get());
            continue;
        }
        if (ParseTreeMatcher::label_equals(child.get(),
                                           "designated_init_val")) {
            nodes.push_back(child.get());
            continue;
        }
        if (ParseTreeMatcher::label_equals(child.get(), "init_val_list")) {
            collect_direct_init_value_nodes(child.get(), nodes);
        }
    }
}

bool AstBuilder::is_binary_operator_label(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return false;
    }
    return ParseTreeMatcher::label_starts_with(node, "PLUS") ||
           ParseTreeMatcher::label_starts_with(node, "MINUS") ||
           ParseTreeMatcher::label_starts_with(node, "MUL") ||
           ParseTreeMatcher::label_starts_with(node, "DIV") ||
           ParseTreeMatcher::label_starts_with(node, "MOD") ||
           ParseTreeMatcher::label_starts_with(node, "ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "ADD_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "SUB_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "MUL_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "DIV_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "MOD_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "SHL_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "SHR_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "AND_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "XOR_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "OR_ASSIGN") ||
           ParseTreeMatcher::label_starts_with(node, "EQ") ||
           ParseTreeMatcher::label_starts_with(node, "NE") ||
           ParseTreeMatcher::label_starts_with(node, "LT") ||
           ParseTreeMatcher::label_starts_with(node, "LE") ||
           ParseTreeMatcher::label_starts_with(node, "GT") ||
           ParseTreeMatcher::label_starts_with(node, "GE") ||
           ParseTreeMatcher::label_starts_with(node, "AND") ||
           ParseTreeMatcher::label_starts_with(node, "OR") ||
           ParseTreeMatcher::label_starts_with(node, "COMMA") ||
           ParseTreeMatcher::label_starts_with(node, "BITAND") ||
           ParseTreeMatcher::label_starts_with(node, "BITOR") ||
           ParseTreeMatcher::label_starts_with(node, "BITXOR") ||
           ParseTreeMatcher::label_starts_with(node, "SHL") ||
           ParseTreeMatcher::label_starts_with(node, "SHR");
}

std::string AstBuilder::get_operator_text(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return "?";
    }
    const std::string &label = node->label;
    const std::size_t space_index = label.find(' ');
    if (space_index == std::string::npos || space_index + 1 >= label.size()) {
        return label;
    }
    return label.substr(space_index + 1);
}

SourceSpan AstBuilder::get_node_source_span(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return {};
    }
    return node->source_span;
}

} // namespace sysycc::detail
