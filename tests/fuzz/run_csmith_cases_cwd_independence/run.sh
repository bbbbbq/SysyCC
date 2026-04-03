#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
FUZZ_DIR="${PROJECT_ROOT}/tests/fuzz"
FUZZ_SCRIPT="${FUZZ_DIR}/run_csmith_cases.sh"

TMP_ROOT="$(mktemp -d)"
cleanup() {
    rm -rf "${TMP_ROOT}"
}
trap cleanup EXIT

mkdir -p "${TMP_ROOT}/001"
cat >"${TMP_ROOT}/001/fuzz_001.c" <<'EOF'
int main(void) {
    return 0;
}
EOF
: >"${TMP_ROOT}/001/fuzz_001.input.txt"

(
    cd "${FUZZ_DIR}"
    SYSYCC_FUZZ_CASE_ROOT="${TMP_ROOT}" \
        bash "./run_csmith_cases.sh" 001 >/dev/null
)

if ! grep -Eq '^\| 001 \| MATCH \|' "${TMP_ROOT}/result.md"; then
    echo "expected cwd-independent fuzz invocation to produce MATCH" >&2
    cat "${TMP_ROOT}/result.md" >&2
    exit 1
fi

echo "verified: fuzz runner finds SysyCC IR dumps even when launched from tests/fuzz"
