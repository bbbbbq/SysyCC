#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "backend/asm_gen/aarch64/model/aarch64_meta_model.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_machine_ir.hpp"
#include "backend/asm_gen/aarch64/model/aarch64_symbol_reference_model.hpp"

namespace sysycc {

enum class AArch64RelocationKind : unsigned char {
    None,
    Absolute32,
    Absolute64,
    Page21,
    PageOffset12,
    Branch26,
    Call26,
};

struct AArch64RelocationRecord {
    AArch64RelocationKind kind = AArch64RelocationKind::None;
    AArch64SymbolReference target;
    std::size_t offset = 0;
};

class AArch64DataFragment {
  private:
    struct ZeroFillPayload {
        std::size_t size = 0;
    };

    struct ByteSequencePayload {
        std::vector<std::uint8_t> bytes;
        std::vector<AArch64RelocationRecord> relocations;
    };

    enum class ScalarKind : unsigned char {
        Byte,
        HalfWord,
        Word,
        XWord,
    };

    struct ScalarValuePayload {
        ScalarKind scalar_kind = ScalarKind::Word;
        std::uint64_t value = 0;
        std::vector<AArch64RelocationRecord> relocations;
    };

    using Payload =
        std::variant<ZeroFillPayload, ByteSequencePayload, ScalarValuePayload>;

    Payload payload_;

  public:
    static AArch64DataFragment zero_fill(std::size_t size) {
        return AArch64DataFragment(ZeroFillPayload{size});
    }

    static AArch64DataFragment byte_sequence(
        std::vector<std::uint8_t> bytes,
        std::vector<AArch64RelocationRecord> relocations = {}) {
        return AArch64DataFragment(
            ByteSequencePayload{std::move(bytes), std::move(relocations)});
    }

    static AArch64DataFragment scalar_byte(
        std::uint64_t value, std::vector<AArch64RelocationRecord> relocations = {}) {
        return AArch64DataFragment(ScalarValuePayload{
            ScalarKind::Byte, value, std::move(relocations)});
    }

    static AArch64DataFragment scalar_halfword(
        std::uint64_t value, std::vector<AArch64RelocationRecord> relocations = {}) {
        return AArch64DataFragment(ScalarValuePayload{
            ScalarKind::HalfWord, value, std::move(relocations)});
    }

    static AArch64DataFragment scalar_word(
        std::uint64_t value, std::vector<AArch64RelocationRecord> relocations = {}) {
        return AArch64DataFragment(ScalarValuePayload{
            ScalarKind::Word, value, std::move(relocations)});
    }

    static AArch64DataFragment scalar_xword(
        std::uint64_t value, std::vector<AArch64RelocationRecord> relocations = {}) {
        return AArch64DataFragment(ScalarValuePayload{
            ScalarKind::XWord, value, std::move(relocations)});
    }

    bool is_zero_fill() const noexcept {
        return std::holds_alternative<ZeroFillPayload>(payload_);
    }

    bool is_byte_sequence() const noexcept {
        return std::holds_alternative<ByteSequencePayload>(payload_);
    }

    bool is_scalar_value() const noexcept {
        return std::holds_alternative<ScalarValuePayload>(payload_);
    }

    const ZeroFillPayload *get_zero_fill() const noexcept {
        return std::get_if<ZeroFillPayload>(&payload_);
    }

    const ByteSequencePayload *get_byte_sequence() const noexcept {
        return std::get_if<ByteSequencePayload>(&payload_);
    }

    const ScalarValuePayload *get_scalar_value() const noexcept {
        return std::get_if<ScalarValuePayload>(&payload_);
    }

    std::size_t get_scalar_size() const noexcept {
        const auto *scalar = get_scalar_value();
        if (scalar == nullptr) {
            return 0;
        }
        switch (scalar->scalar_kind) {
        case ScalarKind::Byte:
            return 1;
        case ScalarKind::HalfWord:
            return 2;
        case ScalarKind::Word:
            return 4;
        case ScalarKind::XWord:
            return 8;
        }
        return 0;
    }

    std::uint64_t get_scalar_bits() const noexcept {
        const auto *scalar = get_scalar_value();
        return scalar == nullptr ? 0 : scalar->value;
    }

    std::size_t get_size() const noexcept {
        if (const auto *zero_fill = get_zero_fill(); zero_fill != nullptr) {
            return zero_fill->size;
        }
        if (const auto *bytes = get_byte_sequence(); bytes != nullptr) {
            return bytes->bytes.size();
        }
        return get_scalar_size();
    }

    const std::vector<AArch64RelocationRecord> &get_relocations() const noexcept {
        static const std::vector<AArch64RelocationRecord> empty_relocations;
        if (const auto *bytes = get_byte_sequence(); bytes != nullptr) {
            return bytes->relocations;
        }
        if (const auto *scalar = get_scalar_value(); scalar != nullptr) {
            return scalar->relocations;
        }
        return empty_relocations;
    }

  private:
    explicit AArch64DataFragment(Payload payload) : payload_(std::move(payload)) {}
};

class AArch64Symbol {
  private:
    std::string name_;
    AArch64SymbolKind kind_ = AArch64SymbolKind::Object;
    std::optional<AArch64SectionKind> section_kind_;
    bool is_defined_ = false;
    AArch64SymbolBinding binding_ = AArch64SymbolBinding::Unknown;
    bool is_referenced_ = false;

  public:
    explicit AArch64Symbol(std::string name) : name_(std::move(name)) {}

