#include "frontend/ast/detail/ast_builder.hpp"

#include <utility>
#include <vector>

#include "frontend/ast/detail/parse_tree_matcher.hpp"

namespace sysycc::detail {

std::unique_ptr<TranslationUnit>
AstBuilder::build(const AstBuilderContext &context) const {
    const ParseTreeNode *parse_tree_root = context.get_parse_tree_root();
    auto translation_unit =
        std::make_unique<TranslationUnit>(get_node_source_span(parse_tree_root));
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
    const ParseTreeNode *node, std::vector<const ParseTreeNode *> &items) const {
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

    if (const ParseTreeNode *function_node =
            ParseTreeMatcher::find_first_child_with_label(node, "func_def")) {
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

    translation_unit.add_top_level_decl(std::make_unique<UnknownDecl>(node->label));
}

std::unique_ptr<FunctionDecl>
AstBuilder::build_function_decl(const ParseTreeNode *node) const {
    std::string function_name = "<anonymous>";
    std::unique_ptr<TypeNode> return_type =
        std::make_unique<UnknownTypeNode>("unknown");
    std::vector<std::unique_ptr<Decl>> parameters;
    std::unique_ptr<BlockStmt> body = std::make_unique<BlockStmt>();

    if (node != nullptr) {
        for (const auto &child : node->children) {
            if (ParseTreeMatcher::label_equals(child.get(), "type_specifier")) {
                return_type = build_return_type(child.get());
                continue;
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "IDENTIFIER")) {
                const std::string suffix =
                    ParseTreeMatcher::extract_terminal_suffix(child.get(),
                                                             "IDENTIFIER");
                if (!suffix.empty()) {
                    function_name = suffix;
                }
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(), "parameter_list_opt")) {
                parameters = build_parameters(child.get());
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(), "block")) {
                body = build_block_stmt(child.get());
            }
        }
    }

    return std::make_unique<FunctionDecl>(
        function_name, std::move(return_type), std::move(parameters),
        std::move(body), get_node_source_span(node));
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
            const ParseTreeNode *declarator =
                ParseTreeMatcher::find_first_child_with_label(current,
                                                              "declarator");
            parameters.push_back(std::make_unique<ParamDecl>(
                extract_declarator_name(declarator),
                build_declared_type(type_specifier, declarator),
                collect_declarator_dimensions(declarator),
                get_node_source_span(current)));
            continue;
        }
        for (auto it = current->children.rbegin(); it != current->children.rend();
             ++it) {
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
    if (const ParseTreeNode *const_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "const_decl")) {
        return build_const_decls(const_decl_node);
    }
    if (const ParseTreeNode *var_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "var_decl")) {
        return build_var_decls(var_decl_node);
    }
    if (const ParseTreeNode *typedef_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "typedef_decl")) {
        return build_typedef_decls(typedef_decl_node);
    }
    if (const ParseTreeNode *struct_decl_node =
            ParseTreeMatcher::find_first_child_with_label(node, "struct_decl")) {
        std::vector<std::unique_ptr<Decl>> decls;
        decls.push_back(build_struct_decl(struct_decl_node));
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

    if (node != nullptr) {
        for (const auto &child : node->children) {
            if (ParseTreeMatcher::label_equals(child.get(), "type_specifier")) {
                shared_type = build_return_type(child.get());
            } else if (ParseTreeMatcher::label_equals(
                           child.get(), "const_init_declarator_list")) {
                list_node = child.get();
            }
        }
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
        for (auto it = current->children.rbegin(); it != current->children.rend();
             ++it) {
            stack.push_back(it->get());
        }
    }

    for (const ParseTreeNode *declarator_node : declarators) {
        const ParseTreeNode *declarator =
            ParseTreeMatcher::find_first_child_with_label(declarator_node,
                                                          "declarator");
        const ParseTreeNode *initializer =
            ParseTreeMatcher::find_first_child_with_label(declarator_node,
                                                          "const_init_val");
        decls.push_back(std::make_unique<ConstDecl>(
            extract_declarator_name(declarator),
            build_declared_type(
                ParseTreeMatcher::find_first_child_with_label(node,
                                                              "type_specifier"),
                declarator),
            collect_declarator_dimensions(declarator),
            build_expr(initializer), get_node_source_span(declarator_node)));
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

    if (node != nullptr) {
        for (const auto &child : node->children) {
            if (ParseTreeMatcher::label_equals(child.get(), "init_declarator_list")) {
                list_node = child.get();
            }
        }
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
        for (auto it = current->children.rbegin(); it != current->children.rend();
             ++it) {
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
            build_declared_type(
                ParseTreeMatcher::find_first_child_with_label(node,
                                                              "type_specifier"),
                declarator),
            collect_declarator_dimensions(declarator),
            initializer == nullptr ? nullptr : build_expr(initializer),
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
    const ParseTreeNode *type_specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "type_specifier");
    const ParseTreeNode *declarator_list =
        ParseTreeMatcher::find_first_child_with_label(node, "declarator_list");
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
        for (auto it = current->children.rbegin(); it != current->children.rend();
             ++it) {
            stack.push_back(it->get());
        }
    }

    for (const ParseTreeNode *declarator : declarators) {
        decls.push_back(std::make_unique<TypedefDecl>(
            extract_declarator_name(declarator),
            build_declared_type(type_specifier, declarator),
            collect_declarator_dimensions(declarator),
            get_node_source_span(declarator)));
    }

    if (decls.empty()) {
        decls.push_back(std::make_unique<UnknownDecl>("typedef_decl",
                                                      get_node_source_span(node)));
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
            if (ParseTreeMatcher::label_starts_with(child.get(), "IDENTIFIER")) {
                const std::string suffix = ParseTreeMatcher::extract_terminal_suffix(
                    child.get(), "IDENTIFIER");
                if (!suffix.empty()) {
                    name = suffix;
                    break;
                }
            }
        }
    }

    auto struct_decl =
        std::make_unique<StructDecl>(name, get_node_source_span(specifier));
    if (specifier != nullptr) {
        for (const auto &child : specifier->children) {
            if (ParseTreeMatcher::label_equals(child.get(), "struct_field_list_opt")) {
                for (auto &field : build_struct_fields(child.get())) {
                    struct_decl->add_field(std::move(field));
                }
            }
        }
    }
    return struct_decl;
}

