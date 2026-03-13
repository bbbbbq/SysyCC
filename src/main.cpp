#include <iostream>
#include <string>

#include "lexer_driver.h"

namespace {

void PrintUsage() {
    std::cerr << "Usage: SysyCC lex <input.sy>" << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 3 || std::string(argv[1]) != "lex") {
        PrintUsage();
        return 1;
    }

    sysycc::LexerDriver lexer_driver(argv[2]);
    lexer_driver.RunLexer();
    return 0;
}
