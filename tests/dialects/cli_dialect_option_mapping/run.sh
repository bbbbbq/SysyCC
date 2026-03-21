#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/cli_dialect_option_mapping"
TEST_SOURCE="${SCRIPT_DIR}/cli_dialect_option_mapping.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${TEST_BUILD_DIR}"

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${TEST_SOURCE}" \
    "${PROJECT_ROOT}/src/cli/cli.cpp" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: cli maps strict and optional dialect flags into compiler options"