std::unique_ptr<EnumDecl>
AstBuilder::build_enum_decl(const ParseTreeNode *node) const {
    const ParseTreeNode *specifier =
        ParseTreeMatcher::find_first_child_with_label(node, "enum_specifier");
    std::string name = "<anonymous>";
    if (specifier != nullptr) {
        for (const auto &child : specifier->children) {
            if (ParseTreeMatcher::label_starts_with(child.get(), "IDENTIFIER")) {
                const std::string suffix = ParseTreeMatcher::extract_terminal_suffix(
                    child.get(), "IDENTIFIER");
                if (!suffix.empty()) {
                    name = suffix;
                    break;
                }
            }
        }
    }

    auto enum_decl = std::make_unique<EnumDecl>(name, get_node_source_span(specifier));
    if (specifier != nullptr) {
        for (const auto &child : specifier->children) {
            if (ParseTreeMatcher::label_equals(child.get(), "enumerator_list_opt")) {
                for (auto &enumerator : build_enumerators(child.get())) {
                    enum_decl->add_enumerator(std::move(enumerator));
                }
            }
        }
    }
    return enum_decl;
}

std::unique_ptr<TypeNode>
AstBuilder::build_return_type(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return std::make_unique<UnknownTypeNode>("null type_specifier");
    }

    if (const ParseTreeNode *basic_type =
            ParseTreeMatcher::find_first_child_with_label(node, "basic_type")) {
        for (const auto &child : basic_type->children) {
            if (ParseTreeMatcher::label_starts_with(child.get(), "INT")) {
                return std::make_unique<BuiltinTypeNode>(
                    "int", get_node_source_span(node));
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "FLOAT")) {
                return std::make_unique<BuiltinTypeNode>(
                    "float", get_node_source_span(node));
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "VOID")) {
                return std::make_unique<BuiltinTypeNode>(
                    "void", get_node_source_span(node));
            }
        }
    }

    if (const ParseTreeNode *struct_specifier =
            ParseTreeMatcher::find_first_child_with_label(node, "struct_specifier")) {
        std::string name = "<anonymous>";
        for (const auto &child : struct_specifier->children) {
            if (ParseTreeMatcher::label_starts_with(child.get(), "IDENTIFIER")) {
                const std::string suffix = ParseTreeMatcher::extract_terminal_suffix(
                    child.get(), "IDENTIFIER");
                if (!suffix.empty()) {
                    name = suffix;
                    break;
                }
            }
        }
        return std::make_unique<StructTypeNode>(name,
                                                get_node_source_span(struct_specifier));
    }

    if (const ParseTreeNode *enum_specifier =
            ParseTreeMatcher::find_first_child_with_label(node, "enum_specifier")) {
        std::string name = "<anonymous>";
        for (const auto &child : enum_specifier->children) {
            if (ParseTreeMatcher::label_starts_with(child.get(), "IDENTIFIER")) {
                const std::string suffix = ParseTreeMatcher::extract_terminal_suffix(
                    child.get(), "IDENTIFIER");
                if (!suffix.empty()) {
                    name = suffix;
                    break;
                }
            }
        }
        return std::make_unique<EnumTypeNode>(name,
                                              get_node_source_span(enum_specifier));
    }

    return std::make_unique<UnknownTypeNode>(node->label, get_node_source_span(node));
}