    const std::string &get_name() const noexcept { return name_; }
    AArch64SymbolKind get_kind() const noexcept { return kind_; }
    void set_kind(AArch64SymbolKind kind) noexcept { kind_ = kind; }
    const std::optional<AArch64SectionKind> &get_section_kind() const noexcept {
        return section_kind_;
    }
    void set_section_kind(AArch64SectionKind section_kind) noexcept {
        section_kind_ = section_kind;
    }
    bool get_is_defined() const noexcept { return is_defined_; }
    void set_is_defined(bool is_defined) noexcept { is_defined_ = is_defined; }
    AArch64SymbolBinding get_binding() const noexcept { return binding_; }
    void set_binding(AArch64SymbolBinding binding) noexcept {
        if (binding != AArch64SymbolBinding::Unknown) {
            binding_ = binding;
        }
    }
    bool get_is_global() const noexcept {
        return binding_ == AArch64SymbolBinding::Global;
    }
    void set_is_global(bool is_global) noexcept {
        binding_ = is_global ? AArch64SymbolBinding::Global
                             : AArch64SymbolBinding::Local;
    }
    bool get_is_referenced() const noexcept { return is_referenced_; }
    void mark_referenced() noexcept { is_referenced_ = true; }
};

class AArch64DataObject {
  private:
    AArch64SectionKind section_kind_ = AArch64SectionKind::Data;
    std::string symbol_name_;
    bool is_global_symbol_ = false;
    std::size_t align_log2_ = 0;
    std::vector<AArch64DataFragment> fragments_;

  public:
    AArch64DataObject(AArch64SectionKind section_kind, std::string symbol_name,
                      bool is_global_symbol, std::size_t align_log2)
        : section_kind_(section_kind),
          symbol_name_(std::move(symbol_name)),
          is_global_symbol_(is_global_symbol),
          align_log2_(align_log2) {}

    AArch64SectionKind get_section_kind() const noexcept { return section_kind_; }
    const std::string &get_symbol_name() const noexcept { return symbol_name_; }
    bool get_is_global_symbol() const noexcept { return is_global_symbol_; }
    std::size_t get_align_log2() const noexcept { return align_log2_; }
    std::size_t get_size() const noexcept {
        std::size_t size = 0;
        for (const AArch64DataFragment &fragment : fragments_) {
            size += fragment.get_size();
        }
        return size;
    }
    void append_fragment(AArch64DataFragment fragment) {
        fragments_.push_back(std::move(fragment));
    }
    const std::vector<AArch64DataFragment> &get_fragments() const noexcept {
        return fragments_;
    }
};

class AArch64ObjectModule {
  private:
    std::vector<std::string> preamble_lines_;
    std::vector<AArch64DebugFileEntry> debug_file_entries_;
    std::unordered_map<std::string, unsigned> debug_file_ids_;
    std::vector<AArch64DataObject> data_objects_;
    std::map<std::string, AArch64Symbol> symbols_;

  public:
    const std::vector<std::string> &get_preamble_lines() const noexcept {
        return preamble_lines_;
    }
    void append_preamble_line(std::string line) {
        preamble_lines_.push_back(std::move(line));
    }
    unsigned record_debug_file(std::string path) {
        const auto existing = debug_file_ids_.find(path);
        if (existing != debug_file_ids_.end()) {
            return existing->second;
        }
        const unsigned next_index =
            static_cast<unsigned>(debug_file_entries_.size() + 1);
        debug_file_ids_[path] = next_index;
        debug_file_entries_.push_back(AArch64DebugFileEntry{next_index, std::move(path)});
        return next_index;
    }
    const std::vector<AArch64DebugFileEntry> &get_debug_file_entries() const noexcept {
        return debug_file_entries_;
    }
    std::vector<AArch64DataObject> &get_data_objects() noexcept { return data_objects_; }
    const std::vector<AArch64DataObject> &get_data_objects() const noexcept {
        return data_objects_;
    }
    AArch64DataObject &append_data_object(AArch64SectionKind section_kind,
                                          std::string symbol_name,
                                          bool is_global_symbol,
                                          std::size_t align_log2) {
        data_objects_.emplace_back(section_kind, std::move(symbol_name),
                                   is_global_symbol, align_log2);
        return data_objects_.back();
    }
    AArch64Symbol &record_symbol(std::string name, AArch64SymbolKind kind,
                                 std::optional<AArch64SectionKind> section_kind,
                                 bool is_defined, bool is_global,
                                 bool is_referenced) {
        auto [it, inserted] = symbols_.emplace(name, AArch64Symbol(name));
        if (inserted || kind != AArch64SymbolKind::Object) {
            it->second.set_kind(kind);
        }
        if (section_kind.has_value()) {
            it->second.set_section_kind(*section_kind);
        }
        if (is_defined) {
            it->second.set_is_defined(true);
        }
        if (is_global) {
            it->second.set_binding(AArch64SymbolBinding::Global);
        } else if (is_defined) {
            it->second.set_binding(AArch64SymbolBinding::Local);
        }
        if (is_referenced) {
            it->second.mark_referenced();
        }
        return it->second;
    }
    const std::map<std::string, AArch64Symbol> &get_symbols() const noexcept {
        return symbols_;
    }
};

class AArch64MachineModule {
  private:
    std::vector<AArch64MachineFunction> functions_;

  public:
    std::vector<AArch64MachineFunction> &get_functions() noexcept {
        return functions_;
    }
    const std::vector<AArch64MachineFunction> &get_functions() const noexcept {
        return functions_;
    }
    AArch64MachineFunction &append_function(std::string name, bool is_global_symbol,
                                            std::string epilogue_label) {
        functions_.emplace_back(std::move(name), is_global_symbol,
                                std::move(epilogue_label));
        return functions_.back();
    }
};

} // namespace sysycc
