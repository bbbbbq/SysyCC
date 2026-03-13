#include "lexer_driver.h"
#include "lexer_scanner.hpp"
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <iostream>
extern void reset_lexer_state(void);

void sysycc::LexerDriver::RunLexer() {
    FILE* file = std::fopen(intput_file_path.c_str(), "r");
    if (!file) {
        std::cerr << "Failed to open file: " << std::strerror(errno) << std::endl;
        return;
    }
    reset_lexer_state();
    yyset_in(file);
    int result = yylex();
    if (result != 0) {
        std::cerr << "Lexer error occurred." << std::endl;
    }
    std::fclose(file);
}