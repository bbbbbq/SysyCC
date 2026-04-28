#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
HELPER_SOURCE="${CASE_BUILD_DIR}/helper.c"
HELPER_OBJECT="${CASE_BUILD_DIR}/helper.o"
HELPER_PIC_OBJECT="${CASE_BUILD_DIR}/helper.pic.o"
HELPER_ARCHIVE="${CASE_BUILD_DIR}/libhelper.a"
HELPER_VERSIONED_SHARED="${CASE_BUILD_DIR}/libhelper.so.1.0.git"
MAIN_SOURCE="${CASE_BUILD_DIR}/main.c"
MAIN_OBJECT="${CASE_BUILD_DIR}/main.o"
OUTPUT_FILE="${CASE_BUILD_DIR}/cli_link_passthrough_host_objects.bin"
OUTPUT_SHARED_FILE="${CASE_BUILD_DIR}/cli_link_passthrough_versioned_shared.bin"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

cat > "${HELPER_SOURCE}" <<'EOF'
int helper_add(int a, int b) { return a + b; }
EOF

cat > "${MAIN_SOURCE}" <<'EOF'
int helper_add(int, int);
int main(void) { return helper_add(20, 22) == 42 ? 0 : 1; }
EOF

/usr/bin/cc -c "${HELPER_SOURCE}" -o "${HELPER_OBJECT}"
/usr/bin/ar rcs "${HELPER_ARCHIVE}" "${HELPER_OBJECT}"
/usr/bin/cc -c "${MAIN_SOURCE}" -o "${MAIN_OBJECT}"

"${BUILD_DIR}/compiler" \
    "${MAIN_OBJECT}" \
    -L"${CASE_BUILD_DIR}" \
    -lhelper \
    -pthread \
    -Wl,-v \
    -o "${OUTPUT_FILE}"

assert_file_nonempty "${OUTPUT_FILE}"

set +e
"${OUTPUT_FILE}"
PROGRAM_RC=$?
set -e

if [[ "${PROGRAM_RC}" -ne 0 ]]; then
    echo "error: passthrough-linked executable returned ${PROGRAM_RC}, expected 0" >&2
    exit 1
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
    /usr/bin/cc -fPIC -c "${HELPER_SOURCE}" -o "${HELPER_PIC_OBJECT}"
    /usr/bin/cc -shared "${HELPER_PIC_OBJECT}" -o "${HELPER_VERSIONED_SHARED}"

    "${BUILD_DIR}/compiler" \
        "${MAIN_OBJECT}" \
        "${HELPER_VERSIONED_SHARED}" \
        -Wl,-rpath,"${CASE_BUILD_DIR}" \
        -o "${OUTPUT_SHARED_FILE}"

    assert_file_nonempty "${OUTPUT_SHARED_FILE}"

    set +e
    LD_LIBRARY_PATH="${CASE_BUILD_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
        "${OUTPUT_SHARED_FILE}"
    SHARED_PROGRAM_RC=$?
    set -e

    if [[ "${SHARED_PROGRAM_RC}" -ne 0 ]]; then
        echo "error: versioned-shared passthrough executable returned ${SHARED_PROGRAM_RC}, expected 0" >&2
        exit 1
    fi
fi

echo "verified: -L/-l/-pthread/-Wl,... are collected and forwarded through the external link driver"
