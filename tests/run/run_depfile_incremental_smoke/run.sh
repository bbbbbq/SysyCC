#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
WORK_DIR="${CASE_BUILD_DIR}/project"
TEMPLATE_DIR="${SCRIPT_DIR}/project"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

mtime_of() {
    local path="$1"
    if stat -f '%m' "${path}" >/dev/null 2>&1; then
        stat -f '%m' "${path}"
        return 0
    fi
    stat -c '%Y' "${path}"
}

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
rm -rf "${WORK_DIR}"
mkdir -p "${CASE_BUILD_DIR}"
cp -R "${TEMPLATE_DIR}" "${WORK_DIR}"

MAKE_LOG1="${CASE_BUILD_DIR}/dep-make-build-1.log"
MAKE_LOG2="${CASE_BUILD_DIR}/dep-make-build-2.log"
MAKE_LOG3="${CASE_BUILD_DIR}/dep-make-build-3.log"

make -C "${WORK_DIR}" CC="${BUILD_DIR}/compiler" >"${MAKE_LOG1}" 2>&1

ADD_OBJECT="${WORK_DIR}/dep_add.o"
MUL_OBJECT="${WORK_DIR}/dep_mul.o"
ARCHIVE_FILE="${WORK_DIR}/libdepdemo.a"
DEPFILE_FILE="${WORK_DIR}/dep_add.o.d"
PRIVATE_HEADER_FILE="${WORK_DIR}/dep_private.h"
SYSTEM_HEADER_FILE="${WORK_DIR}/sys/dep_system.h"

assert_file_nonempty "${ADD_OBJECT}"
assert_file_nonempty "${MUL_OBJECT}"
assert_file_nonempty "${ARCHIVE_FILE}"
assert_file_nonempty "${DEPFILE_FILE}"
grep -Fq "dep_private.h" "${DEPFILE_FILE}"
grep -Fq "sys/dep_system.h" "${DEPFILE_FILE}"
grep -Fq "dep_add.o:" "${DEPFILE_FILE}"
grep -Fxq "dep_private.h:" "${DEPFILE_FILE}"

ADD_MTIME_1="$(mtime_of "${ADD_OBJECT}")"
MUL_MTIME_1="$(mtime_of "${MUL_OBJECT}")"
ARCHIVE_MTIME_1="$(mtime_of "${ARCHIVE_FILE}")"

make -C "${WORK_DIR}" CC="${BUILD_DIR}/compiler" >"${MAKE_LOG2}" 2>&1

ADD_MTIME_2="$(mtime_of "${ADD_OBJECT}")"
MUL_MTIME_2="$(mtime_of "${MUL_OBJECT}")"
ARCHIVE_MTIME_2="$(mtime_of "${ARCHIVE_FILE}")"

[[ "${ADD_MTIME_1}" == "${ADD_MTIME_2}" ]]
[[ "${MUL_MTIME_1}" == "${MUL_MTIME_2}" ]]
[[ "${ARCHIVE_MTIME_1}" == "${ARCHIVE_MTIME_2}" ]]

sleep 1
cat > "${PRIVATE_HEADER_FILE}" <<'EOF'
#ifndef RUN_DEPFILE_INCREMENTAL_SMOKE_PRIVATE_H
#define RUN_DEPFILE_INCREMENTAL_SMOKE_PRIVATE_H

#define DEP_PRIVATE_OFFSET 1

#endif
EOF

make -C "${WORK_DIR}" CC="${BUILD_DIR}/compiler" >"${MAKE_LOG3}" 2>&1

ADD_MTIME_3="$(mtime_of "${ADD_OBJECT}")"
MUL_MTIME_3="$(mtime_of "${MUL_OBJECT}")"
ARCHIVE_MTIME_3="$(mtime_of "${ARCHIVE_FILE}")"

[[ "${ADD_MTIME_3}" -gt "${ADD_MTIME_2}" ]]
[[ "${MUL_MTIME_3}" == "${MUL_MTIME_2}" ]]
[[ "${ARCHIVE_MTIME_3}" -gt "${ARCHIVE_MTIME_2}" ]]
grep -Fq "dep_add.c" "${MAKE_LOG3}"
if grep -Fq "dep_mul.c" "${MAKE_LOG3}"; then
    echo "error: depfile incremental rebuild unexpectedly recompiled dep_mul.c" >&2
    cat "${MAKE_LOG3}" >&2
    exit 1
fi

echo "verified: depfile-driven incremental rebuilds stay no-op when unchanged and recompile only the affected object after a header edit"
