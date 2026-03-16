#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "compiler/pass/pass.hpp"
#include "common/source_span.hpp"

namespace sysycc {

class MacroDefinition {
  private:
    std::string name_;
    std::string replacement_;
    SourceSpan source_span_;

  public:
    MacroDefinition() = default;

    MacroDefinition(std::string name, std::string replacement,
                    SourceSpan source_span = {})
        : name_(std::move(name)),
          replacement_(std::move(replacement)),
          source_span_(std::move(source_span)) {}

    const std::string &get_name() const noexcept { return name_; }

    void set_name(std::string name) { name_ = std::move(name); }

    const std::string &get_replacement() const noexcept {
        return replacement_;
    }

    void set_replacement(std::string replacement) {
        replacement_ = std::move(replacement);
    }

    const SourceSpan &get_source_span() const noexcept { return source_span_; }

    void set_source_span(SourceSpan source_span) {
        source_span_ = std::move(source_span);
    }
};

class PreprocessorState {
  private:
    std::unordered_map<std::string, MacroDefinition> macro_definitions_;
    std::vector<std::string> output_lines_;

  public:
    PreprocessorState() = default;

    void clear();

    bool has_macro(const std::string &name) const;

    const MacroDefinition *get_macro_definition(
        const std::string &name) const noexcept;

    void define_macro(const MacroDefinition &definition);

    void undefine_macro(const std::string &name);

    const std::unordered_map<std::string, MacroDefinition> &
    get_macro_definitions() const noexcept {
        return macro_definitions_;
    }

    void append_output_line(std::string line);

    const std::vector<std::string> &get_output_lines() const noexcept {
        return output_lines_;
    }

    std::string build_output_text() const;
};

class PreprocessorDriver {
  public:
    PassResult Run(CompilerContext &context) const;

  private:
    PassResult write_preprocessed_file(const CompilerContext &context,
                                       const PreprocessorState &state,
                                       std::string &output_file_path) const;
    PassResult preprocess_file(const std::string &file_path,
                               PreprocessorState &state) const;
    std::string expand_macros(const std::string &line,
                              const PreprocessorState &state) const;
    bool is_preprocess_directive(const std::string &line) const;
};

class PreprocessPass : public Pass {
  private:
    PreprocessorDriver preprocessor_driver_;

  public:
    PassKind Kind() const override;
    const char *Name() const override;
    PassResult Run(CompilerContext &context) override;
};

} // namespace sysycc
