#pragma once

#include <cctype>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>

namespace sysycc {

inline bool aarch64_literal_has_raw_hex_bit_pattern(std::string_view text) {
    std::size_t position = 0;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        position = 1;
    }
    if (text.size() < position + 3 || text[position] != '0' ||
        (text[position + 1] != 'x' && text[position + 1] != 'X')) {
        return false;
    }
    if (text.find('p', position + 2) != std::string_view::npos ||
        text.find('P', position + 2) != std::string_view::npos) {
        return false;
    }
    for (std::size_t index = position + 2; index < text.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if (std::isxdigit(ch) || ch == 'L' || ch == 'l' || ch == 'H' || ch == 'h') {
            continue;
        }
        return false;
    }
    return true;
}

inline std::string strip_floating_literal_suffix_preserving_hex(
    std::string value_text) {
    if (aarch64_literal_has_raw_hex_bit_pattern(value_text)) {
        return value_text;
    }
    while (!value_text.empty()) {
        const char last = value_text.back();
        if (last == 'f' || last == 'F' || last == 'l' || last == 'L') {
            value_text.pop_back();
            continue;
        }
        break;
    }
    return value_text;
}

inline std::string format_fp128_words_literal(std::uint64_t low,
                                              std::uint64_t high) {
    std::ostringstream stream;
    stream << "0xL" << std::hex << std::uppercase << std::setfill('0')
           << std::setw(16) << low << std::setw(16) << high;
    return stream.str();
}

inline std::optional<std::pair<std::uint64_t, std::uint64_t>>
parse_fp128_hex_literal_words(std::string_view text) {
    std::size_t position = 0;
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        position = 1;
    }
    if (text.size() != position + 35 || text[position] != '0' ||
        (text[position + 1] != 'x' && text[position + 1] != 'X') ||
        (text[position + 2] != 'L' && text[position + 2] != 'l')) {
        return std::nullopt;
    }
    const std::string payload(text.substr(position + 3));
    try {
        const std::uint64_t low = static_cast<std::uint64_t>(
            std::stoull(payload.substr(0, 16), nullptr, 16));
        const std::uint64_t high = static_cast<std::uint64_t>(
            std::stoull(payload.substr(16, 16), nullptr, 16));
        return std::pair<std::uint64_t, std::uint64_t>{low, high};
    } catch (...) {
        return std::nullopt;
    }
}

inline std::optional<std::pair<std::uint64_t, std::uint64_t>>
encode_fp128_literal_words(std::string_view text) {
    if (const auto direct = parse_fp128_hex_literal_words(text);
        direct.has_value()) {
        return direct;
    }

    llvm::APFloat value(llvm::APFloat::IEEEquad());
    auto status = value.convertFromString(llvm::StringRef(text),
                                          llvm::APFloat::rmNearestTiesToEven);
    if (!status) {
        llvm::consumeError(status.takeError());
        return std::nullopt;
    }
    const llvm::APInt bits = value.bitcastToAPInt();
    return std::pair<std::uint64_t, std::uint64_t>{
        bits.extractBitsAsZExtValue(64, 0),
        bits.extractBitsAsZExtValue(64, 64)};
}

inline std::optional<std::string> canonicalize_fp128_literal_text(
    std::string_view text) {
    const auto words = encode_fp128_literal_words(text);
    if (!words.has_value()) {
        return std::nullopt;
    }
    return format_fp128_words_literal(words->first, words->second);
}

inline std::optional<std::vector<std::uint8_t>>
encode_fp128_literal_bytes(std::string_view text) {
    const auto words = encode_fp128_literal_words(text);
    if (!words.has_value()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(16, 0);
    for (unsigned index = 0; index < 8; ++index) {
        bytes[index] =
            static_cast<std::uint8_t>((words->first >> (index * 8U)) & 0xffU);
        bytes[index + 8] =
            static_cast<std::uint8_t>((words->second >> (index * 8U)) & 0xffU);
    }
    return bytes;
}

} // namespace sysycc
