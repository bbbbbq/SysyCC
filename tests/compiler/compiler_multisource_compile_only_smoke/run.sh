#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

cleanup() {
    rm -rf "${CASE_BUILD_DIR}"
}
trap cleanup EXIT

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
cleanup
mkdir -p "${CASE_BUILD_DIR}/include"

cat >"${CASE_BUILD_DIR}/include/project_config.h" <<'EOF'
#define PROJECT_OFFSET 7
EOF

cat >"${CASE_BUILD_DIR}/alpha.c" <<'EOF'
#include "project_config.h"

int alpha_value(void) {
    return PROJECT_OFFSET + 11;
}
EOF

cat >"${CASE_BUILD_DIR}/beta.c" <<'EOF'
#include "project_config.h"

int beta_value(void) {
    return PROJECT_OFFSET + 23;
}
EOF

(
    cd "${CASE_BUILD_DIR}"
    "${BUILD_DIR}/compiler" \
        -c \
        --backend=aarch64-native \
        --target=aarch64-unknown-linux-gnu \
        -MMD \
        -MP \
        -I "${CASE_BUILD_DIR}/include" \
        alpha.c \
        beta.c
)

assert_file_nonempty "${CASE_BUILD_DIR}/alpha.o"
assert_file_nonempty "${CASE_BUILD_DIR}/beta.o"
assert_file_nonempty "${CASE_BUILD_DIR}/alpha.d"
assert_file_nonempty "${CASE_BUILD_DIR}/beta.d"

READELF_TOOL="$(find_aarch64_readelf)"
"${READELF_TOOL}" -s "${CASE_BUILD_DIR}/alpha.o" | grep -q ' alpha_value$'
"${READELF_TOOL}" -s "${CASE_BUILD_DIR}/beta.o" | grep -q ' beta_value$'

grep -Fq "alpha.o:" "${CASE_BUILD_DIR}/alpha.d"
grep -Fq "alpha.c" "${CASE_BUILD_DIR}/alpha.d"
grep -Fq "project_config.h" "${CASE_BUILD_DIR}/alpha.d"
grep -Fq "beta.o:" "${CASE_BUILD_DIR}/beta.d"
grep -Fq "beta.c" "${CASE_BUILD_DIR}/beta.d"
grep -Fq "project_config.h" "${CASE_BUILD_DIR}/beta.d"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -c \
    -o "${CASE_BUILD_DIR}/combined.o" \
    "${CASE_BUILD_DIR}/alpha.c" \
    "${CASE_BUILD_DIR}/beta.c" \
    "cannot specify '-o' with '-c' and multiple source inputs"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -c \
    -MMD \
    -MF "${CASE_BUILD_DIR}/combined.d" \
    "${CASE_BUILD_DIR}/alpha.c" \
    "${CASE_BUILD_DIR}/beta.c" \
    "'-MF' with '-c' and multiple source inputs is not supported yet"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -c \
    -MMD \
    -MT custom-target \
    "${CASE_BUILD_DIR}/alpha.c" \
    "${CASE_BUILD_DIR}/beta.c" \
    "'-MT' and '-MQ' with '-c' and multiple source inputs are not supported yet"

echo "verified: multi-source -c emits one object and default depfile per source with stable unsupported -o/-MF/-MT diagnostics"
