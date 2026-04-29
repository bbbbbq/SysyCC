#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_BUILD_DIR="${SCRIPT_DIR}/build"
TEST_BINARY="${TEST_BUILD_DIR}/ir_core_simple_loop_unswitch"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_simple_loop_unswitch.cpp"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

mkdir -p "${TEST_BUILD_DIR}"

LINK_INPUTS=()
while IFS= read -r -d '' link_input; do
    LINK_INPUTS+=("${link_input}")
done < <(collect_sysycc_cpp_test_link_inputs "${BUILD_DIR}")

RPATH_ARGS=()
while IFS= read -r -d '' rpath_arg; do
    RPATH_ARGS+=("${rpath_arg}")
done < <(collect_sysycc_cpp_test_rpath_args "${BUILD_DIR}")

clang++ -std=c++17 -I"${PROJECT_ROOT}/src" \
    "${RPATH_ARGS[@]}" \
    "${TEST_SOURCE}" \
    "${LINK_INPUTS[@]}" \
    -o "${TEST_BINARY}"

"${TEST_BINARY}"

echo "verified: SimpleLoopUnswitch hoists invariant header and loop-body condition slices out of loops"
