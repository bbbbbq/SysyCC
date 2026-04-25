#include "frontend/attribute/attribute_parser.hpp"

#include <string>
#include <utility>
#include <vector>

#include "frontend/ast/detail/parse_tree_matcher.hpp"

namespace sysycc {

namespace {

using detail::ParseTreeMatcher;

std::string extract_terminal_text(const ParseTreeNode *node) {
    if (node == nullptr) {
        return "";
    }
    const std::size_t separator = node->label.find(' ');
    if (separator == std::string::npos || separator + 1 >= node->label.size()) {
        return "";
    }
    return node->label.substr(separator + 1);
}

void collect_nodes_with_label(const ParseTreeNode *node, const char *label,
                              std::vector<const ParseTreeNode *> &matches) {
    if (node == nullptr || label == nullptr) {
        return;
    }
    if (ParseTreeMatcher::label_equals(node, label)) {
        matches.push_back(node);
        return;
    }
    for (const auto &child : node->children) {
        collect_nodes_with_label(child.get(), label, matches);
    }
}

void collect_terminal_texts(const ParseTreeNode *node,
                            std::vector<std::string> &texts) {
    if (node == nullptr) {
        return;
    }
    if (node->children.empty()) {
        const std::string text = extract_terminal_text(node);
        if (!text.empty()) {
            texts.push_back(text);
        }
        return;
    }
    for (const auto &child : node->children) {
        collect_terminal_texts(child.get(), texts);
    }
}

std::string join_terminal_texts(const ParseTreeNode *node) {
    std::vector<std::string> texts;
    collect_terminal_texts(node, texts);
    std::string joined;
    for (std::size_t index = 0; index < texts.size(); ++index) {
        if (index != 0U) {
            joined += " ";
        }
        joined += texts[index];
    }
    return joined;
}

std::vector<ParsedAttributeArgument>
parse_attribute_arguments(const ParseTreeNode *node) {
    std::vector<const ParseTreeNode *> argument_nodes;
    collect_nodes_with_label(node, "attribute_argument", argument_nodes);

    std::vector<ParsedAttributeArgument> arguments;
    for (const ParseTreeNode *argument_node : argument_nodes) {
        arguments.emplace_back(join_terminal_texts(argument_node),
                               argument_node->source_span);
    }
    return arguments;
}

ParsedAttribute parse_attribute(const ParseTreeNode *node) {
    std::string name;
    std::vector<ParsedAttributeArgument> arguments;

    if (node != nullptr) {
        for (const auto &child : node->children) {
            if (ParseTreeMatcher::label_equals(child.get(), "attribute_name")) {
                name = join_terminal_texts(child.get());
                continue;
            }
            if (ParseTreeMatcher::label_starts_with(child.get(), "IDENTIFIER")) {
                name = ParseTreeMatcher::extract_terminal_suffix(child.get(),
                                                                 "IDENTIFIER");
                continue;
            }
            if (ParseTreeMatcher::label_equals(child.get(),
                                               "attribute_argument_list_opt")) {
                arguments = parse_attribute_arguments(child.get());
            }
        }
    }

    return ParsedAttribute(AttributeSyntaxKind::GNU, std::move(name),
                           std::move(arguments),
                           node != nullptr ? node->source_span : SourceSpan{});
}

} // namespace

ParsedAttributeList AttributeParser::parse_gnu_attribute_specifier_seq(
    const ParseTreeNode *node, AttributeAttachmentSite attachment_site) const {
    std::vector<const ParseTreeNode *> attribute_nodes;
    collect_nodes_with_label(node, "attribute", attribute_nodes);

    std::vector<ParsedAttribute> attributes;
    for (const ParseTreeNode *attribute_node : attribute_nodes) {
        attributes.push_back(parse_attribute(attribute_node));
    }

    return ParsedAttributeList(attachment_site, std::move(attributes),
                               node != nullptr ? node->source_span
                                               : SourceSpan{});
}

} // namespace sysycc
