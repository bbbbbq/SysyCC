#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
CASE_BUILD_DIR="${SCRIPT_DIR}/build"
INPUT_FILE="${SCRIPT_DIR}/cli_depfile_requires_mmd.c"
DEP_FILE="${CASE_BUILD_DIR}/cli_depfile_requires_mmd.d"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"
mkdir -p "${CASE_BUILD_DIR}"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -c \
    -MF "${DEP_FILE}" \
    "${INPUT_FILE}" \
    "'-MF' requires '-MD' or '-MMD'"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -c \
    -MT custom-target \
    "${INPUT_FILE}" \
    "'-MT' and '-MQ' require '-MD' or '-MMD'"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -c \
    -MQ quoted-target \
    "${INPUT_FILE}" \
    "'-MT' and '-MQ' require '-MD' or '-MMD'"

assert_compiler_fails_with_message \
    "${BUILD_DIR}/compiler" \
    -c \
    -MP \
    "${INPUT_FILE}" \
    "'-MP' requires '-MD' or '-MMD'"

echo "verified: depfile companion flags now fail fast unless -MD or -MMD is also present"
