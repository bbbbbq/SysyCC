#include "frontend/preprocess/preprocess.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace sysycc {

namespace {

std::string trim_left(const std::string &text) {
    std::size_t index = 0;
    while (index < text.size() &&
           std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }
    return text.substr(index);
}

bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_identifier_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

} // namespace

void PreprocessorState::clear() {
    macro_definitions_.clear();
    output_lines_.clear();
}

bool PreprocessorState::has_macro(const std::string &name) const {
    return macro_definitions_.find(name) != macro_definitions_.end();
}

const MacroDefinition *
PreprocessorState::get_macro_definition(const std::string &name) const noexcept {
    const auto iter = macro_definitions_.find(name);
    if (iter == macro_definitions_.end()) {
        return nullptr;
    }

    return &iter->second;
}

void PreprocessorState::define_macro(const MacroDefinition &definition) {
    macro_definitions_[definition.get_name()] = definition;
}

void PreprocessorState::undefine_macro(const std::string &name) {
    macro_definitions_.erase(name);
}

void PreprocessorState::append_output_line(std::string line) {
    output_lines_.push_back(std::move(line));
}

std::string PreprocessorState::build_output_text() const {
    std::ostringstream oss;
    for (std::size_t index = 0; index < output_lines_.size(); ++index) {
        if (index != 0) {
            oss << '\n';
        }
        oss << output_lines_[index];
    }
    return oss.str();
}

PassResult PreprocessorDriver::Run(CompilerContext &context) const {
    PreprocessorState state;
    state.clear();

    const PassResult result = preprocess_file(context.get_input_file(), state);
    if (!result.ok) {
        context.set_preprocessed_file_path("");
        return result;
    }

    std::string output_file_path;
    const PassResult write_result =
        write_preprocessed_file(context, state, output_file_path);
    if (!write_result.ok) {
        context.set_preprocessed_file_path("");
        return write_result;
    }

    context.set_preprocessed_file_path(std::move(output_file_path));
    return PassResult::Success();
}

PassResult PreprocessorDriver::write_preprocessed_file(
    const CompilerContext &context, const PreprocessorState &state,
    std::string &output_file_path) const {
    const std::filesystem::path output_dir("build/intermediate_results");
    std::filesystem::create_directories(output_dir);

    const std::filesystem::path input_path(context.get_input_file());
    const std::filesystem::path output_path =
        output_dir / (input_path.stem().string() + ".preprocessed.sy");

    std::ofstream ofs(output_path);
    if (!ofs.is_open()) {
        return PassResult::Failure(
            "failed to open preprocessed output file in intermediate_results");
    }

    ofs << state.build_output_text();
    if (!ofs.good()) {
        return PassResult::Failure("failed to write preprocessed output file");
    }

    output_file_path = output_path.string();
    return PassResult::Success();
}

PassResult PreprocessorDriver::preprocess_file(const std::string &file_path,
                                               PreprocessorState &state) const {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        return PassResult::Failure(
            "failed to open input file for preprocessor");
    }

    std::string line;
    int line_number = 1;
    while (std::getline(ifs, line)) {
        if (!is_preprocess_directive(line)) {
            state.append_output_line(expand_macros(line, state));
            ++line_number;
            continue;
        }

        std::istringstream iss(trim_left(line));
        std::string directive;
        iss >> directive;

        if (directive == "#define") {
            std::string name;
            iss >> name;
            if (name.empty()) {
                return PassResult::Failure(
                    "invalid #define directive: missing macro name");
            }

            if (state.has_macro(name)) {
                return PassResult::Failure("macro redefinition is not allowed: " +
                                           name);
            }

            std::string replacement;
            std::getline(iss, replacement);
            replacement = trim_left(replacement);

            state.define_macro(MacroDefinition(
                std::move(name), std::move(replacement),
                SourceSpan(line_number, 1, line_number,
                           static_cast<int>(line.size()))));
            ++line_number;
            continue;
        }

        if (directive == "#undef") {
            std::string name;
            iss >> name;
            if (name.empty()) {
                return PassResult::Failure(
                    "invalid #undef directive: missing macro name");
            }

            state.undefine_macro(name);
            ++line_number;
            continue;
        }

        return PassResult::Failure("unsupported preprocess directive: " +
                                   directive);
    }

    return PassResult::Success();
}

std::string PreprocessorDriver::expand_macros(
    const std::string &line, const PreprocessorState &state) const {
    std::string output;
    std::size_t index = 0;
    while (index < line.size()) {
        if (!is_identifier_start(line[index])) {
            output.push_back(line[index]);
            ++index;
            continue;
        }

        std::size_t end = index + 1;
        while (end < line.size() && is_identifier_char(line[end])) {
            ++end;
        }

        const std::string identifier = line.substr(index, end - index);
        const MacroDefinition *definition =
            state.get_macro_definition(identifier);
        if (definition != nullptr) {
            output += definition->get_replacement();
        } else {
            output += identifier;
        }
        index = end;
    }

    return output;
}

bool PreprocessorDriver::is_preprocess_directive(const std::string &line) const {
    const std::string trimmed = trim_left(line);
    return !trimmed.empty() && trimmed[0] == '#';
}

PassKind PreprocessPass::Kind() const { return PassKind::Preprocess; }

const char *PreprocessPass::Name() const { return "PreprocessPass"; }

PassResult PreprocessPass::Run(CompilerContext &context) {
    return preprocessor_driver_.Run(context);
}

} // namespace sysycc
