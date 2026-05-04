#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${SYSYCC_BUILD_DIR:-${PROJECT_ROOT}/build}"
INPUT_FILE="${SCRIPT_DIR}/semantic_baked_sysy_runtime_builtins.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

BAKED_STANDARD="$(
    grep '^SYSYCC_SOURCE_LANGUAGE_STANDARD:STRING=' "${BUILD_DIR}/CMakeCache.txt" |
        sed 's/^SYSYCC_SOURCE_LANGUAGE_STANDARD:STRING=//' || true
)"
if [[ -z "${BAKED_STANDARD}" ]]; then
    BAKED_STANDARD="sysy22"
fi

if [[ "${BAKED_STANDARD}" == "sysy22" ]]; then
    "${BUILD_DIR}/SysyCC" --stop-after=semantic "${INPUT_FILE}" >/dev/null
    echo "verified: baked SysY22 build predeclares SysY runtime functions"
else
    assert_compiler_fails_with_message \
        "${BUILD_DIR}/SysyCC" \
        --stop-after=semantic \
        "${INPUT_FILE}" \
        "getint"
    echo "verified: baked C build does not predeclare SysY runtime functions"
fi
