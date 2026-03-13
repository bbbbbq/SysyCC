#ifndef SYSYCC_ERROR_DEF_H
#define SYSYCC_ERROR_DEF_H

#include <stdexcept>
#include <string>
#include <utility>

namespace sysycc {

struct SourceLocation {
    int line = 0;
    int column = 0;
};

class LexerError : public std::runtime_error {
public:
    LexerError(SourceLocation location, std::string message)
        : std::runtime_error(std::move(message)),
          location_(location) {}

    SourceLocation location() const noexcept {
        return location_;
    }

private:
    SourceLocation location_;
};

}  // namespace sysycc

#endif
