#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_SOURCE="${SCRIPT_DIR}/ir_core_inliner_unique_block_labels.sy"
IR_OUTPUT_FILE="${BUILD_DIR}/intermediate_results/ir_core_inliner_unique_block_labels.ll"
OBJECT_OUTPUT_FILE="${BUILD_DIR}/intermediate_results/ir_core_inliner_unique_block_labels.o"
COMPILER_BIN="${BUILD_DIR}/compiler"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${COMPILER_BIN}" "${TEST_SOURCE}" --dump-ir >/dev/null

if [[ ! -f "${IR_OUTPUT_FILE}" ]]; then
    echo "missing IR dump: ${IR_OUTPUT_FILE}" >&2
    exit 1
fi

python3 - "${IR_OUTPUT_FILE}" <<'PY'
from collections import Counter
from pathlib import Path
import sys

ir_path = Path(sys.argv[1])
text = ir_path.read_text()
current_function = None
labels = []

def flush(function_name, block_labels):
    if function_name is None:
        return
    duplicates = [name for name, count in Counter(block_labels).items() if count > 1]
    if duplicates:
        raise SystemExit(
            f"duplicate block labels in {function_name}: {', '.join(sorted(duplicates))}"
        )

for line in text.splitlines():
    if line.startswith("define "):
        flush(current_function, labels)
        current_function = line
        labels = []
        continue
    if current_function is None:
        continue
    if line == "}":
        flush(current_function, labels)
        current_function = None
        labels = []
        continue
    if line and not line.startswith(" ") and line.endswith(":"):
        labels.append(line[:-1])

flush(current_function, labels)
PY

perl -e 'alarm shift @ARGV; exec @ARGV' 20 \
    clang -c -x ir "${IR_OUTPUT_FILE}" -o "${OBJECT_OUTPUT_FILE}"

if [[ ! -f "${OBJECT_OUTPUT_FILE}" || ! -s "${OBJECT_OUTPUT_FILE}" ]]; then
    echo "missing object output: ${OBJECT_OUTPUT_FILE}" >&2
    exit 1
fi

echo "verified: inlined LLVM IR keeps unique block labels and compiles with clang"
