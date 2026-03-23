#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CASE_ROOT="${SYSYCC_FUZZ_CASE_ROOT:-${SCRIPT_DIR}}"
CSMITH_BIN="${SYSYCC_CSMITH_BIN:-${PROJECT_ROOT}/tools/csmith/build/src/csmith}"
CSMITH_RUNTIME_DIR="${SYSYCC_CSMITH_RUNTIME_DIR:-${PROJECT_ROOT}/tools/csmith/runtime}"
CSMITH_BUILD_RUNTIME_DIR="${SYSYCC_CSMITH_BUILD_RUNTIME_DIR:-${PROJECT_ROOT}/tools/csmith/build/runtime}"
CLANG_BIN="${SYSYCC_CLANG_BIN:-clang}"
GENERATE_ONLY=0

get_existing_case_state() {
    local max_case_number=0
    local case_id_width=3

    if [[ ! -d "${CASE_ROOT}" ]]; then
        printf '0 3\n'
        return 0
    fi

    while IFS= read -r case_dir; do
        local case_name
        case_name="$(basename "${case_dir}")"
        if [[ ! "${case_name}" =~ ^[0-9]+$ ]]; then
            continue
        fi

        local case_number=$((10#${case_name}))
        if [[ "${case_number}" -gt "${max_case_number}" ]]; then
            max_case_number="${case_number}"
        fi
        if [[ "${#case_name}" -gt "${case_id_width}" ]]; then
            case_id_width="${#case_name}"
        fi
    done < <(find "${CASE_ROOT}" -mindepth 1 -maxdepth 1 -type d | sort)

    printf '%s %s\n' "${max_case_number}" "${case_id_width}"
}

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
    if ! command -v "${CLANG_BIN}" >/dev/null 2>&1; then
        echo "missing required tool: ${CLANG_BIN}" >&2
        exit 1
    fi

    if [[ ! -d "${CSMITH_RUNTIME_DIR}" ]] || [[ ! -d "${CSMITH_BUILD_RUNTIME_DIR}" ]]; then
        echo "missing csmith runtime include directories" >&2
        exit 1
    fi
fi

read -r max_existing_case_number case_id_width < <(get_existing_case_state)
first_case_number=$((max_existing_case_number + 1))
last_case_number=$((max_existing_case_number + COUNT))
if [[ "${#last_case_number}" -gt "${case_id_width}" ]]; then
    case_id_width="${#last_case_number}"
fi

for ((case_number = first_case_number; case_number <= last_case_number; ++case_number)); do
    case_id="$(printf "%0${case_id_width}d" "${case_number}")"
    case_dir="${CASE_ROOT}/${case_id}"
    case_file="${case_dir}/fuzz_${case_id}.c"
    case_binary="${case_dir}/fuzz_${case_id}.out"

    mkdir -p "${case_dir}"
    "${CSMITH_BIN}" --output "${case_file}"
    echo "generated ${case_file}"

    if [[ "${GENERATE_ONLY}" -eq 0 ]]; then
        "${CLANG_BIN}" -w -O0 \
            -I "${CSMITH_RUNTIME_DIR}" \
            -I "${CSMITH_BUILD_RUNTIME_DIR}" \
            "${case_file}" \
            -o "${case_binary}"
        echo "built ${case_binary}"
    elif [[ -f "${case_binary}" ]]; then
        rm -f "${case_binary}"
    fi
done