std::unique_ptr<TypeNode> AstBuilder::build_declared_type(
    const ParseTreeNode *type_specifier, const ParseTreeNode *declarator) const {
    std::unique_ptr<TypeNode> declared_type = build_return_type(type_specifier);
    if (declarator == nullptr) {
        return declared_type;
    }

    const ParseTreeNode *pointer_node = nullptr;
    for (const auto &child : declarator->children) {
        if (ParseTreeMatcher::label_equals(child.get(), "pointer")) {
            pointer_node = child.get();
            break;
        }
    }

    const int pointer_levels = count_pointer_levels(pointer_node);
    for (int level = 0; level < pointer_levels; ++level) {
        declared_type = std::make_unique<PointerTypeNode>(
            std::move(declared_type),
            pointer_node == nullptr ? get_node_source_span(declarator)
                                    : get_node_source_span(pointer_node));
    }
    return declared_type;
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
        for (auto it = current->children.rbegin(); it != current->children.rend();
             ++it) {
            stack.push_back(it->get());
        }
    }

    for (const ParseTreeNode *field_node : field_nodes) {
        const ParseTreeNode *type_specifier =
            ParseTreeMatcher::find_first_child_with_label(field_node,
                                                          "type_specifier");
        const ParseTreeNode *declarator_list =
            ParseTreeMatcher::find_first_child_with_label(field_node,
                                                          "declarator_list");
        std::vector<const ParseTreeNode *> declarators;
        std::vector<const ParseTreeNode *> declarator_stack;
        if (declarator_list != nullptr) {
            declarator_stack.push_back(declarator_list);
        }
        while (!declarator_stack.empty()) {
            const ParseTreeNode *current = declarator_stack.back();
            declarator_stack.pop_back();
            if (current == nullptr) {
                continue;
            }
            if (ParseTreeMatcher::label_equals(current, "declarator")) {
                declarators.push_back(current);
                continue;
            }
            for (auto it = current->children.rbegin();
                 it != current->children.rend(); ++it) {
                declarator_stack.push_back(it->get());
            }
        }

        for (const ParseTreeNode *declarator : declarators) {
            fields.push_back(std::make_unique<FieldDecl>(
                extract_declarator_name(declarator),
                build_declared_type(type_specifier, declarator),
                collect_declarator_dimensions(declarator),
                get_node_source_span(declarator)));
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
        for (auto it = current->children.rbegin(); it != current->children.rend();
             ++it) {
            stack.push_back(it->get());
        }
    }

    for (const ParseTreeNode *enumerator_node : enumerator_nodes) {
        std::string name = "<anonymous>";
        std::unique_ptr<Expr> value = nullptr;
        for (const auto &child : enumerator_node->children) {
            if (ParseTreeMatcher::label_starts_with(child.get(), "IDENTIFIER")) {
                const std::string suffix = ParseTreeMatcher::extract_terminal_suffix(
                    child.get(), "IDENTIFIER");
                if (!suffix.empty()) {
                    name = suffix;
                }
            } else if (ParseTreeMatcher::label_equals(child.get(), "const_expr")) {
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
    const ParseTreeNode *node, std::vector<const ParseTreeNode *> &items) const {
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
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(), "IF")) {
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
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(), "DO")) {
        return std::make_unique<DoWhileStmt>(
            build_stmt(stmt_node->children[1].get()),
            build_expr(stmt_node->children[4].get()),
            get_node_source_span(stmt_node));
    }

    if (stmt_node->children.size() == 9 &&
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(), "FOR")) {
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
        return std::make_unique<ForStmt>(std::move(init), std::move(condition),
                                         std::move(step),
                                         build_stmt(stmt_node->children[8].get()),
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
        ParseTreeMatcher::label_starts_with(stmt_node->children[0].get(), "CASE")) {
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

    for (const auto &child : stmt_node->children) {
        if (ParseTreeMatcher::label_starts_with(child.get(), "RETURN")) {
            const ParseTreeNode *expr_opt =
                ParseTreeMatcher::find_first_child_with_label(stmt_node,
                                                              "expr_opt");
            if (expr_opt == nullptr || expr_opt->children.empty()) {
                return std::make_unique<ReturnStmt>(nullptr,
                                                    get_node_source_span(stmt_node));
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

    if (ParseTreeMatcher::label_starts_with(node, "IDENTIFIER")) {
        const std::string name =
            ParseTreeMatcher::extract_terminal_suffix(node, "IDENTIFIER");
        return std::make_unique<IdentifierExpr>(name.empty() ? node->label : name,
                                                get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "unary_expr") &&
        node->children.size() == 2) {
        if (ParseTreeMatcher::label_starts_with(node->children[0].get(), "INC") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(), "DEC")) {
            return std::make_unique<PrefixExpr>(
                get_operator_text(node->children[0].get()),
                build_expr(node->children[1].get()), get_node_source_span(node));
        }

        if (ParseTreeMatcher::label_starts_with(node->children[0].get(), "PLUS") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(), "MINUS") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(), "NOT") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(), "BITNOT") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(), "BITAND") ||
            ParseTreeMatcher::label_starts_with(node->children[0].get(), "MUL")) {
            return std::make_unique<UnaryExpr>(
                get_operator_text(node->children[0].get()),
                build_expr(node->children[1].get()), get_node_source_span(node));
        }
    }

    if (ParseTreeMatcher::label_equals(node, "assignment_expr") &&
        node->children.size() == 3 &&
        ParseTreeMatcher::label_starts_with(node->children[1].get(), "ASSIGN")) {
        return std::make_unique<AssignExpr>(build_expr(node->children[0].get()),
                                            build_expr(node->children[2].get()),
                                            get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "init_val") ||
        ParseTreeMatcher::label_equals(node, "const_init_val")) {
        if (node->children.size() == 1) {
            return build_expr(node->children[0].get());
        }
        if (!node->children.empty() &&
            ParseTreeMatcher::label_starts_with(node->children[0].get(), "LBRACE")) {
            return build_init_list_expr(node);
        }
    }

    if ((ParseTreeMatcher::label_equals(node, "postfix_expr") ||
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
        node->children.size() == 3 && is_binary_operator_label(node->children[1].get())) {
        return std::make_unique<BinaryExpr>(
            get_operator_text(node->children[1].get()),
            build_expr(node->children[0].get()),
            build_expr(node->children[2].get()), get_node_source_span(node));
    }

    if (ParseTreeMatcher::label_equals(node, "postfix_expr") &&
        node->children.size() == 4) {
        if (ParseTreeMatcher::label_starts_with(node->children[1].get(), "LPAREN")) {
            std::vector<std::unique_ptr<Expr>> arguments;
            if (!ParseTreeMatcher::label_equals(node->children[2].get(),
                                                "argument_expr_list_opt")) {
                arguments = build_argument_exprs(node->children[2].get());
            } else if (!node->children[2]->children.empty()) {
                arguments = build_argument_exprs(node->children[2]->children[0].get());
            }
            return std::make_unique<CallExpr>(build_expr(node->children[0].get()),
                                              std::move(arguments),
                                              get_node_source_span(node));
        }
        if (ParseTreeMatcher::label_starts_with(node->children[1].get(), "LBRACKET")) {
            return std::make_unique<IndexExpr>(build_expr(node->children[0].get()),
                                               build_expr(node->children[2].get()),
                                               get_node_source_span(node));
        }
    }

    if (ParseTreeMatcher::label_equals(node, "postfix_expr") &&
        node->children.size() == 3 &&
        ParseTreeMatcher::label_starts_with(node->children[1].get(), "ARROW")) {
        const std::string member_name = ParseTreeMatcher::extract_terminal_suffix(
            node->children[2].get(), "IDENTIFIER");
        return std::make_unique<MemberExpr>(
            get_operator_text(node->children[1].get()),
            build_expr(node->children[0].get()),
            member_name.empty() ? node->children[2]->label : member_name,
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
         ParseTreeMatcher::label_equals(node, "primary_expr") ||
         ParseTreeMatcher::label_equals(node, "unary_expr") ||
         ParseTreeMatcher::label_equals(node, "postfix_expr")) &&
        node->children.size() == 1) {
        return build_expr(node->children[0].get());
    }

    if (ParseTreeMatcher::label_equals(node, "primary_expr") &&
        node->children.size() == 3 &&
        ParseTreeMatcher::label_starts_with(node->children[0].get(), "LPAREN")) {
        return build_expr(node->children[1].get());
    }

    return std::make_unique<UnknownExpr>(node->label, get_node_source_span(node));
}

