#ifndef SYSYCC_LEXER_DRIVER_H
#define SYSYCC_LEXER_DRIVER_H

#include <string>
#include "lexer_scanner.hpp"
namespace sysycc {



class LexerDriver
{
private:
    std::string intput_file_path;
public:
    LexerDriver(const std::string& intput_file_path) : intput_file_path(intput_file_path) {}

    void RunLexer();
};

};
#endif
