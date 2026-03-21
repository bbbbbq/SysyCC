#pragma once

#include <string>

#include "common/source_span.hpp"
#include "frontend/ast/ast_node.hpp"
#include "frontend/dialects/ast_feature_registry.hpp"

namespace sysycc::detail {

class AstFeatureErrorInfo {
  private:
    std::string message_;
    SourceSpan source_span_;

  public:
    AstFeatureErrorInfo() = default;

    AstFeatureErrorInfo(std::string message, SourceSpan source_span)
        : message_(std::move(message)), source_span_(source_span) {}

    const std::string &get_message() const noexcept { return message_; }

    const SourceSpan &get_source_span() const noexcept { return source_span_; }

    bool empty() const noexcept { return message_.empty(); }
};

class AstFeatureValidator {
  public:
    bool validate(const AstNode *ast_root,
                  const AstFeatureRegistry &feature_registry,
                  AstFeatureErrorInfo &error_info) const;
};

} // namespace sysycc::detail