std::vector<std::unique_ptr<Expr>>
AstBuilder::collect_declarator_dimensions(const ParseTreeNode *node) const {
    std::vector<std::unique_ptr<Expr>> dimensions;
    if (node == nullptr) {
        return dimensions;
    }

    for (const auto &child : node->children) {
        if (ParseTreeMatcher::label_equals(child.get(), "expr_opt") &&
            !child->children.empty()) {
            dimensions.push_back(build_expr(child->children[0].get()));
        }
        auto nested_dimensions = collect_declarator_dimensions(child.get());
        for (auto &dimension : nested_dimensions) {
            dimensions.push_back(std::move(dimension));
        }
    }
    return dimensions;
}

std::string AstBuilder::extract_declarator_name(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return "<unnamed>";
    }
    if (ParseTreeMatcher::label_starts_with(node, "IDENTIFIER")) {
        const std::string name =
            ParseTreeMatcher::extract_terminal_suffix(node, "IDENTIFIER");
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
    for (const ParseTreeNode *expr_node : expr_nodes) {
        arguments.push_back(build_expr(expr_node));
    }
    return arguments;
}

void AstBuilder::collect_argument_expr_nodes(
    const ParseTreeNode *node, std::vector<const ParseTreeNode *> &expr_nodes) const {
    if (node == nullptr) {
        return;
    }
    if (ParseTreeMatcher::label_equals(node, "expr")) {
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
            (ParseTreeMatcher::label_equals(child.get(), "init_val_list") ||
             ParseTreeMatcher::label_equals(child.get(), "const_init_val_list"))) {
            collect_direct_init_value_nodes(child.get(), init_value_nodes);
        }
    }

    for (const ParseTreeNode *init_value_node : init_value_nodes) {
        init_list->add_element(build_expr(init_value_node));
    }
    return init_list;
}

