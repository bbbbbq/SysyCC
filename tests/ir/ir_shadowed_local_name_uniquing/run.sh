#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_shadowed_local_name_uniquing.sy"
IR_FILE="${BUILD_DIR}/intermediate_results/ir_shadowed_local_name_uniquing.ll"
OBJECT_FILE="${BUILD_DIR}/ir_shadowed_local_name_uniquing.o"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-tokens --dump-parse --dump-ir

assert_file_nonempty "${IR_FILE}"
python3 - "${IR_FILE}" <<'PY'
import pathlib
import re
import sys

ir_text = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
allocas = re.findall(r'^\s+(%i\.addr[0-9]*) = alloca i32$', ir_text, flags=re.MULTILINE)
if not allocas:
    if re.search(r'^\s+ret i32 1$', ir_text, flags=re.MULTILINE) is None:
        raise SystemExit("expected either uniquified shadowed allocas or a fully folded constant return")
    raise SystemExit(0)
if len(set(allocas)) != len(allocas):
    raise SystemExit(f"found duplicate shadowed alloca names: {allocas}")
PY
clang -c "${IR_FILE}" -o "${OBJECT_FILE}"

echo "verified: llvm lowering uniquifies shadowed local stack-slot names"
