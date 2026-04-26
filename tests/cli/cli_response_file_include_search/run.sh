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
mkdir -p \
    "${CASE_BUILD_DIR}/quote dir" \
    "${CASE_BUILD_DIR}/include" \
    "${CASE_BUILD_DIR}/after" \
    "${CASE_BUILD_DIR}/sys root/usr/include" \
    "${CASE_BUILD_DIR}/sdk root/usr/include"

cat >"${CASE_BUILD_DIR}/quote dir/quote_only.h" <<'EOF'
#define QUOTE_ONLY_VALUE 3
EOF

cat >"${CASE_BUILD_DIR}/include/normal_header.h" <<'EOF'
#define NORMAL_HEADER_VALUE 5
EOF

cat >"${CASE_BUILD_DIR}/after/late_header.h" <<'EOF'
#define LATE_HEADER_VALUE 7
EOF

cat >"${CASE_BUILD_DIR}/sys root/usr/include/sysroot_header.h" <<'EOF'
#define SYSROOT_HEADER_VALUE 11
EOF

cat >"${CASE_BUILD_DIR}/sdk root/usr/include/sdk_header.h" <<'EOF'
#define SDK_HEADER_VALUE 13
EOF

cat >"${CASE_BUILD_DIR}/response_source.c" <<'EOF'
#if __has_include(<quote_only.h>)
#error "angle includes must not search -iquote directories"
#endif

#include "quote_only.h"
#include <normal_header.h>
#include <late_header.h>
#include <sysroot_header.h>
#include <sdk_header.h>

int main(void) {
    return QUOTE_ONLY_VALUE + NORMAL_HEADER_VALUE + LATE_HEADER_VALUE +
           SYSROOT_HEADER_VALUE + SDK_HEADER_VALUE - PROJECT_TOTAL;
}
EOF

OUT_FILE="${CASE_BUILD_DIR}/out file.ll"
NESTED_RSP="${CASE_BUILD_DIR}/nested args.rsp"
OUTER_RSP="${CASE_BUILD_DIR}/outer.rsp"

cat >"${NESTED_RSP}" <<EOF
-iquote "${CASE_BUILD_DIR}/quote dir"
-I "${CASE_BUILD_DIR}/include"
-idirafter "${CASE_BUILD_DIR}/after"
--sysroot="${CASE_BUILD_DIR}/sys root"
-isysroot "${CASE_BUILD_DIR}/sdk root"
-DPROJECT_TOTAL=39
-Wno-unused-command-line-argument
-fno-strict-aliasing
EOF

cat >"${OUTER_RSP}" <<EOF
-S
-emit-llvm
@"${NESTED_RSP}"
-o "${OUT_FILE}"
"${CASE_BUILD_DIR}/response_source.c"
EOF

"${BUILD_DIR}/compiler" @"${OUTER_RSP}"

assert_file_nonempty "${OUT_FILE}"
grep -Fq "define i32 @main()" "${OUT_FILE}"

echo "verified: @response files, -iquote, -idirafter, --sysroot and -isysroot participate in driver include search"
