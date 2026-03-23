#pragma once

#include <filesystem>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace sysycc {

namespace detail {

inline std::vector<std::string> get_default_system_include_directories() {
    std::vector<std::string> system_include_directories;

#if defined(__APPLE__)
    const std::filesystem::path clang_include_root(
        "/Library/Developer/CommandLineTools/usr/lib/clang");
    if (std::filesystem::exists(clang_include_root)) {
        for (const auto &entry :
             std::filesystem::directory_iterator(clang_include_root)) {
            if (!entry.is_directory()) {
                continue;
            }

            const std::filesystem::path include_path = entry.path() / "include";
            if (std::filesystem::exists(include_path)) {
                system_include_directories.push_back(include_path.string());
            }
        }
    }

    const std::filesystem::path sdk_include_path(
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include");
    if (std::filesystem::exists(sdk_include_path)) {
        system_include_directories.push_back(sdk_include_path.string());
    }

    const std::filesystem::path clt_include_path(
        "/Library/Developer/CommandLineTools/usr/include");
    if (std::filesystem::exists(clt_include_path)) {
        system_include_directories.push_back(clt_include_path.string());
    }
#else
    const std::filesystem::path local_include_path("/usr/local/include");
    if (std::filesystem::exists(local_include_path)) {
        system_include_directories.push_back(local_include_path.string());
    }

    const std::filesystem::path usr_include_path("/usr/include");
    if (std::filesystem::exists(usr_include_path)) {
        system_include_directories.push_back(usr_include_path.string());
    }
#endif

    return system_include_directories;
}

} // namespace detail

enum class StopAfterStage : uint8_t {
    None,
    Preprocess,
    Lex,
    Parse,
    Ast,
    Semantic,
    IR,
};

// Stores the configuration for one compiler invocation.
class ComplierOption {
  private:
    std::string input_file_;
    std::string output_file_;
    std::vector<std::string> include_directories_;
    std::vector<std::string> system_include_directories_ =
        detail::get_default_system_include_directories();
    bool dump_tokens_ = false;
    bool dump_parse_ = false;
    bool dump_ast_ = false;
    bool dump_ir_ = false;
    StopAfterStage stop_after_stage_ = StopAfterStage::None;
    bool enable_gnu_dialect_ = true;
    bool enable_clang_dialect_ = true;
    bool enable_builtin_type_extension_pack_ = true;

  public:
    ComplierOption() = default;

    explicit ComplierOption(std::string input_file)
        : input_file_(std::move(input_file)) {}

    ComplierOption(std::string input_file, std::string output_file)
        : input_file_(std::move(input_file)),
          output_file_(std::move(output_file)) {}

    const std::string &get_input_file() const noexcept { return input_file_; }

    void set_input_file(std::string input_file) {
        input_file_ = std::move(input_file);
    }

    const std::string &get_output_file() const noexcept { return output_file_; }

    void set_output_file(std::string output_file) {
        output_file_ = std::move(output_file);
    }

    const std::vector<std::string> &get_include_directories() const noexcept {
        return include_directories_;
    }

    void set_include_directories(std::vector<std::string> include_directories) {
        include_directories_ = std::move(include_directories);
    }

    void add_include_directory(std::string include_directory) {
        include_directories_.push_back(std::move(include_directory));
    }

    const std::vector<std::string> &
    get_system_include_directories() const noexcept {
        return system_include_directories_;
    }

    void set_system_include_directories(
        std::vector<std::string> system_include_directories) {
        system_include_directories_ = std::move(system_include_directories);
    }

    bool dump_tokens() const noexcept { return dump_tokens_; }

    void set_dump_tokens(bool dump_tokens) noexcept {
        dump_tokens_ = dump_tokens;
    }

    bool dump_ast() const noexcept { return dump_ast_; }

    void set_dump_ast(bool dump_ast) noexcept { dump_ast_ = dump_ast; }

    bool dump_parse() const noexcept { return dump_parse_; }

    void set_dump_parse(bool dump_parse) noexcept { dump_parse_ = dump_parse; }

    bool dump_ir() const noexcept { return dump_ir_; }

    void set_dump_ir(bool dump_ir) noexcept { dump_ir_ = dump_ir; }

    StopAfterStage get_stop_after_stage() const noexcept {
        return stop_after_stage_;
    }

    void set_stop_after_stage(StopAfterStage stop_after_stage) noexcept {
        stop_after_stage_ = stop_after_stage;
    }

    bool get_enable_gnu_dialect() const noexcept {
        return enable_gnu_dialect_;
    }

    void set_enable_gnu_dialect(bool enable_gnu_dialect) noexcept {
        enable_gnu_dialect_ = enable_gnu_dialect;
    }

    bool get_enable_clang_dialect() const noexcept {
        return enable_clang_dialect_;
    }

    void set_enable_clang_dialect(bool enable_clang_dialect) noexcept {
        enable_clang_dialect_ = enable_clang_dialect;
    }

    bool get_enable_builtin_type_extension_pack() const noexcept {
        return enable_builtin_type_extension_pack_;
    }

    void set_enable_builtin_type_extension_pack(
        bool enable_builtin_type_extension_pack) noexcept {
        enable_builtin_type_extension_pack_ =
            enable_builtin_type_extension_pack;
    }
};

} // namespace sysycc
