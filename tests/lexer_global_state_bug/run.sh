#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

LEXER_HEADER="${PROJECT_ROOT}/src/frontend/lexer/lexer.hpp"
LEXER_TEMPLATE="${PROJECT_ROOT}/src/frontend/lexer/lexer.l"
LEXER_PASS="${PROJECT_ROOT}/src/frontend/lexer/lexer.cpp"
PARSER_PASS="${PROJECT_ROOT}/src/frontend/parser/parser.cpp"

grep -q "class LexerState" "${LEXER_HEADER}"
grep -q "%option reentrant" "${LEXER_TEMPLATE}"
grep -q "%option bison-bridge" "${LEXER_TEMPLATE}"
grep -q "yylex_init_extra" "${LEXER_PASS}"
grep -q "yylex_init_extra" "${PARSER_PASS}"
grep -q "yyset_in" "${LEXER_PASS}"
grep -q "yyset_in" "${PARSER_PASS}"

if grep -q "static int g_line = 1;" "${LEXER_TEMPLATE}"; then
    echo "error: lexer still keeps global position state" >&2
    exit 1
fi

echo "verified: lexer now uses per-scanner reentrant state"
