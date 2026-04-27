#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
MAIN_SOURCE="${CASE_BUILD_DIR}/${TEST_NAME}.c"
HELPER_SOURCE="${CASE_BUILD_DIR}/${TEST_NAME}_helper.c"
HELPER_OBJECT="${CASE_BUILD_DIR}/${TEST_NAME}_helper.o"
PROGRAM_BINARY="${CASE_BUILD_DIR}/${TEST_NAME}.bin"
PROGRAM_OUTPUT="${CASE_BUILD_DIR}/${TEST_NAME}.out"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${CASE_BUILD_DIR}"

cat >"${MAIN_SOURCE}" <<'EOF'
#include <stdio.h>

void fill(unsigned long *p);

int main(void) {
    unsigned long len;
    unsigned long total = 0;
    int i = 0;

    while (i < 3) {
        len = 0;
        fill(&len);
        total += len;
        i = i + 1;
    }

    printf("%lu\n", total);
    return total == 9 ? 0 : 1;
}
EOF

cat >"${HELPER_SOURCE}" <<'EOF'
void fill(unsigned long *p) {
    *p = 3;
}
EOF

cc -c "${HELPER_SOURCE}" -o "${HELPER_OBJECT}"
"${BUILD_DIR}/compiler" -O2 "${MAIN_SOURCE}" "${HELPER_OBJECT}" \
    -o "${PROGRAM_BINARY}"
"${PROGRAM_BINARY}" >"${PROGRAM_OUTPUT}"

if [[ "$(cat "${PROGRAM_OUTPUT}")" != "9" ]]; then
    echo "error: expected loop out-parameter writes to sum to 9" >&2
    cat "${PROGRAM_OUTPUT}" >&2
    exit 1
fi

echo "verified: loop memory promotion preserves external out-parameter writes"
