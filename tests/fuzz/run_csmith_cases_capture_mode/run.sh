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

mkdir -p "${TMP_ROOT}/001"
cp "${PROJECT_ROOT}/tests/fuzz/001/fuzz_001.c" "${TMP_ROOT}/001/fuzz_001.c"
if [[ -f "${PROJECT_ROOT}/tests/fuzz/001/fuzz_001.input.txt" ]]; then
    cp "${PROJECT_ROOT}/tests/fuzz/001/fuzz_001.input.txt" \
        "${TMP_ROOT}/001/fuzz_001.input.txt"
fi

SYSYCC_FUZZ_CASE_ROOT="${TMP_ROOT}" \
    bash "${FUZZ_SCRIPT}" 001 >/dev/null

for artifact in \
    "${TMP_ROOT}/001/fuzz_001.preprocessed.sy" \
    "${TMP_ROOT}/001/fuzz_001.tokens.txt" \
    "${TMP_ROOT}/001/fuzz_001.parse.txt" \
    "${TMP_ROOT}/001/fuzz_001.ast.txt" \
    "${TMP_ROOT}/001/fuzz_001.ll"; do
    if [[ -e "${artifact}" ]]; then
        echo "unexpected default intermediate artifact: ${artifact}" >&2
        exit 1
    fi
done

if ! grep -Eq '^\| 001 \| [A-Z_]+ \|' "${TMP_ROOT}/result.md"; then
    echo "expected a result row for default fuzz capture mode" >&2
    exit 1
fi

SYSYCC_FUZZ_CASE_ROOT="${TMP_ROOT}" \
SYSYCC_FUZZ_CAPTURE_INTERMEDIATES=full \
    bash "${FUZZ_SCRIPT}" 001 >/dev/null

for artifact in \
    "${TMP_ROOT}/001/fuzz_001.preprocessed.sy" \
    "${TMP_ROOT}/001/fuzz_001.tokens.txt" \
    "${TMP_ROOT}/001/fuzz_001.parse.txt" \
    "${TMP_ROOT}/001/fuzz_001.ast.txt"; do
    if [[ ! -f "${artifact}" ]]; then
        echo "missing full-capture intermediate artifact: ${artifact}" >&2
        exit 1
    fi
done

if ! grep -Eq '^\| 001 \| [A-Z_]+ \|' "${TMP_ROOT}/result.md"; then
    echo "expected a result row for full fuzz capture mode" >&2
    exit 1
fi

echo "verified: fuzz runner skips SysyCC dump artifacts by default and preserves them only in full capture mode"
