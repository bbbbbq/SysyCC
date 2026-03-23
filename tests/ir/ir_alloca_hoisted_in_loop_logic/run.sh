#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/ir_alloca_hoisted_in_loop_logic.sy"
TEST_NAME="$(basename "${SCRIPT_DIR}")"
IR_FILE="${BUILD_DIR}/intermediate_results/${TEST_NAME}.ll"

source "${PROJECT_ROOT}/tests/test_helpers.sh"

build_project "${PROJECT_ROOT}" "${BUILD_DIR}"

"${BUILD_DIR}/SysyCC" "${INPUT_FILE}" --dump-ir

assert_file_nonempty "${IR_FILE}"
python3 - "${IR_FILE}" <<'PY'
import pathlib
import sys

ir_path = pathlib.Path(sys.argv[1])
lines = ir_path.read_text(encoding="utf-8").splitlines()
inside = False
current_label = None
entry_allocas = []
bad_allocas = []

for line in lines:
    stripped = line.strip()
    if not inside:
        if stripped.startswith("define ") and "@guarded_mul(" in stripped:
            inside = True
        continue

    if stripped == "}":
        break

    if stripped.endswith(":"):
        current_label = stripped[:-1]
        continue

    if " = alloca " in stripped:
        if current_label == "entry":
            entry_allocas.append(stripped)
        else:
            bad_allocas.append((current_label, stripped))

if not entry_allocas:
    raise SystemExit("expected guarded_mul to allocate stack slots in entry block")

if bad_allocas:
    formatted = ", ".join(f"{label}: {line}" for label, line in bad_allocas)
    raise SystemExit(f"found non-entry alloca instructions: {formatted}")
PY

echo "verified: LLVM lowering hoists guarded_mul allocas into the entry block"
