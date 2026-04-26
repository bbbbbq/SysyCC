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
    -o "${SCRIPT_DIR}/build/combined.o" \
    "${INPUT_FILE_A}" \
    "${INPUT_FILE_B}" \
    "cannot specify '-o' with '-c' and multiple source inputs"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -S \
    "${INPUT_FILE_A}" \
    "${INPUT_FILE_B}" \
    "multiple input files are not yet supported"

echo "verified: gcc-like CLI rejects unsupported non-linking multisource modes while allowing bare multi-source -c elsewhere"
