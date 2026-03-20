#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CSMITH_BIN="${PROJECT_ROOT}/tools/csmith/build/src/csmith"
CSMITH_RUNTIME_DIR="${PROJECT_ROOT}/tools/csmith/runtime"
CSMITH_BUILD_RUNTIME_DIR="${PROJECT_ROOT}/tools/csmith/build/runtime"
GENERATE_ONLY=0

if [[ $# -eq 2 ]] && [[ "$1" == "--generate-only" ]]; then
    GENERATE_ONLY=1
    COUNT="$2"
elif [[ $# -eq 1 ]]; then
    COUNT="$1"
else
    echo "usage: $0 [--generate-only] <count>" >&2
    exit 1
fi

if ! [[ "${COUNT}" =~ ^[0-9]+$ ]] || [[ "${COUNT}" -le 0 ]]; then
    echo "count must be a positive integer" >&2
    exit 1
fi

if [[ ! -x "${CSMITH_BIN}" ]]; then
    echo "missing csmith binary: ${CSMITH_BIN}" >&2
    echo "build it first with: cmake -S tools/csmith -B tools/csmith/build && cmake --build tools/csmith/build" >&2
    exit 1
fi

if [[ "${GENERATE_ONLY}" -eq 0 ]]; then
    if ! command -v clang >/dev/null 2>&1; then
        echo "missing required tool: clang" >&2
        exit 1
    fi

    if [[ ! -d "${CSMITH_RUNTIME_DIR}" ]] || [[ ! -d "${CSMITH_BUILD_RUNTIME_DIR}" ]]; then
        echo "missing csmith runtime include directories" >&2
        exit 1
    fi
fi

for ((index = 1; index <= COUNT; ++index)); do
    case_id="$(printf "%03d" "${index}")"
    case_dir="${SCRIPT_DIR}/${case_id}"
    case_file="${case_dir}/fuzz_${case_id}.c"
    case_binary="${case_dir}/fuzz_${case_id}.out"

    mkdir -p "${case_dir}"
    "${CSMITH_BIN}" --output "${case_file}"
    echo "generated ${case_file}"

    if [[ "${GENERATE_ONLY}" -eq 0 ]]; then
        clang -w -O0 \
            -I "${CSMITH_RUNTIME_DIR}" \
            -I "${CSMITH_BUILD_RUNTIME_DIR}" \
            "${case_file}" \
            -o "${case_binary}"
        echo "built ${case_binary}"
    elif [[ -f "${case_binary}" ]]; then
        rm -f "${case_binary}"
    fi
done
