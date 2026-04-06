#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_linking_unsupported.sy"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    "${INPUT_FILE}" \
    "linking is not supported yet; use -E, -fsyntax-only, -S, or -S -emit-llvm"

echo "verified: bare public driver invocations fail with explicit linking guidance"
