#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE_A="${SCRIPT_DIR}/first.sy"
INPUT_FILE_B="${SCRIPT_DIR}/second.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -fsyntax-only \
    "${INPUT_FILE_A}" \
    "${INPUT_FILE_B}" \
    "multiple input files are not yet supported"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -c \
    "${INPUT_FILE_A}" \
    "${INPUT_FILE_B}" \
    "multiple source inputs with -c are not supported yet; compile sources separately"

echo "verified: gcc-like CLI rejects unsupported non-linking multisource modes with driver errors"
