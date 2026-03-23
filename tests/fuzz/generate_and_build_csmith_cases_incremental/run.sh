#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
FUZZ_SCRIPT="${PROJECT_ROOT}/tests/fuzz/generate_and_build_csmith_cases.sh"
FAKE_CSMITH_BIN="${SCRIPT_DIR}/fake_csmith.sh"

TMP_ROOT="$(mktemp -d)"
cleanup() {
    rm -rf "${TMP_ROOT}"
}
trap cleanup EXIT

mkdir -p "${TMP_ROOT}/005"
printf '%s\n' 'existing case' > "${TMP_ROOT}/005/fuzz_005.c"

SYSYCC_FUZZ_CASE_ROOT="${TMP_ROOT}" \
SYSYCC_CSMITH_BIN="${FAKE_CSMITH_BIN}" \
bash "${FUZZ_SCRIPT}" 10

if [[ -d "${TMP_ROOT}/001" ]]; then
    echo "error: incremental generation unexpectedly restarted from 001" >&2
    exit 1
fi

if [[ "$(<"${TMP_ROOT}/005/fuzz_005.c")" != "existing case" ]]; then
    echo "error: existing fuzz case was modified" >&2
    exit 1
fi

for case_number in $(seq 6 15); do
    case_id="$(printf '%03d' "${case_number}")"
    case_dir="${TMP_ROOT}/${case_id}"
    case_file="${case_dir}/fuzz_${case_id}.c"
    case_binary="${case_dir}/fuzz_${case_id}.out"

    [[ -d "${case_dir}" ]]
    [[ -f "${case_file}" ]]
    [[ -x "${case_binary}" ]]
done

"${TMP_ROOT}/006/fuzz_006.out"

echo "verified: fuzz generation appends new cases after the current maximum id"
