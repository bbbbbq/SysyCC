#include <cassert>
#include <cstdio>
#include <string_view>

#include "frontend/dialects/lexer_keyword_registry.hpp"
#include "frontend/lexer/lexer.hpp"
#include "frontend/parser/parser.tab.h"

extern int yylex(YYSTYPE *yylval_param, yyscan_t yyscanner);
extern int yylex_init_extra(void *user_defined, yyscan_t *scanner);
extern int yylex_destroy(yyscan_t yyscanner);
extern void yyset_in(FILE *input_file, yyscan_t yyscanner);

using namespace sysycc;

int main(int argc, char **argv) {
    assert(argc == 2);

    LexerKeywordRegistry keyword_registry;
    keyword_registry.add_keyword("shared_keyword", TokenKind::KwInt);

    LexerState lexer_state;
    lexer_state.reset();
    lexer_state.set_keyword_registry(&keyword_registry);
    lexer_state.set_emit_parse_nodes(false);

    std::FILE *input = std::fopen(argv[1], "r");
    assert(input != nullptr);

    yyscan_t scanner = nullptr;
    assert(yylex_init_extra(&lexer_state, &scanner) == 0);
    yyset_in(input, scanner);

    YYSTYPE semantic_value = {};
    assert(yylex(&semantic_value, scanner) == INT);
    assert(yylex(&semantic_value, scanner) == IDENTIFIER);
    assert(yylex(&semantic_value, scanner) == 0);

    yylex_destroy(scanner);
    std::fclose(input);
    return 0;
}
