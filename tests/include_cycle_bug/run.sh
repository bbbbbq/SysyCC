#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
INPUT_FILE="${SCRIPT_DIR}/include_cycle_bug.sy"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

python3 - <<'PY'
import pathlib
import subprocess
import sys

project_root = pathlib.Path("/Users/caojunze424/code/SysyCC")
cmd = [str(project_root / "build" / "SysyCC"),
       str(project_root / "tests" / "include_cycle_bug" / "include_cycle_bug.sy"),
       "--dump-tokens",
       "--dump-parse"]

try:
    completed = subprocess.run(
        cmd,
        cwd=project_root,
        capture_output=True,
        text=True,
        timeout=3,
        check=False,
    )
except subprocess.TimeoutExpired:
    print("error: include cycle still causes non-terminating preprocess recursion", file=sys.stderr)
    sys.exit(1)

if completed.returncode == 0:
    print("error: include cycle was silently accepted", file=sys.stderr)
    sys.exit(1)

message = completed.stderr.strip() or completed.stdout.strip()
if "include cycle detected" not in message:
    print("error: include cycle did not report a clear cycle diagnostic", file=sys.stderr)
    print(message, file=sys.stderr)
    sys.exit(1)

print("verified: include cycle is detected explicitly")
print(message)
PY
