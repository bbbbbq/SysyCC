#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
FUZZ_SCRIPT="${PROJECT_ROOT}/tests/fuzz/run_csmith_cases.sh"

TMP_ROOT="$(mktemp -d)"
cleanup() {
    rm -rf "${TMP_ROOT}"
}
trap cleanup EXIT

mkdir -p \
    "${TMP_ROOT}/001" \
    "${TMP_ROOT}/200" \
    "${TMP_ROOT}/0201" \
    "${TMP_ROOT}/0202" \
    "${TMP_ROOT}/0999" \
    "${TMP_ROOT}/1000" \
    "${TMP_ROOT}/1200"

all_case_ids="$(SYSYCC_FUZZ_CASE_ROOT="${TMP_ROOT}" \
    bash "${FUZZ_SCRIPT}" --list-case-ids-internal all)"
expected_all_case_ids=$'001\n200\n0201\n0202\n0999\n1000\n1200'
if [[ "${all_case_ids}" != "${expected_all_case_ids}" ]]; then
    echo "unexpected all-case discovery order" >&2
    printf 'expected:\n%s\n' "${expected_all_case_ids}" >&2
    printf 'actual:\n%s\n' "${all_case_ids}" >&2
    exit 1
fi

resolved_case_ids="$(SYSYCC_FUZZ_CASE_ROOT="${TMP_ROOT}" \
    bash "${FUZZ_SCRIPT}" --list-case-ids-internal 1 200 201 202 999 1000 1200)"
expected_resolved_case_ids=$'001\n200\n0201\n0202\n0999\n1000\n1200'
if [[ "${resolved_case_ids}" != "${expected_resolved_case_ids}" ]]; then
    echo "unexpected explicit case-id resolution" >&2
    printf 'expected:\n%s\n' "${expected_resolved_case_ids}" >&2
    printf 'actual:\n%s\n' "${resolved_case_ids}" >&2
    exit 1
fi

echo "verified: fuzz runner discovers and resolves arbitrary-width numeric case ids"
