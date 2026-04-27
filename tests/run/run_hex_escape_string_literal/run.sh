#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
OUTPUT_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${OUTPUT_DIR}/hex_escape_string_literal.c"
PROGRAM_BINARY="${OUTPUT_DIR}/hex_escape_string_literal"
EXPECTED_OUTPUT="${OUTPUT_DIR}/expected.out"
ACTUAL_OUTPUT="${OUTPUT_DIR}/actual.out"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

cat > "${SOURCE_FILE}" <<'EOF'
int printf(const char *, ...);

int main(void) {
    const unsigned char *signature = (const unsigned char *)"\x1bLua";
    const unsigned char *data = (const unsigned char *)"\031\223\r\n\032\n";
    printf("%u %u %u %u %u %u %u %u %u %u\n",
           signature[0], signature[1], signature[2], signature[3],
           data[0], data[1], data[2], data[3], data[4], data[5]);
    return 0;
}
EOF

cat > "${EXPECTED_OUTPUT}" <<'EOF'
27 76 117 97 25 147 13 10 26 10
EOF

"${BUILD_DIR}/compiler" "${SOURCE_FILE}" -o "${PROGRAM_BINARY}"
"${PROGRAM_BINARY}" > "${ACTUAL_OUTPUT}"
diff -u "${EXPECTED_OUTPUT}" "${ACTUAL_OUTPUT}"

echo "verified: C hex and octal string escapes preserve Lua chunk header bytes"
