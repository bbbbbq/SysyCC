#pragma once

#include <string>
#include <utility>
#include <vector>

#include "common/source_span.hpp"

namespace sysycc {

enum class AttributeSyntaxKind : unsigned char {
    GNU,
};

enum class AttributeAttachmentSite : unsigned char {
    DeclSpecifier,
    DeclaratorSuffix,
    Parameter,
    TypeSpecifier,
};

class ParsedAttributeArgument {
  private:
    std::string raw_text_;
    SourceSpan source_span_;

  public:
    ParsedAttributeArgument() = default;

    ParsedAttributeArgument(std::string raw_text, SourceSpan source_span = {})
        : raw_text_(std::move(raw_text)), source_span_(source_span) {}

    const std::string &get_raw_text() const noexcept { return raw_text_; }
    const SourceSpan &get_source_span() const noexcept { return source_span_; }
};

class ParsedAttribute {
  private:
    AttributeSyntaxKind syntax_kind_ = AttributeSyntaxKind::GNU;
    std::string name_;
    std::vector<ParsedAttributeArgument> arguments_;
    SourceSpan source_span_;

  public:
    ParsedAttribute() = default;

    ParsedAttribute(AttributeSyntaxKind syntax_kind, std::string name,
                    std::vector<ParsedAttributeArgument> arguments,
                    SourceSpan source_span = {})
        : syntax_kind_(syntax_kind), name_(std::move(name)),
          arguments_(std::move(arguments)), source_span_(source_span) {}

    AttributeSyntaxKind get_syntax_kind() const noexcept {
        return syntax_kind_;
    }
    const std::string &get_name() const noexcept { return name_; }
    const std::vector<ParsedAttributeArgument> &get_arguments() const noexcept {
        return arguments_;
    }
    const SourceSpan &get_source_span() const noexcept { return source_span_; }
};

class ParsedAttributeList {
  private:
    AttributeAttachmentSite attachment_site_ =
        AttributeAttachmentSite::DeclSpecifier;
    std::vector<ParsedAttribute> attributes_;
    SourceSpan source_span_;

  public:
    ParsedAttributeList() = default;

    ParsedAttributeList(AttributeAttachmentSite attachment_site,
                        std::vector<ParsedAttribute> attributes,
                        SourceSpan source_span = {})
        : attachment_site_(attachment_site),
          attributes_(std::move(attributes)), source_span_(source_span) {}

    AttributeAttachmentSite get_attachment_site() const noexcept {
        return attachment_site_;
    }
    const std::vector<ParsedAttribute> &get_attributes() const noexcept {
        return attributes_;
    }
    const SourceSpan &get_source_span() const noexcept { return source_span_; }
    bool empty() const noexcept { return attributes_.empty(); }
};

} // namespace sysycc
