#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
OUTPUT_DIR="${SCRIPT_DIR}/build"
SOURCE_FILE="${OUTPUT_DIR}/float_negative_zero.c"
PROGRAM_BINARY="${OUTPUT_DIR}/float_negative_zero"
EXPECTED_OUTPUT="${OUTPUT_DIR}/expected.out"
ACTUAL_OUTPUT="${OUTPUT_DIR}/actual.out"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

cat > "${SOURCE_FILE}" <<'EOF'
int printf(const char *, ...);

int main(void) {
    double x = -0.0;
    double inverse = 1.0 / x;
    if (!(inverse < 0.0)) {
        return 1;
    }
    printf("%.1f %.0f\n", x, inverse);
    return 0;
}
EOF

cat > "${EXPECTED_OUTPUT}" <<'EOF'
-0.0 -inf
EOF

"${BUILD_DIR}/compiler" "${SOURCE_FILE}" -o "${PROGRAM_BINARY}"
"${PROGRAM_BINARY}" > "${ACTUAL_OUTPUT}"
diff -u "${EXPECTED_OUTPUT}" "${ACTUAL_OUTPUT}"

echo "verified: floating unary minus preserves negative zero"
