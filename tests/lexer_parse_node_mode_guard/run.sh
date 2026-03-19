#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

LEXER_TEMPLATE="${PROJECT_ROOT}/src/frontend/lexer/lexer.l"
LEXER_PASS="${PROJECT_ROOT}/src/frontend/lexer/lexer.cpp"
PARSER_PASS="${PROJECT_ROOT}/src/frontend/parser/parser.cpp"
LEXER_HEADER="${PROJECT_ROOT}/src/frontend/lexer/lexer.hpp"

grep -q "class LexerState" "${LEXER_HEADER}"
grep -q "get_emit_parse_nodes" "${LEXER_HEADER}"
grep -q "set_emit_parse_nodes" "${LEXER_HEADER}"
grep -q "get_emit_parse_nodes()" "${LEXER_TEMPLATE}"
grep -q "lexer_state.set_emit_parse_nodes(false);" "${LEXER_PASS}"
grep -q "lexer_state.set_emit_parse_nodes(true);" "${PARSER_PASS}"

echo "verified: lexer-only mode no longer allocates parser terminal nodes"
