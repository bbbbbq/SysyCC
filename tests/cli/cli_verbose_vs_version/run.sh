#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_verbose_vs_version.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

VERSION_OUTPUT="$("${BUILD_DIR}/SysyCC" --version 2>&1)"
grep -Fq "sysycc version 0.1.0" <<<"${VERSION_OUTPUT}"

set +e
VERBOSE_OUTPUT="$("${BUILD_DIR}/SysyCC" -v -fsyntax-only "${INPUT_FILE}" 2>&1)"
VERBOSE_RC=$?
set -e

if [[ ${VERBOSE_RC} -ne 0 ]]; then
    echo "error: verbose syntax-only invocation unexpectedly failed" >&2
    echo "${VERBOSE_OUTPUT}" >&2
    exit 1
fi

grep -Fq "sysycc version 0.1.0" <<<"${VERBOSE_OUTPUT}"
grep -Fq "driver action: syntax-only" <<<"${VERBOSE_OUTPUT}"
grep -Fq "language mode: sysy" <<<"${VERBOSE_OUTPUT}"

echo "verified: -v reports driver configuration while --version exits immediately"