void AstBuilder::collect_direct_init_value_nodes(
    const ParseTreeNode *node, std::vector<const ParseTreeNode *> &nodes) const {
    if (node == nullptr) {
        return;
    }

    for (const auto &child : node->children) {
        if (child == nullptr) {
            continue;
        }
        if (ParseTreeMatcher::label_equals(child.get(), "init_val") ||
            ParseTreeMatcher::label_equals(child.get(), "const_init_val")) {
            nodes.push_back(child.get());
            continue;
        }
        if (ParseTreeMatcher::label_equals(child.get(), "init_val_list") ||
            ParseTreeMatcher::label_equals(child.get(), "const_init_val_list")) {
            collect_direct_init_value_nodes(child.get(), nodes);
        }
    }
}

int AstBuilder::count_pointer_levels(const ParseTreeNode *node) const {
    if (node == nullptr) {
        return 0;
    }

    int nested_levels = 0;
    for (const auto &child : node->children) {
        if (ParseTreeMatcher::label_equals(child.get(), "pointer")) {
            nested_levels = count_pointer_levels(child.get());
            break;
        }
    }
    return 1 + nested_levels;
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
           ParseTreeMatcher::label_starts_with(node, "EQ") ||
           ParseTreeMatcher::label_starts_with(node, "NE") ||
           ParseTreeMatcher::label_starts_with(node, "LT") ||
           ParseTreeMatcher::label_starts_with(node, "LE") ||
           ParseTreeMatcher::label_starts_with(node, "GT") ||
           ParseTreeMatcher::label_starts_with(node, "GE") ||
           ParseTreeMatcher::label_starts_with(node, "AND") ||
           ParseTreeMatcher::label_starts_with(node, "OR") ||
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
